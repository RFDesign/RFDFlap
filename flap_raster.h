#if !defined(__FLAP_RASTER_H__)
#define __FLAP_RASTER_H__

#include "nc_types.h"
#include "nc_math.h"

struct Arena;

// @defines____________________________________________________________________
#define FLAP_RASTER_LANE_WIDTH    8
#define FLAP_RASTER_CURVE_COLOURS 8

#define FLAP_RASTER_COLOUR_BACKGROUND 0x0A0E14FF
#define FLAP_RASTER_COLOUR_PLOT       0x131721FF
#define FLAP_RASTER_COLOUR_LABEL      0xB3B1ADFF

// @types______________________________________________________________________
struct FLAPRasterImage {
    u32* Pixels;
    u32  Width;
    u32  Height;
};

// @runtime____________________________________________________________________
extern u32 FLAP_RASTER_CURVE_COLOUR_TABLE[FLAP_RASTER_CURVE_COLOURS];

// @functions__________________________________________________________________
FLAPRasterImage FLAPRasterImageAlloc(Arena* MemPool, u32 Width, u32 Height);
u32 FLAPRasterPixelFromColour(u32 Colour);
void FLAPRasterFill(FLAPRasterImage* Image, r2i32 Rect, u32 Pixel);
void FLAPRasterBlend(FLAPRasterImage* Image, r2i32 Rect, u32 Pixel, f32 Alpha);

#endif // __FLAP_RASTER_H__
