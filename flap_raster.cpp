#include "nc_arena.h"
#include "nc_memory.h"
#include "nc_simd.h"

#include "flap_raster.h"

#if NC_SIMD_LEVEL < 6
    #error "flap_raster needs AVX2"
#endif

u32 FLAP_RASTER_CURVE_COLOUR_TABLE[FLAP_RASTER_CURVE_COLOURS] = {
    0x59c2ffff,
    0xffb454ff,
    0xc2d94cff,
    0xf07178ff,
    0xd2a6ffff,
    0x95e6cbff,
    0xffee99ff,
    0xf29668ff
};

global readonly i32 FLAP_RASTER_TAIL_MASK_TABLE[2 * FLAP_RASTER_LANE_WIDTH] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
     0,  0,  0,  0,  0,  0,  0,  0
};

INTERNAL FLAPRasterImage
FLAPRasterImageAlloc(Arena* MemPool, u32 Width, u32 Height)
{
    FLAPRasterImage Result = {};

    Result.Pixels = (u32*) ArenaPushSize(
        MemPool,
        sizeof(u32) * (u64) Width * (u64) Height,
        NC_SIMD_ALIGN256,
        FALSE
    );
    Result.Width = Width;
    Result.Height = Height;

    return Result;
}

INTERNAL u32
FLAPRasterPixelFromColour(u32 Colour)
{
    u32 Result = (
        (Colour >> 24) |
        ((Colour >> 8) & 0x0000FF00) |
        ((Colour << 8) & 0x00FF0000) |
        0xFF000000
    );

    return Result;
}

INTERNAL void
FLAPRasterFill(FLAPRasterImage* Image, r2i32 Rect, u32 Pixel)
{
    r2i32 Clip = Intersect(
        Rect,
        Rng(0, 0, (i32) Image->Width, (i32) Image->Height)
    );

    if (Clip.X1 <= Clip.X0 || Clip.Y1 <= Clip.Y0)
        return;

    u64 Count = (u64) (Clip.X1 - Clip.X0);
    u64 Rows = (u64) (Clip.Y1 - Clip.Y0);
    u32* Row = Image->Pixels + (u64) Clip.Y0 * Image->Width + Clip.X0;

    if (Count == Image->Width) {
        Count *= Rows;
        Rows = 1;
    }

    v256 Wide = _mm256_set1_epi32((i32) Pixel);
    v256 Tail = _mm256_loadu_si256(
        (v256 const*) &FLAP_RASTER_TAIL_MASK_TABLE[
            FLAP_RASTER_LANE_WIDTH - (Count & (FLAP_RASTER_LANE_WIDTH - 1))
        ]
    );

    for (u64 Y = 0; Y < Rows; ++Y, Row += Image->Width) {
        u64 X = 0;

        for (
            ;
            X + FLAP_RASTER_LANE_WIDTH <= Count;
            X += FLAP_RASTER_LANE_WIDTH
        ) {
            _mm256_storeu_si256((v256*) (Row + X), Wide);
        }

        if (X < Count)
            _mm256_maskstore_epi32((int*) (Row + X), Tail, Wide);
    }
}

INTERNAL v256
FLAPRasterBlendChannel(v256 Dst, v256f Src, v256f Alpha)
{
    v256f Value = _mm256_cvtepi32_ps(Dst);

    Value = _mm256_mul_ps(Value, Value);
    Value = _mm256_add_ps(
        Value,
        _mm256_mul_ps(_mm256_sub_ps(Src, Value), Alpha)
    );

    v256 Result = _mm256_cvtps_epi32(_mm256_sqrt_ps(Value));

    return Result;
}

INTERNAL void
FLAPRasterBlend(FLAPRasterImage* Image, r2i32 Rect, u32 Pixel, f32 Alpha)
{
    r2i32 Clip = Intersect(
        Rect,
        Rng(0, 0, (i32) Image->Width, (i32) Image->Height)
    );

    if (Clip.X1 <= Clip.X0 || Clip.Y1 <= Clip.Y0)
        return;

    u64 Count = (u64) (Clip.X1 - Clip.X0);
    u64 Rows = (u64) (Clip.Y1 - Clip.Y0);
    u32* Row = Image->Pixels + (u64) Clip.Y0 * Image->Width + Clip.X0;

    if (Count == Image->Width) {
        Count *= Rows;
        Rows = 1;
    }

    f32 Red = (f32) (Pixel & 0xFF);
    f32 Green = (f32) ((Pixel >> 8) & 0xFF);
    f32 Blue = (f32) ((Pixel >> 16) & 0xFF);
    v256f SrcRed = _mm256_set1_ps(Red * Red);
    v256f SrcGreen = _mm256_set1_ps(Green * Green);
    v256f SrcBlue = _mm256_set1_ps(Blue * Blue);
    v256f WideAlpha = _mm256_set1_ps(CLAMP(0.0f, Alpha, 1.0f));
    v256 ByteMask = _mm256_set1_epi32(0xFF);
    v256 Opaque = _mm256_set1_epi32((i32) 0xFF000000);
    v256 Whole = _mm256_set1_epi32(-1);
    v256 Tail = _mm256_loadu_si256(
        (v256 const*) &FLAP_RASTER_TAIL_MASK_TABLE[
            FLAP_RASTER_LANE_WIDTH - (Count & (FLAP_RASTER_LANE_WIDTH - 1))
        ]
    );

    for (u64 Y = 0; Y < Rows; ++Y, Row += Image->Width) {
        for (u64 X = 0; X < Count; X += FLAP_RASTER_LANE_WIDTH) {
            v256 Mask = (X + FLAP_RASTER_LANE_WIDTH <= Count)
                ? Whole
                : Tail;
            v256 Dst = _mm256_maskload_epi32((int const*) (Row + X), Mask);
            v256 OutRed = FLAPRasterBlendChannel(
                _mm256_and_si256(Dst, ByteMask),
                SrcRed,
                WideAlpha
            );
            v256 OutGreen = FLAPRasterBlendChannel(
                _mm256_and_si256(_mm256_srli_epi32(Dst, 8), ByteMask),
                SrcGreen,
                WideAlpha
            );
            v256 OutBlue = FLAPRasterBlendChannel(
                _mm256_and_si256(_mm256_srli_epi32(Dst, 16), ByteMask),
                SrcBlue,
                WideAlpha
            );
            v256 Out = _mm256_or_si256(
                _mm256_or_si256(OutRed, _mm256_slli_epi32(OutGreen, 8)),
                _mm256_or_si256(_mm256_slli_epi32(OutBlue, 16), Opaque)
            );

            _mm256_maskstore_epi32((int*) (Row + X), Mask, Out);
        }
    }
}
