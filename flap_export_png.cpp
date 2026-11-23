#include "nc_arena.h"
#include "nc_memory.h"

#include "flap_export.h"

INTERNAL void*
FLAPExportPNGRealloc(void* Ptr, u64 OldSize, u64 NewSize)
{
    void* Result = ArenaPushSize(
        GetTLS()->FLAP_EXPORT_PNG_MEMPOOL, 
        NewSize, 
        16, 
        FALSE
    );

    if (Ptr)
        MemCpy(Result, Ptr, MIN(OldSize, NewSize));

    return Result;
}

#if !defined(STB_IMAGE_WRITE_INCLUDED)
    #define STBI_WRITE_NO_STDIO
    #define STB_IMAGE_WRITE_STATIC
    #define STBIW_ASSERT(X) ASSERT(X)
    #define STBIW_MALLOC(Size) FLAPExportPNGRealloc(NULL, 0, (u64) (Size))
    #define STBIW_REALLOC_SIZED(Ptr, OldSize, NewSize) FLAPExportPNGRealloc((Ptr), (u64) (OldSize), (u64) (NewSize))
    #define STBIW_FREE(Ptr) ((void) (Ptr))
    #define STBIW_MEMMOVE(Dst, Src, Size) MemMove((Dst), (Src), (Size))
    #define STB_IMAGE_WRITE_IMPLEMENTATION
    #define STB_IMAGE_WRITE_INCLUDED
    #include "stb_image_write.h"
#endif

// NOTE(nc): stb calls this one time, with the whole file. the bytes are in
//         : the arena and free does nothing, so the blob is not a copy.
INTERNAL void
FLAPExportPNGWrite(void* Context, void* Data, int Size)
{
    FLAPExportWrite(
        (FLAPExportWriter*) Context,
        Str((u8*) Data, (u64) Size)
    );
}

INTERNAL
FLAP_EXPORT_FUNC_SIG(PNG)
{
    b32 Result = FALSE;

    if (Source->Pixels && Source->Size.X > 0 && Source->Size.Y > 0) {
        GetTLS()->FLAP_EXPORT_PNG_MEMPOOL = MemPool;
        Result = !!stbi_write_png_to_func(
            FLAPExportPNGWrite,
            Writer,
            Source->Size.X,
            Source->Size.Y,
            4,
            Source->Pixels,
            Source->Size.X * 4
        );
        GetTLS()->FLAP_EXPORT_PNG_MEMPOOL = NULL;
    }

    return Result;
}
