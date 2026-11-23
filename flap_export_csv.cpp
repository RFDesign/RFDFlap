#include "nc_arena.h"

#include "flap_export.h"
#include "flap_widgets.h"

// NOTE(nc): a field with a comma, a quote or a line break in it is put in
//         : quotes, and each quote in it is written two times.
INTERNAL void
FLAPExportCSVField(FLAPExportWriter* Writer, Str8 Field)
{
    b32 NeedsQuotes = FALSE;

    for (u64 Index = 0; Index < Field.Size && !NeedsQuotes; ++Index) {
        u8 Byte = Field.Str[Index];

        NeedsQuotes = (
            Byte == ',' ||
            Byte == '"' ||
            Byte == '\n' ||
            Byte == '\r'
        );
    }

    if (!NeedsQuotes) {
        FLAPExportWrite(Writer, Field);
    } else {
        u64 Start = 0;

        FLAPExportWrite(Writer, "\""_s8);

        for (u64 Index = 0; Index < Field.Size; ++Index) {
            if (Field.Str[Index] != '"')
                continue;

            FLAPExportWrite(
                Writer,
                Str(Field.Str + Start, Index + 1 - Start)
            );
            FLAPExportWrite(Writer, "\""_s8);
            Start = Index + 1;
        }

        FLAPExportWrite(Writer, Str(Field.Str + Start, Field.Size - Start));
        FLAPExportWrite(Writer, "\""_s8);
    }
}

INTERNAL Str8
FLAPStrFromCSVField(Arena* MemPool, Str8 Field)
{
    Str8 Result = Field;
    u64 QuotesCount = 0;
    b32 NeedsQuotes = FALSE;

    for (u64 Index = 0; Index < Field.Size; ++Index) {
        u8 Byte = Field.Str[Index];

        QuotesCount += (Byte == '"');
        NeedsQuotes |= (
            Byte == ',' ||
            Byte == '"' ||
            Byte == '\n' ||
            Byte == '\r'
        );
    }

    if (NeedsQuotes) {
        u64 At = 0;

        Result.Size = Field.Size + QuotesCount + 2;
        Result.Str = ArenaPushArray(MemPool, u8, Result.Size);
        Result.Str[At++] = '"';

        for (u64 Index = 0; Index < Field.Size; ++Index) {
            if (Field.Str[Index] == '"')
                Result.Str[At++] = '"';

            Result.Str[At++] = Field.Str[Index];
        }

        Result.Str[At++] = '"';
    }

    return Result;
}

INTERNAL
FLAP_EXPORT_FUNC_SIG(CSV)
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
        Str8* Cells = ArenaPushArray(MemPool, Str8, Table->ColumnCount + 1);
        u64 RowPosition = 0;

        FLAPExportWrite(Writer, "time"_s8);

        for (u32 Index = 0; Index < Table->ColumnCount; ++Index) {
            FLAPExportWrite(Writer, ","_s8);
            FLAPExportCSVField(
                Writer,
                Ctx.Program->Strings[Table->Columns[Index].LabelStringIndex]
            );
        }

        FLAPExportWrite(Writer, "\n"_s8);
        RowPosition = ArenaGetPosition(MemPool);

        for (u64 Row = 0; Row < Table->RowCount; ++Row) {
            u64 TimeUSecs = 0;

            FLAPExportTableRow(MemPool, &Ctx, Row, Cells, &TimeUSecs);

            // NOTE(nc): the time is the time in the flight, in seconds, as
            //         : a number. the table shows it from its first row.
            FLAPExportCommit(
                Writer,
                FLAPExportPutTime(
                    FLAPExportReserve(Writer, FLAP_EXPORT_ROW_SIZE),
                    TimeUSecs
                )
            );

            for (u32 Index = 0; Index < Table->ColumnCount; ++Index) {
                Str8 Cell = Cells[Index + 1];

                // NOTE(nc): the table shows "-" where there is no value.
                //         : in a number column a "-" alone is never a
                //         : value, so the field is left empty.
                if (
                    Cell.Size == 1 &&
                    Cell.Str[0] == '-' &&
                    !FLAPScalarKindIsChar(Table->Columns[Index].Kind)
                ) {
                    Cell.Size = 0;
                }

                FLAPExportWrite(Writer, ","_s8);
                FLAPExportCSVField(Writer, Cell);
            }

            FLAPExportWrite(Writer, "\n"_s8);
            ArenaPopTo(MemPool, RowPosition);
        }

        Result = TRUE;
    }

    return Result;
}
