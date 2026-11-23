#include "nc_simd.h"
#include "nc_string.h"
#include "nc_font_provider.h"

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

struct FLAPRasterShape {
    v256f X0;
    v256f X1;
    v256f Top0;
    v256f Top1;
    v256f Bottom0;
    v256f Bottom1;
    v256f DeltaX;
    v256f TopDeltaY;
    v256f BottomDeltaY;
    v256f TopInvLengthSq;
    v256f BottomInvLengthSq;
    v256f TopSlope;
    v256f BottomSlope;
    v256f Reach;
    v256f Limit;
};

INTERNAL FLAPRasterCanvas
FLAPRasterCanvasAlloc(Arena* MemPool, u32 Width, u32 Height)
{
    FLAPRasterCanvas Result = {};

    Result.Image = FLAPRasterImageAlloc(MemPool, Width, Height);
    Result.Mask.Width = Width;
    Result.Mask.Height = Height;
    Result.Mask.Pitch = ALIGNTO(Width + FLAP_RASTER_LANE_WIDTH, 32);
    Result.Mask.Coverage = (u8*) ArenaPushSize(
        MemPool,
        (u64) Result.Mask.Pitch * (u64) Height,
        NC_SIMD_ALIGN256,
        TRUE
    );

    return Result;
}

INTERNAL void
FLAPRasterMaskClear(FLAPRasterMask* Mask)
{
    MemZero(Mask->Coverage, (u64) Mask->Pitch * (u64) Mask->Height);
}

// NOTE(nc): the coverage of 8 pixels of one row, as 8 bytes. it is from
//         : the distance of the middle of the pixel to the ribbon: all of
//         : it at Radius - 0.5 and less, none of it at Radius + 0.5 and
//         : more. a pixel past the width of the mask has none.
INTERNAL v128
FLAPRasterRibbonCoverage(FLAPRasterShape const* Shape, v256f PX, f32 PY)
{
    v256f Zero = _mm256_setzero_ps();
    v256f One = _mm256_set1_ps(1.0f);
    v256f Y = _mm256_set1_ps(PY);
    v256f FromX = _mm256_sub_ps(PX, Shape->X0);
    v256f ToX = _mm256_sub_ps(PX, Shape->X1);
    v256f TopY = _mm256_sub_ps(Y, Shape->Top0);
    v256f BottomY = _mm256_sub_ps(Y, Shape->Bottom0);

    // NOTE(nc): the two sides are vertical, so the nearest point of a side
    //         : is Y held between its top and its bottom
    v256f LeftY = _mm256_sub_ps(
        Y,
        _mm256_min_ps(_mm256_max_ps(Y, Shape->Top0), Shape->Bottom0)
    );
    v256f RightY = _mm256_sub_ps(
        Y,
        _mm256_min_ps(_mm256_max_ps(Y, Shape->Top1), Shape->Bottom1)
    );
    v256f DistSq = _mm256_min_ps(
        _mm256_add_ps(
            _mm256_mul_ps(FromX, FromX),
            _mm256_mul_ps(LeftY, LeftY)
        ),
        _mm256_add_ps(
            _mm256_mul_ps(ToX, ToX),
            _mm256_mul_ps(RightY, RightY)
        )
    );

    // NOTE(nc): the top edge and the bottom edge, each as the nearest
    //         : point of a segment
    v256f TopT = _mm256_mul_ps(
        _mm256_add_ps(
            _mm256_mul_ps(FromX, Shape->DeltaX),
            _mm256_mul_ps(TopY, Shape->TopDeltaY)
        ),
        Shape->TopInvLengthSq
    );
    v256f BottomT = _mm256_mul_ps(
        _mm256_add_ps(
            _mm256_mul_ps(FromX, Shape->DeltaX),
            _mm256_mul_ps(BottomY, Shape->BottomDeltaY)
        ),
        Shape->BottomInvLengthSq
    );

    TopT = _mm256_min_ps(_mm256_max_ps(TopT, Zero), One);
    BottomT = _mm256_min_ps(_mm256_max_ps(BottomT, Zero), One);

    v256f TopEX = _mm256_sub_ps(FromX, _mm256_mul_ps(TopT, Shape->DeltaX));
    v256f TopEY = _mm256_sub_ps(
        TopY,
        _mm256_mul_ps(TopT, Shape->TopDeltaY)
    );
    v256f BottomEX = _mm256_sub_ps(
        FromX,
        _mm256_mul_ps(BottomT, Shape->DeltaX)
    );
    v256f BottomEY = _mm256_sub_ps(
        BottomY,
        _mm256_mul_ps(BottomT, Shape->BottomDeltaY)
    );

    DistSq = _mm256_min_ps(
        DistSq,
        _mm256_add_ps(
            _mm256_mul_ps(TopEX, TopEX),
            _mm256_mul_ps(TopEY, TopEY)
        )
    );
    DistSq = _mm256_min_ps(
        DistSq,
        _mm256_add_ps(
            _mm256_mul_ps(BottomEX, BottomEX),
            _mm256_mul_ps(BottomEY, BottomEY)
        )
    );

    // NOTE(nc): a pixel in the ribbon has no distance to it
    v256f Inside = _mm256_and_ps(
        _mm256_and_ps(
            _mm256_cmp_ps(FromX, Zero, _CMP_GE_OQ),
            _mm256_cmp_ps(ToX, Zero, _CMP_LE_OQ)
        ),
        _mm256_and_ps(
            _mm256_cmp_ps(
                TopY,
                _mm256_mul_ps(FromX, Shape->TopSlope),
                _CMP_GE_OQ
            ),
            _mm256_cmp_ps(
                BottomY,
                _mm256_mul_ps(FromX, Shape->BottomSlope),
                _CMP_LE_OQ
            )
        )
    );
    v256f Dist = _mm256_andnot_ps(Inside, _mm256_sqrt_ps(DistSq));
    v256f Cover = _mm256_min_ps(
        _mm256_max_ps(_mm256_sub_ps(Shape->Reach, Dist), Zero),
        One
    );

    Cover = _mm256_and_ps(
        Cover,
        _mm256_cmp_ps(PX, Shape->Limit, _CMP_LT_OQ)
    );

    v256 Wide = _mm256_cvtps_epi32(
        _mm256_mul_ps(Cover, _mm256_set1_ps(255.0f))
    );
    v128 Narrow = _mm_packus_epi32(
        _mm256_castsi256_si128(Wide),
        _mm256_extracti128_si256(Wide, 1)
    );
    v128 Result = _mm_packus_epi16(Narrow, Narrow);

    return Result;
}

// NOTE(nc): a ribbon is the quad from the top edge to the bottom edge,
//         : between two X, and all that is Radius or less from it. the
//         : positions are from the corner of the mask, and the middle of
//         : the first pixel is at 0.5. the mask keeps the larger coverage,
//         : so the shapes of one curve are one shape.
INTERNAL void
FLAPRasterRibbon(
    FLAPRasterMask* Mask,
    v2f32 Top0,
    v2f32 Top1,
    f32 Bottom0,
    f32 Bottom1,
    f32 Radius
) {
    if (Top1.X < Top0.X) {
        SWAP(v2f32, Top0, Top1);
        SWAP(f32, Bottom0, Bottom1);
    }

    if (Bottom0 < Top0.Y)
        SWAP(f32, Top0.Y, Bottom0);

    if (Bottom1 < Top1.Y)
        SWAP(f32, Top1.Y, Bottom1);

    f32 Reach = Radius + 0.5f;
    f32 DeltaX = Top1.X - Top0.X;
    f32 TopDeltaY = Top1.Y - Top0.Y;
    f32 BottomDeltaY = Bottom1 - Bottom0;
    f32 TopMin = MIN(Top0.Y, Top1.Y);
    f32 TopMax = MAX(Top0.Y, Top1.Y);
    f32 BottomMin = MIN(Bottom0, Bottom1);
    f32 BottomMax = MAX(Bottom0, Bottom1);
    f32 Width = (f32) Mask->Width;
    f32 Height = (f32) Mask->Height;
    i32 MinX = (i32) CLAMP(0.0f, FloorF32(Top0.X - Reach), Width);
    i32 MaxX = (i32) CLAMP(0.0f, CeilF32(Top1.X + Reach), Width);
    i32 MinY = (i32) CLAMP(0.0f, FloorF32(TopMin - Reach), Height);
    i32 MaxY = (i32) CLAMP(0.0f, CeilF32(BottomMax + Reach), Height);

    if (MinX >= MaxX || MinY >= MaxY)
        return;

    // NOTE(nc): the rows that are below all of the top edge and above all
    //         : of the bottom edge. the coverage of such a row is from X
    //         : only, so one vector is the coverage of all of them.
    i32 FlatMin = (i32) CLAMP(
        (f32) MinY,
        CeilF32(TopMax - 0.5f),
        (f32) MaxY
    );
    i32 FlatMax = (i32) CLAMP(
        (f32) MinY,
        FloorF32(BottomMin - 0.5f) + 1.0f,
        (f32) MaxY
    );
    b32 HasFlat = (TopMax <= BottomMin && FlatMin < FlatMax);
    FLAPRasterShape Shape = {};

    Shape.X0 = _mm256_set1_ps(Top0.X);
    Shape.X1 = _mm256_set1_ps(Top1.X);
    Shape.Top0 = _mm256_set1_ps(Top0.Y);
    Shape.Top1 = _mm256_set1_ps(Top1.Y);
    Shape.Bottom0 = _mm256_set1_ps(Bottom0);
    Shape.Bottom1 = _mm256_set1_ps(Bottom1);
    Shape.DeltaX = _mm256_set1_ps(DeltaX);
    Shape.TopDeltaY = _mm256_set1_ps(TopDeltaY);
    Shape.BottomDeltaY = _mm256_set1_ps(BottomDeltaY);
    // NOTE(nc): an edge with no length is a point, and its T is 0
    Shape.TopInvLengthSq = _mm256_set1_ps(
        1.0f / MAX(DeltaX * DeltaX + TopDeltaY * TopDeltaY, 1e-12f)
    );
    Shape.BottomInvLengthSq = _mm256_set1_ps(
        1.0f / MAX(DeltaX * DeltaX + BottomDeltaY * BottomDeltaY, 1e-12f)
    );
    Shape.TopSlope = _mm256_set1_ps(
        (DeltaX > 0.0f) ? TopDeltaY / DeltaX : 0.0f
    );
    Shape.BottomSlope = _mm256_set1_ps(
        (DeltaX > 0.0f) ? BottomDeltaY / DeltaX : 0.0f
    );
    Shape.Reach = _mm256_set1_ps(Reach);
    Shape.Limit = _mm256_set1_ps(Width);

    v256f Lanes = _mm256_setr_ps(
        0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f
    );

    for (i32 X = MinX; X < MaxX; X += FLAP_RASTER_LANE_WIDTH) {
        v256f PX = _mm256_add_ps(_mm256_set1_ps((f32) X), Lanes);
        v128 Flat = _mm_setzero_si128();
        u8* Dst = Mask->Coverage + (u64) MinY * Mask->Pitch + X;

        if (HasFlat) {
            Flat = FLAPRasterRibbonCoverage(
                &Shape,
                PX,
                0.5f * (TopMax + BottomMin)
            );
        }

        for (i32 Y = MinY; Y < MaxY; ++Y, Dst += Mask->Pitch) {
            v128 Cover = (HasFlat && Y >= FlatMin && Y < FlatMax)
                ? Flat
                : FLAPRasterRibbonCoverage(&Shape, PX, (f32) Y + 0.5f);

            _mm_storel_epi64(
                (v128*) Dst,
                _mm_max_epu8(_mm_loadl_epi64((v128 const*) Dst), Cover)
            );
        }
    }
}

// NOTE(nc): a line with round ends is a ribbon with no height
INTERNAL void
FLAPRasterLine(FLAPRasterMask* Mask, v2f32 From, v2f32 To, f32 Radius)
{
    FLAPRasterRibbon(Mask, From, To, From.Y, To.Y, Radius);
}

INTERNAL void
FLAPRasterDisc(FLAPRasterMask* Mask, v2f32 Centre, f32 Radius)
{
    FLAPRasterRibbon(Mask, Centre, Centre, Centre.Y, Centre.Y, Radius);
}

// NOTE(nc): Pixel over the image, with the coverage of the mask as its
//         : alpha. Origin is where the corner of the mask is in the image,
//         : and all of the mask is in the image. a block of 32 pixels with
//         : no coverage is one test, and a pixel with none is not read
//         : and is not written.
INTERNAL void
FLAPRasterBlendMask(
    FLAPRasterImage* Image,
    FLAPRasterMask const* Mask,
    v2i32 Origin,
    u32 Pixel
) {
    ASSERT(Origin.X >= 0 && Origin.X + (i32) Mask->Width <= (i32) Image->Width);
    ASSERT(Origin.Y >= 0 && Origin.Y + (i32) Mask->Height <= (i32) Image->Height);

    f32 Red = (f32) (Pixel & 0xFF);
    f32 Green = (f32) ((Pixel >> 8) & 0xFF);
    f32 Blue = (f32) ((Pixel >> 16) & 0xFF);
    v256f SrcRed = _mm256_set1_ps(Red * Red);
    v256f SrcGreen = _mm256_set1_ps(Green * Green);
    v256f SrcBlue = _mm256_set1_ps(Blue * Blue);
    v256f AlphaScale = _mm256_set1_ps(1.0f / 255.0f);
    v256 ByteMask = _mm256_set1_epi32(0xFF);
    v256 Opaque = _mm256_set1_epi32((i32) 0xFF000000);

    for (u32 Y = 0; Y < Mask->Height; ++Y) {
        u8 const* Coverage = Mask->Coverage + (u64) Y * Mask->Pitch;
        u32* Row = (
            Image->Pixels +
            (u64) (Origin.Y + (i32) Y) * Image->Width +
            Origin.X
        );

        for (u32 X = 0; X < Mask->Width; X += 32) {
            v256 Block = _mm256_load_si256((v256 const*) (Coverage + X));

            if (_mm256_testz_si256(Block, Block))
                continue;

            for (
                u32 Lane = 0;
                Lane < 32;
                Lane += FLAP_RASTER_LANE_WIDTH
            ) {
                v256 Wide = _mm256_cvtepu8_epi32(
                    _mm_loadl_epi64((v128 const*) (Coverage + X + Lane))
                );
                v256 Live = _mm256_cmpgt_epi32(
                    Wide,
                    _mm256_setzero_si256()
                );

                if (_mm256_testz_si256(Live, Live))
                    continue;

                u32* Dst = Row + X + Lane;
                v256f Alpha = _mm256_mul_ps(
                    _mm256_cvtepi32_ps(Wide),
                    AlphaScale
                );
                v256 Pixels = _mm256_maskload_epi32((int const*) Dst, Live);
                v256 OutRed = FLAPRasterBlendChannel(
                    _mm256_and_si256(Pixels, ByteMask),
                    SrcRed,
                    Alpha
                );
                v256 OutGreen = FLAPRasterBlendChannel(
                    _mm256_and_si256(_mm256_srli_epi32(Pixels, 8), ByteMask),
                    SrcGreen,
                    Alpha
                );
                v256 OutBlue = FLAPRasterBlendChannel(
                    _mm256_and_si256(_mm256_srli_epi32(Pixels, 16), ByteMask),
                    SrcBlue,
                    Alpha
                );
                v256 Out = _mm256_or_si256(
                    _mm256_or_si256(OutRed, _mm256_slli_epi32(OutGreen, 8)),
                    _mm256_or_si256(_mm256_slli_epi32(OutBlue, 16), Opaque)
                );

                _mm256_maskstore_epi32((int*) Dst, Live, Out);
            }
        }
    }
}

struct FLAPRasterSource {
    v256f Red;
    v256f Green;
    v256f Blue;
};

INTERNAL FLAPRasterSource
FLAPRasterSourceFromPixel(u32 Pixel)
{
    FLAPRasterSource Result = {};
    f32 Red = (f32) (Pixel & 0xFF);
    f32 Green = (f32) ((Pixel >> 8) & 0xFF);
    f32 Blue = (f32) ((Pixel >> 16) & 0xFF);

    Result.Red = _mm256_set1_ps(Red * Red);
    Result.Green = _mm256_set1_ps(Green * Green);
    Result.Blue = _mm256_set1_ps(Blue * Blue);

    return Result;
}

// NOTE(nc): the source over 8 pixels, with a coverage from 0 to 255 for
//         : each. a pixel with no coverage is not read and is not written,
//         : so the 8 can go past the end of a row.
INTERNAL void
FLAPRasterBlendLanes(u32* Dst, FLAPRasterSource const* Source, v256 Cover)
{
    v256 Live = _mm256_cmpgt_epi32(Cover, _mm256_setzero_si256());

    if (_mm256_testz_si256(Live, Live))
        return;

    v256 ByteMask = _mm256_set1_epi32(0xFF);
    v256f Alpha = _mm256_mul_ps(
        _mm256_cvtepi32_ps(Cover),
        _mm256_set1_ps(1.0f / 255.0f)
    );
    v256 Pixels = _mm256_maskload_epi32((int const*) Dst, Live);
    v256 OutRed = FLAPRasterBlendChannel(
        _mm256_and_si256(Pixels, ByteMask),
        Source->Red,
        Alpha
    );
    v256 OutGreen = FLAPRasterBlendChannel(
        _mm256_and_si256(_mm256_srli_epi32(Pixels, 8), ByteMask),
        Source->Green,
        Alpha
    );
    v256 OutBlue = FLAPRasterBlendChannel(
        _mm256_and_si256(_mm256_srli_epi32(Pixels, 16), ByteMask),
        Source->Blue,
        Alpha
    );
    v256 Out = _mm256_or_si256(
        _mm256_or_si256(OutRed, _mm256_slli_epi32(OutGreen, 8)),
        _mm256_or_si256(
            _mm256_slli_epi32(OutBlue, 16),
            _mm256_set1_epi32((i32) 0xFF000000)
        )
    );

    _mm256_maskstore_epi32((int*) Dst, Live, Out);
}

// NOTE(nc): a colour of a theme as the draw data of a graph has it. the
//         : pixel of it is FLAPGraphMinimapPixel.
INTERNAL v4f32
FLAPRasterVecFromColour(u32 Colour)
{
    v4f32 Result = Vec(
        (f32) ((Colour >> 24) & 0xFF) / 255.0f,
        (f32) ((Colour >> 16) & 0xFF) / 255.0f,
        (f32) ((Colour >> 8) & 0xFF) / 255.0f,
        (f32) (Colour & 0xFF) / 255.0f
    );

    return Result;
}

// NOTE(nc): the provider is called here and nowhere else, one time for
//         : each glyph, so this is for one thread before the lanes draw.
//         : the ascent and the descent are as FontMetricsFromTagSize has
//         : them. a glyph has 8 bytes after its coverage, so a read of 8
//         : lanes at its end is in its memory.
INTERNAL FLAPRasterFont*
FLAPRasterFontFromHandle(Arena* MemPool, FPHandle Handle, f32 Size)
{
    TempArena Scratch = GetScratch(&MemPool, 1);
    FLAPRasterFont* Result = ArenaPushArrayZero(MemPool, FLAPRasterFont, 1);
    FPMetrics Metrics = MetricsFromFont(Handle);
    f32 SizeFloored = FloorF32(Size);

    Result->Size = Size;

    if (Metrics.DesignUnitsPerEM > 0.0f) {
        Result->Ascent = (
            SizeFloored * Metrics.Ascent / Metrics.DesignUnitsPerEM
        );
        Result->Descent = (
            SizeFloored * Metrics.Descent / Metrics.DesignUnitsPerEM
        );
    }

    for (u32 Index = 0; Index < FLAP_RASTER_GLYPH_COUNT; ++Index) {
        FLAPRasterGlyph* Glyph = &Result->Glyphs[Index];
        u64 Position = ArenaGetPosition(Scratch.MemPool);
        u8 Byte = (u8) (FLAP_RASTER_GLYPH_FIRST + Index);
        FPRasterResult Raw = Raster(
            Scratch.MemPool,
            Handle,
            SizeFloored,
            FP_RASTER_KIND_SMOOTH | FP_RASTER_KIND_HINTED,
            Str(&Byte, 1)
        );

        Glyph->Advance = Raw.Advance;

        if (
            Raw.Atlas &&
            Raw.AtlasDimensions.X > 0 &&
            Raw.AtlasDimensions.Y > 0
        ) {
            u64 Count = (
                (u64) Raw.AtlasDimensions.X * (u64) Raw.AtlasDimensions.Y
            );
            u8 const* Atlas = (u8 const*) Raw.Atlas;

            Glyph->Size = Raw.AtlasDimensions;
            Glyph->Coverage = ArenaPushArrayZero(
                MemPool,
                u8,
                Count + FLAP_RASTER_LANE_WIDTH
            );

            // NOTE(nc): the atlas is white with the coverage as its alpha
            for (u64 Pixel = 0; Pixel < Count; ++Pixel)
                Glyph->Coverage[Pixel] = Atlas[Pixel * 4 + 3];
        }

        ArenaPopTo(Scratch.MemPool, Position);
    }

    ReleaseScratch(Scratch);

    return Result;
}

INTERNAL f32
FLAPRasterTextWidth(FLAPRasterFont const* Font, Str8 String)
{
    f32 Result = 0.0f;

    for (u64 Index = 0; Index < String.Size; ++Index) {
        u32 Code = String.Str[Index];

        if (
            Code >= FLAP_RASTER_GLYPH_FIRST &&
            Code < FLAP_RASTER_GLYPH_FIRST + FLAP_RASTER_GLYPH_COUNT
        ) {
            Result += Font->Glyphs[Code - FLAP_RASTER_GLYPH_FIRST].Advance;
        }
    }

    return Result;
}

// NOTE(nc): a line of text, with Position as the top left of it, as
//         : FLAPGraphText takes it. the baseline is the top and the
//         : ascent, and the top of a glyph is the ascent and the descent
//         : above the baseline, as the font cache has it for a piece. a
//         : byte that the font has no glyph for is not drawn, and a glyph
//         : that is not all in the image is not drawn.
INTERNAL void
FLAPRasterText(
    FLAPRasterImage* Image,
    FLAPRasterFont const* Font,
    v2f32 Position,
    Str8 String,
    u32 Pixel
) {
    FLAPRasterSource Source = FLAPRasterSourceFromPixel(Pixel);
    i32 Baseline = (i32) FloorF32(Position.Y + Font->Ascent);
    i32 Top = Baseline - (i32) (Font->Ascent + Font->Descent);
    f32 PenX = FloorF32(Position.X);

    for (u64 Index = 0; Index < String.Size; ++Index) {
        u32 Code = String.Str[Index];

        if (
            Code < FLAP_RASTER_GLYPH_FIRST ||
            Code >= FLAP_RASTER_GLYPH_FIRST + FLAP_RASTER_GLYPH_COUNT
        ) {
            continue;
        }

        FLAPRasterGlyph const* Glyph = &Font->Glyphs[
            Code - FLAP_RASTER_GLYPH_FIRST
        ];
        i32 Left = (i32) PenX;
        i32 Width = (i32) Glyph->Size.X;
        i32 Height = (i32) Glyph->Size.Y;

        PenX += Glyph->Advance;

        if (
            !Glyph->Coverage ||
            Left < 0 ||
            Top < 0 ||
            Left + Width > (i32) Image->Width ||
            Top + Height > (i32) Image->Height
        ) {
            continue;
        }

        for (i32 Y = 0; Y < Height; ++Y) {
            u8 const* Coverage = Glyph->Coverage + (u64) Y * (u64) Width;
            u32* Dst = (
                Image->Pixels +
                (u64) (Top + Y) * Image->Width +
                Left
            );

            for (i32 X = 0; X < Width; X += FLAP_RASTER_LANE_WIDTH) {
                i32 Count = Width - X;
                v256 Cover = _mm256_cvtepu8_epi32(
                    _mm_loadl_epi64((v128 const*) (Coverage + X))
                );

                // NOTE(nc): the lanes past the end of the row of the
                //         : glyph are the start of its next row
                if (Count < FLAP_RASTER_LANE_WIDTH) {
                    Cover = _mm256_and_si256(
                        Cover,
                        _mm256_loadu_si256(
                            (v256 const*) &FLAP_RASTER_TAIL_MASK_TABLE[
                                FLAP_RASTER_LANE_WIDTH - Count
                            ]
                        )
                    );
                }

                FLAPRasterBlendLanes(Dst + X, &Source, Cover);
            }
        }
    }
}
