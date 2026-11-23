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

INTERNAL u8*
FLAPExportReserve(FLAPExportWriter* Writer, u64 Size)
{
    ASSERT(Size < Writer->Capacity);

    if (Writer->Size + Size > Writer->Capacity)
        FLAPExportFlush(Writer);

    return Writer->Chunk + Writer->Size;
}

INTERNAL void
FLAPExportCommit(FLAPExportWriter* Writer, u8* Ptr)
{
    Writer->Size = INT_FROM_PTR(Ptr - Writer->Chunk);
}

INTERNAL u8*
FLAPExportPutU64(u8* Ptr, u64 Value)
{
    u8 Digits[20];
    u32 Count = 0;

    do {
        Digits[Count++] = (u8) ('0' + Value % 10);
        Value /= 10;
    } while (Value);

    while (Count)
        *Ptr++ = Digits[--Count];

    return Ptr;
}

// NOTE(nc): the last Width digits of the value, with zeros in front
INTERNAL u8*
FLAPExportPutPadded(u8* Ptr, u64 Value, u32 Width)
{
    for (u32 Index = Width; Index--; ) {
        Ptr[Index] = (u8) ('0' + Value % 10);
        Value /= 10;
    }

    return Ptr + Width;
}

INTERNAL u8*
FLAPExportPutI64(u8* Ptr, i64 Value)
{
    u64 Magnitude = (u64) Value;

    if (Value < 0) {
        *Ptr++ = '-';
        Magnitude = (u64) 0 - Magnitude;
    }

    return FLAPExportPutU64(Ptr, Magnitude);
}

// NOTE(nc): the time in seconds, with all six digits of the microseconds
INTERNAL u8*
FLAPExportPutTime(u8* Ptr, u64 TimeUSecs)
{
    Ptr = FLAPExportPutU64(Ptr, TimeUSecs / MILLION(1));
    *Ptr++ = '.';
    Ptr = FLAPExportPutPadded(Ptr, TimeUSecs % MILLION(1), 6);

    return Ptr;
}

// NOTE(nc): a NaN, an infinity and a value that is too large for its
//         : whole part fail the test, and nothing is written for them.
INTERNAL u8*
FLAPExportPutF64(u8* Ptr, f64 Value)
{
    f64 Magnitude = AbsF64(Value);

    if (Magnitude < 1.0e18) {
        u64 Whole = (u64) Magnitude;
        u64 Part = (u64) (
            (Magnitude - (f64) Whole) * (f64) FLAP_EXPORT_F64_SCALE + 0.5
        );

        if (Part >= FLAP_EXPORT_F64_SCALE) {
            Part -= FLAP_EXPORT_F64_SCALE;
            ++Whole;
        }

        if (Value < 0.0 && (Whole || Part))
            *Ptr++ = '-';

        Ptr = FLAPExportPutU64(Ptr, Whole);
        *Ptr++ = '.';
        Ptr = FLAPExportPutPadded(Ptr, Part, FLAP_EXPORT_F64_DECIMALS);
    }

    return Ptr;
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
