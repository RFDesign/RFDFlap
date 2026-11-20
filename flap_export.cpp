#include "nc_arena.h"
#include "nc_file.h"
#include "nc_log.h"
#include "nc_memory.h"
#include "nc_profile.h"

#include "flv_run.h"

#include "flap_export.h"
#include "flap_widgets.h"

Str8 FLAP_EXPORT_KIND_EXTENSION_TABLE[FLAP_EXPORT_KIND_COUNT] = {
    Str8Lit(""),
    Str8Lit("png"),
    Str8Lit("csv"),
    Str8Lit("txt")
};

FLAPExportFuncType* FLAP_EXPORT_KIND_FUNC_TABLE[FLAP_EXPORT_KIND_COUNT] = {
    NULL,
    FLAP_EXPORT_FUNC_NAME(PNG),
    FLAP_EXPORT_FUNC_NAME(CSV),
    FLAP_EXPORT_FUNC_NAME(TXT)
};

INTERNAL void
FLAPExportFlush(FLAPExportWriter* Writer)
{
    if (Writer->Size) {
        u64 Written = SystemWriteFile(
            Writer->File,
            Rng(Writer->Offset, Writer->Offset + Writer->Size),
            Writer->Chunk
        );

        Writer->HasFailed |= (Written != Writer->Size);
        Writer->Offset += Writer->Size;
        Writer->Size = 0;
    }
}

INTERNAL void
FLAPExportWrite(FLAPExportWriter* Writer, Str8 String)
{
    if (String.Size >= Writer->Capacity) {
        // NOTE(nc): a string that is as large as the chunk goes to the
        //         : file as it is, with no copy.
        FLAPExportFlush(Writer);

        u64 Written = SystemWriteFile(
            Writer->File,
            Rng(Writer->Offset, Writer->Offset + String.Size),
            String.Str
        );

        Writer->HasFailed |= (Written != String.Size);
        Writer->Offset += String.Size;
    } else {
        if (Writer->Size + String.Size > Writer->Capacity)
            FLAPExportFlush(Writer);

        MemCpy(Writer->Chunk + Writer->Size, String.Str, String.Size);
        Writer->Size += String.Size;
    }
}

INTERNAL void
FLAPExportTableRow(
    Arena* MemPool,
    FLAPTableCtx* Ctx,
    u64 Row,
    OUT Str8* Cells,
    OUT u64* TimeUSecs
) {
    FLVRenderTable const* Table = Ctx->Table;
    u64 Sample = FLVSeriesSampleIndex(
        Table->Ranges,
        Table->RangesCount,
        FLAPTablePositionFromRow(Table, Row)
    );

    *TimeUSecs = FLVTimeAt(&Ctx->Time, Sample);
    Cells[0] = FLAPStrFromTimeOffset(
        MemPool,
        *TimeUSecs - Ctx->StartTimeUSecs
    );

    for (u32 Index = 0; Index < Table->ColumnCount; ++Index) {
        Cells[Index + 1] = FLAPStrFromCell(
            MemPool,
            Ctx,
            Index,
            Row,
            Sample
        );
    }
}

INTERNAL b32
FLAPExport(FLAPExportKind Kind, FLAPExportSource const* Source, Str8 FilePath)
{
    b32 Result = FALSE;

    if (Kind < FLAP_EXPORT_KIND_COUNT && FLAP_EXPORT_KIND_FUNC_TABLE[Kind]) {
        TempArena Scratch = GetScratch(NULL, 0);
        FLAPExportWriter Writer = {};

        Writer.File = SystemOpenFile(SYS_ACCESS_WRITE, FilePath);

        if (Writer.File != EMPTY_HANDLE_VALUE) {
            Writer.Capacity = FLAP_EXPORT_CHUNK_SIZE;
            Writer.Chunk = ArenaPushArray(
                Scratch.MemPool,
                u8,
                Writer.Capacity
            );

            PROFILE(FLAPExportTimer) {
                Result = FLAP_EXPORT_KIND_FUNC_TABLE[Kind](
                    Scratch.MemPool,
                    Source,
                    &Writer
                );
                FLAPExportFlush(&Writer);
                Result = (Result && !Writer.HasFailed);
            }

            SystemCloseFile(Writer.File);

            // NOTE(nc): a file that is not whole is not left on the disk
            if (!Result)
                SystemDeleteFile(FilePath);
        }

        if (!Result) {
            LogError("the export to %S did not work", PRINT_STR(FilePath));
        }

        ReleaseScratch(Scratch);
    }

    return Result;
}
