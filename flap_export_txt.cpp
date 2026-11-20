#include "nc_arena.h"

#include "flap_export.h"
#include "flap_widgets.h"

INTERNAL void
FLAPExportTXTPad(FLAPExportWriter* Writer, u64 Count)
{
    Str8 Spaces = "                                "_s8;

    while (Count) {
        u64 Size = MIN(Count, Spaces.Size);

        FLAPExportWrite(Writer, Str(Spaces.Str, Size));
        Count -= Size;
    }
}

// NOTE(nc): one row. a number is at the right of its column and text is at
//         : the left, as in the table on screen. the last column gets no
//         : spaces after it.
INTERNAL void
FLAPExportTXTRow(
    FLAPExportWriter* Writer,
    Str8* Cells,
    u64* Widths,
    b32* IsLeft,
    u32 Count
) {
    for (u32 Index = 0; Index < Count; ++Index) {
        u64 Pad = Widths[Index] - Cells[Index].Size;

        if (Index)
            FLAPExportWrite(Writer, "  "_s8);

        if (!IsLeft[Index])
            FLAPExportTXTPad(Writer, Pad);

        FLAPExportWrite(Writer, Cells[Index]);

        if (IsLeft[Index] && Index + 1 < Count)
            FLAPExportTXTPad(Writer, Pad);
    }

    FLAPExportWrite(Writer, "\n"_s8);
}

INTERNAL
FLAP_EXPORT_FUNC_SIG(TXT)
{
    b32 Result = FALSE;
    FLAPTableParams* Params = Source->Table;
    FLAPTableCtx Ctx = {};

    if (
        Params &&
        FLAPTableCtxFromParams(MemPool, Params, &Ctx) &&
        (!Ctx.Table->RowCount || !Ctx.Time.IsMissing)
    ) {
        FLVRenderTable const* Table = Ctx.Table;
        u32 Count = Table->ColumnCount + 1;
        Str8* Cells = ArenaPushArray(MemPool, Str8, Count);
        Str8* Labels = ArenaPushArray(MemPool, Str8, Count);
        u64* Widths = ArenaPushArrayZero(MemPool, u64, Count);
        b32* IsLeft = ArenaPushArrayZero(MemPool, b32, Count);
        u64 RowPosition = 0;

        Labels[0] = "time"_s8;

        for (u32 Index = 0; Index < Table->ColumnCount; ++Index) {
            Labels[Index + 1] = Ctx.Program->Strings[
                Table->Columns[Index].LabelStringIndex
            ];
            IsLeft[Index + 1] = FLAPScalarKindIsChar(
                Table->Columns[Index].Kind
            );
        }

        for (u32 Index = 0; Index < Count; ++Index)
            Widths[Index] = Labels[Index].Size;

        RowPosition = ArenaGetPosition(MemPool);

        // NOTE(nc): the width of a column is not known before all of its
        //         : cells are made, so the rows are made two times: one
        //         : time for the widths, one time for the file.
        for (u64 Row = 0; Row < Table->RowCount; ++Row) {
            u64 TimeUSecs = 0;

            FLAPExportTableRow(MemPool, &Ctx, Row, Cells, &TimeUSecs);

            for (u32 Index = 0; Index < Count; ++Index)
                Widths[Index] = MAX(Widths[Index], Cells[Index].Size);

            ArenaPopTo(MemPool, RowPosition);
        }

        FLAPExportTXTRow(Writer, Labels, Widths, IsLeft, Count);

        for (u64 Row = 0; Row < Table->RowCount; ++Row) {
            u64 TimeUSecs = 0;

            FLAPExportTableRow(MemPool, &Ctx, Row, Cells, &TimeUSecs);
            FLAPExportTXTRow(Writer, Cells, Widths, IsLeft, Count);
            ArenaPopTo(MemPool, RowPosition);
        }

        Result = TRUE;
    }

    return Result;
}
