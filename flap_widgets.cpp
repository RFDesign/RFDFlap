#include "nc_arena.h"
#include "nc_application.h"
#include "nc_draw.h"
#include "nc_memory.h"
#include "nc_ui_components.h"
#include "nc_render.h"
#include "nc_profile.h"

#include "flv_token.h"
#include "flv_stdlib.h"
#include "flv_compile.h"
#include "flv_run.h"

#include "flap_graph.h"
#include "flap_widgets.h"

global readonly FLAPGraphSettingInfo FLAP_GRAPH_SETTING_INFO_TABLE[FLAP_GRAPH_SETTING_KIND_COUNT] = {
    {
        Str8Lit("lock_y"),
        Str8Lit("Lock Y Axis")
    },
    {
        Str8Lit("show_minimap"),
        Str8Lit("Show Minimap")
    },
    {
        Str8Lit("show_flags_on_graph"),
        Str8Lit("Show Flags On Graph")
    },
    {
        Str8Lit("show_modes_on_graph"),
        Str8Lit("Show Modes On Graph")
    },
    {
        Str8Lit("show_flags_on_minimap"),
        Str8Lit("Show Flags On Minimap")
    },
    {
        Str8Lit("show_modes_on_minimap"),
        Str8Lit("Show Modes On Minimap")
    },
    {
        Str8Lit("time_from_cursor"),
        Str8Lit("Relative To Cursor")
    }
};

internal Str8
FLAPStrFromTimeOffset(Arena* MemPool, u64 USecs)
{
    Str8 Result = "0s"_s8;
 
    if (USecs) {
        Result = ArenaPushStrFmt(
            MemPool,
            "+ %llu.%03llus",
            USecs / MILLION(1),
            (USecs % MILLION(1)) / THOUSAND(1)
        );
    }
 
    return Result;
}
 
internal b32
FLAPScalarKindIsChar(FLIScalarKind Kind)
{
    b32 Result = (
        Kind == FLI_SCALAR_KIND_CHAR4 ||
        Kind == FLI_SCALAR_KIND_CHAR16 ||
        Kind == FLI_SCALAR_KIND_CHAR64
    );
 
    return Result;
}

internal u64
FLAPTablePositionFromRow(FLVRenderTable const* Table, u64 Row)
{
    u64 Result = Row;
 
    if (Table->Mask) {
        u64 Low = 0;
        u64 High = FLVSeriesWordCount(Table->SampleCount);
 
        while (High - Low > 1) {
            u64 Mid = Low + (High - Low) / 2;
 
            if (Table->Rank[Mid] <= Row)
                Low = Mid;
            else
                High = Mid;
        }
 
        u64 Word = Table->Mask[Low];
 
        for (u64 Skip = Row - Table->Rank[Low]; Skip; --Skip)
            Word &= Word - 1;
 
        Result = (Low << 6) + Ctz64(Word);
    }
 
    return Result;
}

internal Str8
FLAPStrFromCell(
    Arena* MemPool,
    FLAPTableCtx* Data,
    u32 ColumnIndex,
    u64 Row,
    u64 Sample
) {
    FLVRenderTableColumn const* Column = &Data->Table->Columns[ColumnIndex];
    FLVColumn const* Ref = &Data->Columns[ColumnIndex];
    Str8 Result = "-"_s8;
 
    if (Column->IsMissing)
        return Result;
 
    if (Column->Column != FLV_COLUMN_NONE) {
        if (Ref->IsMissing)
            return Result;
 
        if (FLAPScalarKindIsChar(Ref->Kind)) {
            u8 const* Chars = (u8 const*) Ref->Data + Sample * Ref->ElementSize;
            u64 Size = 0;
 
            for ( ; Size < Ref->ElementSize && Chars[Size]; ++Size);
 
            Result = Str((u8*) Chars, Size);
        } else {
            f64 Value = FLVColumnF64At(Ref, Sample);
            b32 IsInteger = (
                Ref->Scale == 1.0 &&
                Ref->Kind != FLI_SCALAR_KIND_F32 &&
                Ref->Kind != FLI_SCALAR_KIND_F64
            );
 
            if (Value != Value) {
                // NOTE(nathan): no-op
            } else if (IsInteger) {
                Result = ArenaPushStrFmt(MemPool, "%lld", (i64) Value);
            } else {
                Result = ArenaPushStrFmt(MemPool, "%.3f", Value);
            }
        }
    } else {
        u8 const* Bytes = (u8 const*) Column->Values + Row * Column->Width;
 
        if (FLAPScalarKindIsChar(Column->Kind)) {
            u64 Size = 0;
 
            for ( ; Size < Column->Width && Bytes[Size]; ++Size);
 
            Result = Str((u8*) Bytes, Size);
        } else if (Column->Kind == FLI_SCALAR_KIND_F64) {
            f64 Value = 0.0;
 
            MemCpy(&Value, Bytes, sizeof(Value));
 
            if (Value == Value)
                Result = ArenaPushStrFmt(MemPool, "%.3f", Value);
        } else {
            i64 Value = 0;
 
            MemCpy(&Value, Bytes, sizeof(Value));
 
            if (Value != FLV_I64_HOLE)
                Result = ArenaPushStrFmt(MemPool, "%lld", Value);
        }
    }
 
    return Result;
}

INTERNAL
UI_BOX_CUSTOM_DRAW(FLAPFlaggedRowDraw)
{
    v4f32 Colour = UIColourFromTagsKeyName(Box->TagsKey, "hover"_s8);
 
    UNUSED(UserData);
    Colour.W *= 0.5f;
    DrawRect(Box->Rect, Colour, 0.0f, 0.0f, 0.0f);
}

INTERNAL b32
FLAPTimeIsFlagged(FLAPTableCtx* Ctx, u64 TimeUSecs)
{
    b32 Result = FALSE;
 
    for (
        u32 FlagIndex = 0; 
        FlagIndex < Ctx->FlagsCount && !Result; 
        ++FlagIndex
    ) {
        FLVRenderFlag const* Flag = Ctx->Flags[FlagIndex];
 
        for (u64 Index = 0; Index < Flag->Count && !Result; ++Index)
            Result = InRange(Flag->Ranges[Index], TimeUSecs);
    }
 
    return Result;
}


INTERNAL Str8
FLAPFlagLine(
    Arena* MemPool,
    FLVProgram const* Program,
    FLVRenderItem* Items,
    u64 Row
) {
    Str8 Result = ""_s8;
 
    for (u32 Index = 0; Index < Program->EmitCount; ++Index) {
        if (Program->Emits[Index].DisplayKind != TOKEN_KIND_FLAG)
            continue;
 
        FLVRenderItem const* Item = &Items[Index];
        Str8 Label = Program->Strings[Program->Emits[Index].LabelStringIndex];
        u64 Lines = Item->IsMissing ? 1 : 1 + Item->Flag.Count;
 
        if (Row >= Lines) {
            Row -= Lines;
            continue;
        }
 
        if (!Row) {
            Result = Item->IsMissing
                ? ArenaPushStrFmt(MemPool, "%S: missing", PRINT_STR(Label))
                : ArenaPushStrFmt(
                    MemPool,
                    "%S: %llu, total %S",
                    PRINT_STR(Label),
                    Item->Flag.Count,
                    PRINT_STR(
                        FLAPStrFromTimeOffset(
                            MemPool,
                            Item->Flag.TotalDurationUSecs
                        )
                    )
                );
        } else {
            r1u64 Range = Item->Flag.Ranges[Row - 1];
 
            Result = ArenaPushStrFmt(
                MemPool,
                "  %S for %S",
                PRINT_STR(FLAPStrFromTimeOffset(MemPool, Range.Min)),
                PRINT_STR(FLAPStrFromTimeOffset(MemPool, Length(Range)))
            );
        }
 
        break;
    }
 
    return Result;
}

INTERNAL b32
FLAPTableCtxFromParams(
    Arena* MemPool,
    FLAPTableParams* Params,
    OUT FLAPTableCtx* Ctx
) {
    FLVProgram const* Program = Params->Program;
    FLVRenderItem* Items = Params->Items;
    FLVRenderTable const* Table = &Items[Params->HeadEmit].Table;
    b32 Result = TRUE;

    MemZero(Ctx, sizeof(*Ctx));
    Ctx->Table = Table;
    Ctx->Program = Program;
    Ctx->Columns = ArenaPushArrayZero(
        MemPool,
        FLVColumn,
        Table->ColumnCount
    );
    Ctx->Flags = ArenaPushArrayZero(
        MemPool,
        FLVRenderFlag const*,
        Program->EmitCount
    );

    Result &= FLVColumnInit(
        Params->Acc,
        Params->Flight,
        Params->FLI,
        Table->TimeColumn,
        0,
        &Ctx->Time
    );

    for (u32 Index = 0; Index < Table->ColumnCount; ++Index) {
        FLVRenderTableColumn const* Column = &Table->Columns[Index];

        if (Column->Column != FLV_COLUMN_NONE) {
            Result &= FLVColumnInit(
                Params->Acc,
                Params->Flight,
                Params->FLI,
                Column->Column,
                0,
                &Ctx->Columns[Index]
            );
        }
    }

    for (u32 Index = 0; Index < Program->EmitCount; ++Index) {
        if (
            Program->Emits[Index].DisplayKind == TOKEN_KIND_FLAG &&
            !Items[Index].IsMissing
        ) {
            Ctx->Flags[Ctx->FlagsCount++] = &Items[Index].Flag;
        }
    }

    if (Table->RowCount && !Ctx->Time.IsMissing) {
        u64 StartSample = FLVSeriesSampleIndex(
            Table->Ranges,
            Table->RangesCount,
            FLAPTablePositionFromRow(Table, 0)
        );

        Ctx->StartTimeUSecs = FLVTimeAt(&Ctx->Time, StartSample);
    }

    return Result;
}

INTERNAL void
FLAPTable(
    FLAPTableParams* Params, 
    r2f32 Rect
) {
    TempArena Scratch = GetScratch(NULL, 0);
    FLVProgram const* Program = Params->Program;
    FLVRenderItem* Items = Params->Items;
    FLVRenderTable const* Table = &Items[Params->HeadEmit].Table;
    FLAPTableCtx TableCtx = {};
    u32 ColumnCount = Table->ColumnCount + 1;
 
    FLAPTableCtxFromParams(Scratch.MemPool, Params, &TableCtx);
 
    UIScrollListParameters ListParams = {};
    UIScrollListSignal ListSig = {};
    r1i64 Visible = {};
 
    ListParams.Kind = UI_SCROLL_LIST_KIND_ALL;
    ListParams.DimensionsPX = Vec(
        Length(Rect).X,
        Length(Rect).Y - UIHeadPXHeight()
    );
    ListParams.RowHeightPX = UIHeadPXHeight();
    ListParams.CursorRange.Max.Y = (i64) Table->RowCount;
    ListParams.ItemRange = Rng((i64) 0, (i64) Table->RowCount);
    ListParams.CursorMinIsEmptySelection[AXIS_2D_Y] = TRUE;
 
    UITableFmt(ColumnCount, Params->Percents, "###table_%u", Params->Key) {
        UITag("menu_bar"_s8) {
            UISetNextFlags(UI_BOX_KIND_DRAW_BACKGROUND);
 
            UITableVector() {
                UITableCell() {
                    UILabel("time"_s8);
                }
 
                for (u32 Index = 0; Index < Table->ColumnCount; ++Index) {
                    UITableCell() {
                        UILabel(
                            Program->Strings[
                                Table->Columns[Index].LabelStringIndex
                            ]
                        );
                    }
                }
            }
        }
 
        UIScrollList(
            &ListParams,
            Params->Scroll,
            Params->Cursor,
            Params->Mark,
            &Visible,
            &ListSig
        ) {
            for (
                i64 Row = Visible.Min;
                Row <= Visible.Max && Row < (i64) Table->RowCount;
                ++Row
            ) {
                b32 IsCursor = (Params->Cursor->Y == Row + 1);
                u64 Sample = FLVSeriesSampleIndex(
                    Table->Ranges,
                    Table->RangesCount,
                    FLAPTablePositionFromRow(Table, (u64) Row)
                );
                u64 TimeUSecs = FLVTimeAt(&TableCtx.Time, Sample);
 
                UITag(
                    IsCursor 
                        ? "pop"_s8 
                        : ""_s8
                ) {
                    UISetNextFlags(
                        UI_BOX_KIND_MOUSE_CLICKABLE |
                        UI_BOX_KIND_DRAW_BACKGROUND |
                        UI_BOX_KIND_DRAW_HOT_EFFECTS
                    );
 
                    UIBox* RowBox = UITableVectorBegin();
 
                    if (FLAPTimeIsFlagged(&TableCtx, TimeUSecs)) {
                        UIBoxEquipCustomDraw(
                            RowBox, 
                            FLAPFlaggedRowDraw, 
                            NULL
                        );
                    }

                    UITextAlignment(UI_TEXT_ALIGN_RIGHT) {
                        UITableCell() {
                            UILabel(
                                "%S###c0",
                                PRINT_STR(
                                    FLAPStrFromTimeOffset(
                                        UIBuildMemPool(),
                                        TimeUSecs - TableCtx.StartTimeUSecs
                                    )
                                )
                            );
                        }
 
                        for (
                            u32 Index = 0; 
                            Index < Table->ColumnCount; 
                            ++Index
                        ) {
                            b32 IsChar = FLAPScalarKindIsChar(
                                Table->Columns[Index].Kind
                            );
 
                            UITextAlignment(
                                IsChar
                                    ? UI_TEXT_ALIGN_LEFT
                                    : UI_TEXT_ALIGN_RIGHT
                            ) {
                                UITableCell() {
                                    UILabel(
                                        "%S###c%u",
                                        PRINT_STR(
                                            FLAPStrFromCell(
                                                UIBuildMemPool(),
                                                &TableCtx,
                                                Index,
                                                (u64) Row,
                                                Sample
                                            )
                                        ),
                                        Index + 1
                                    );
                                }
                            }
                        }
                    }
 
                    UISignal RowSig = UITableVectorEnd();
 
                    if (UI_PRESSED(RowSig)) {
                        Params->Cursor->Y = Row + 1;
                        *Params->Mark = *Params->Cursor;
                    }
                }
            }
        }
    }
 
    ReleaseScratch(Scratch);
}

INTERNAL void
FLAPFlagList(
    FLAPFlagListParams* Params,
    r2f32 Rect
) {
    FLVProgram const* Program = Params->Program;
    FLVRenderItem* Items = Params->Items;
    u64 LineCount = 0;
 
    for (u32 Index = 0; Index < Program->EmitCount; ++Index) {
        if (Program->Emits[Index].DisplayKind == TOKEN_KIND_FLAG) {
            LineCount += Items[Index].IsMissing
                ? 1
                : 1 + Items[Index].Flag.Count;
        }
    }
 
    UIScrollListParameters ListParams = {};
    UIScrollListSignal ListSig = {};
    r1i64 Visible = {};
 
    ListParams.Kind = UI_SCROLL_LIST_KIND_ALL;
    ListParams.DimensionsPX = Length(Rect);
    ListParams.RowHeightPX = UIHeadPXHeight();
    ListParams.CursorRange.Max.Y = (i64) LineCount;
    ListParams.ItemRange = Rng((i64) 0, (i64) LineCount);
    ListParams.CursorMinIsEmptySelection[AXIS_2D_Y] = TRUE;
 
    UIScrollList(
        &ListParams,
        Params->Scroll,
        Params->Cursor,
        Params->Mark,
        &Visible,
        &ListSig
    ) {
        for (
            i64 Row = Visible.Min;
            Row <= Visible.Max && Row < (i64) LineCount;
            ++Row
        ) {
            UIWidthFill() {
                UILabel(
                    FLAPFlagLine(
                        UIBuildMemPool(),
                        Program,
                        Items,
                        (u64) Row
                    )
                );
            }
        }
    }
}

INTERNAL void
FLAPErrorList(FLAPErrorListParams *Params, r2f32 Rect)
{
    UIScrollListParameters ListParams = {};
    UIScrollListSignal ListSig = {};
    r1i64 Visible = {};

    ListParams.Kind = UI_SCROLL_LIST_KIND_ALL;
    ListParams.DimensionsPX = Length(Rect);
    ListParams.RowHeightPX = UIHeadPXHeight();
    ListParams.CursorRange.Max.Y = (i64) Params->Errors->Count;
    ListParams.ItemRange = Rng((i64) 0, (i64) Params->Errors->Count);
    ListParams.CursorMinIsEmptySelection[AXIS_2D_Y] = TRUE;

    UIScrollList(
        &ListParams,
        Params->Scroll,
        Params->Cursor,
        Params->Mark,
        &Visible,
        &ListSig
    ) {
        Str8Node* Node = Params->Errors->Head;

        for (i64 Row = 0; Row < Visible.Min && Node; ++Row)
            Node = Node->Next;

        for (
            i64 Row = Visible.Min;
            Row < Visible.Max && Node;
            ++Row, Node = Node->Next
        ) {
            UIWidthFill() {
                UILabel(Node->String);
            }
        }
    }
}

INTERNAL FLAPGraphMetrics
FLAPGraphMetricsFromFontSize(f32 FontSize)
{
    FLAPGraphMetrics Result = {};

    Result.ColumnsPerUnit = 1;
    Result.Gutter = 9.0f * FontSize;
    Result.ValueTick = 3.0f * FontSize;
    Result.TimeTick = 12.0f * FontSize;
    Result.MarkerSpacing = 8.0f;
    Result.LineWidth = 1.5f;
    Result.SnapRadius = 6.0f;
    Result.Minimap = 3.0f * FontSize;

    return Result;
}

INTERNAL b32
FLAPGraphEmitIsCurve(
    FLVProgram const* Program, 
    FLVRenderItem* Items, 
    u32 Index
) {
    b32 Result = (
        Program->Emits[Index].DisplayKind == TOKEN_KIND_GRAPH &&
        !Items[Index].IsMissing &&
        Items[Index].Kind == FLV_RENDER_KIND_CURVE
    );

    return Result;
}

INTERNAL b32
FLAPGraphFitTimeRange(FLAPGraphParams* Params, OUT r1u64* TimeRange)
{
    b32 Result = TRUE;
    r1u64 Fit = {};

    Fit.Min = U64_MAX;

    for (u32 Index = 0; Index < Params->Program->EmitCount; ++Index) {
        if (!FLAPGraphEmitIsCurve(Params->Program, Params->Items, Index))
            continue;

        FLVRenderCurve const* Curve = &Params->Items[Index].Curve;
        FLVColumn Time = {};

        Result &= FLVColumnInit(
            Params->Acc,
            Params->Flight,
            Params->FLI,
            Curve->TimeColumn,
            0,
            &Time
        );

        if (Time.IsMissing)
            continue;

        for (
            u64 RangeIndex = 0; 
            RangeIndex < Curve->RangesCount; 
            ++RangeIndex
        ) {
            r1u64 Range = Curve->Ranges[RangeIndex];

            if (Range.Min == Range.Max)
                continue;

            Fit.Min = MIN(Fit.Min, FLVTimeAt(&Time, Range.Min));
            Fit.Max = MAX(Fit.Max, FLVTimeAt(&Time, Range.Max - 1) + 1);
        }
    }

    if (Result && Fit.Min < Fit.Max)
        *TimeRange = Fit;

    return Result;
}

INTERNAL f32
FLAPGraphTickStep(f32 Range, u32 MaxTicks)
{
    f32 Raw = Range / (f32) MaxTicks;
    f32 Magnitude = PowF32(10.0f, FloorF32(Log10F32(Raw)));
    f32 Norm = Raw / Magnitude;
    f32 Result = Magnitude * (
        (Norm <= 1.0f) 
            ? 1.0f 
            : (Norm <= 2.0f) 
                ? 2.0f 
                : (Norm <= 5.0f) 
                    ? 5.0f 
                    : 10.0f
    );

    return Result;
}

// NOTE(nc): the step of the time labels. a time of day is read in the steps
//         : of a clock, and not in 1, 2 and 5. below one second the steps
//         : are the same, and above one day the step is whole days.
INTERNAL u64
FLAPGraphTickUSecs(u64 SpanUSecs, u32 MaxTicks, b32 IsClock)
{
    readonly local u32 ClockSecs[] = {
           1,    2,     5,    10,    15,    30,
          60,  120,   300,   600,   900,  1800,
        3600, 7200, 10800, 21600, 43200, 86400
    };
    f32 TickStep = FLAPGraphTickStep(
        (f32) SpanUSecs / 1000000.0f,
        MaxTicks
    );
    u64 Result = MAX((u64) (TickStep * 1000000.0f + 0.5f), (u64) 1);

    if (IsClock && Result > MILLION(1)) {
        u64 Raw = SpanUSecs / MaxTicks;
        u64 Day = FLAP_DAY_SECS * MILLION(1);

        Result = ((Raw + Day - 1) / Day) * Day;

        for (u32 Index = 0; Index < ARRAY_COUNT(ClockSecs); ++Index) {
            u64 ClockUSecs = (u64) ClockSecs[Index] * MILLION(1);

            if (Raw <= ClockUSecs) {
                Result = ClockUSecs;
                break;
            }
        }
    }

    return Result;
}

// NOTE(nc): a time of the flight as text. with a clock it is the local
//         : time of day, and the date too if HasDate. with no clock it is
//         : the seconds from the start of the file. Digits is the digits
//         : after the second: 0, 3 or 6.
INTERNAL Str8
FLAPGraphStrFromTime(
    Arena* MemPool,
    FLAPGraphClock const* Clock,
    u64 TimeUSecs,
    u32 Digits,
    b32 HasDate
) {
    Str8 Result = {};

    if (!Clock || !Clock->Has) {
        u64 Secs = TimeUSecs / MILLION(1);
        u64 USecs = TimeUSecs % MILLION(1);

        Result = (Digits == 6)
            ? ArenaPushStrFmt(MemPool, "%llu.%06llus", Secs, USecs)
            : (Digits == 3)
                ? ArenaPushStrFmt(
                    MemPool,
                    "%llu.%03llus",
                    Secs,
                    USecs / THOUSAND(1)
                )
                : ArenaPushStrFmt(MemPool, "%llus", Secs);
    } else {
        u64 LocalUSecs = Clock->LocalStartUSecs + TimeUSecs;
        u64 Secs = LocalUSecs / MILLION(1);
        u64 USecs = LocalUSecs % MILLION(1);
        u64 DaySecs = Secs % FLAP_DAY_SECS;
        Str8 Date = ""_s8;
        Str8 Fraction = ""_s8;

        // NOTE(nc): the days from 1970 as a date. the year starts in March
        //         : here, so the leap day is its last day.
        if (HasDate) {
            u64 Days = Secs / FLAP_DAY_SECS + 719468;
            u64 Era = Days / 146097;
            u64 DayOfEra = Days % 146097;
            u64 YearOfEra = (
                DayOfEra - DayOfEra / 1460 + DayOfEra / 36524 - DayOfEra / 146096
            ) / 365;
            u64 DayOfYear = DayOfEra - (
                365 * YearOfEra + YearOfEra / 4 - YearOfEra / 100
            );
            u64 MonthIndex = (5 * DayOfYear + 2) / 153;
            u64 Day = DayOfYear - (153 * MonthIndex + 2) / 5 + 1;
            u64 Month = (MonthIndex < 10)
                ? MonthIndex + 3
                : MonthIndex - 9;
            u64 Year = YearOfEra + Era * 400 + ((Month <= 2) ? 1 : 0);

            Date = ArenaPushStrFmt(
                MemPool,
                "%04llu-%02llu-%02llu ",
                Year,
                Month,
                Day
            );
        }

        if (Digits == 6) {
            Fraction = ArenaPushStrFmt(MemPool, ".%06llu", USecs);
        } else if (Digits == 3) {
            Fraction = ArenaPushStrFmt(
                MemPool,
                ".%03llu",
                USecs / THOUSAND(1)
            );
        }

        Result = ArenaPushStrFmt(
            MemPool,
            "%S%02llu:%02llu:%02llu%S",
            PRINT_STR(Date),
            DaySecs / 3600,
            (DaySecs / 60) % 60,
            DaySecs % 60,
            PRINT_STR(Fraction)
        );
    }

    return Result;
}

INTERNAL void
FLAPGraphText(
    UIBox* Box, 
    v2f32 Position, 
    Str8 String, 
    v4f32 Colour
) {
    FontMetrics Metrics = FontMetricsFromTagSize(Box->Font, Box->FontSize);

    // NOTE(nc): the draw call takes the baseline, see UIBoxTextPosition
    Position.Y = FloorF32(Position.Y + Metrics.Ascent);
    NC_DrawText(
        Box->Font,
        Box->FontSize,
        0.0f,
        Box->TabSize,
        Box->TextRasterKind,
        Position,
        Colour,
        String
    );
}

INTERNAL Str8
FLAPGraphCursorValue(
    Arena* MemPool,
    FLAPGraphColumns const* Columns,
    r1u64 View,
    r1u64 TimeRangeUSecs
) {
    Str8 Result = " -"_s8;
    r1u64 Span = FLAPGraphViewFromTimeRange(
        TimeRangeUSecs,
        Columns->Key.StepUSecs
    );
    f32 Low = 0.0f;
    f32 High = 0.0f;
    b32 HasValue = FALSE;

    Span.Min = MAX(Span.Min, View.Min);
    Span.Max = MIN(Span.Max, View.Max);

    for (u64 Column = Span.Min; Column < Span.Max; ++Column) {
        u64 Slot = 0;

        if (!FLAPGraphColumnHas(Columns, Column, &Slot))
            continue;

        if (!HasValue) {
            Low = Columns->Min[Slot];
            High = Columns->Max[Slot];
            HasValue = TRUE;
        } else {
            Low = MIN(Low, Columns->Min[Slot]);
            High = MAX(High, Columns->Max[Slot]);
        }
    }

    if (HasValue) {
        Result = (Low == High)
            ? ArenaPushStrFmt(MemPool, "=%3.3f", (f64) Low)
            : ArenaPushStrFmt(
                MemPool,
                "=[%3.3f..%3.3f]",
                (f64) Low,
                (f64) High
            );
    }

    return Result;
}

INTERNAL void
FLAPGraphEdgeLine(
    r2f32 Rect,
    f64 FromX,
    f64 FromY,
    f64 ToX,
    f64 ToY,
    v4f32 Colour,
    f32 LineWidth
) {
    f64 Pad = 2.0 * (f64) LineWidth;
    f64 DeltaX = ToX - FromX;
    f64 DeltaY = ToY - FromY;
    f64 Deltas[4] = {
        -DeltaX,
        DeltaX,
        -DeltaY,
        DeltaY
    };
    f64 Distances[4] = {
        FromX - ((f64) Rect.X0 - Pad),
        ((f64) Rect.X1 + Pad) - FromX,
        FromY - ((f64) Rect.Y0 - Pad),
        ((f64) Rect.Y1 + Pad) - FromY
    };
    f64 Enter = 0.0;
    f64 Leave = 1.0;
    b32 IsVisible = TRUE;

    for (u32 Index = 0; Index < 4 && IsVisible; ++Index) {
        if (Deltas[Index] == 0.0) {
            IsVisible = (Distances[Index] >= 0.0);
        } else if (Deltas[Index] < 0.0) {
            Enter = MAX(Enter, Distances[Index] / Deltas[Index]);
        } else {
            Leave = MIN(Leave, Distances[Index] / Deltas[Index]);
        }
    }

    if (IsVisible && Enter < Leave) {
        DrawCADLine(
            Vec(
                (f32) (FromX + Enter * DeltaX),
                (f32) (FromY + Enter * DeltaY)
            ),
            Vec(
                (f32) (FromX + Leave * DeltaX),
                (f32) (FromY + Leave * DeltaY)
            ),
            Colour,
            LineWidth,
            1.0f,
            CAD_LINE_CAP_ROUND
        );
    }
}

// NOTE(nc): the name of a mode, from the table of the vehicle. a mode that
//         : the table does not have is its number.
INTERNAL Str8
FLAPGraphModeName(
    Arena* MemPool,
    FLVModeName const* Names,
    u32 Count,
    u32 Number
) {
    Str8 Result = {};

    for (u32 Index = 0; Index < Count; ++Index) {
        if (Names[Index].Number == Number) {
            Result = Names[Index].Name;
            break;
        }
    }

    if (!Result.Size)
        Result = ArenaPushStrFmt(MemPool, "MODE %u", Number);

    return Result;
}

INTERNAL
UI_BOX_CUSTOM_DRAW(FLAPGraphDraw)
{
    FLAPGraphDrawData* Data = (FLAPGraphDrawData*) UserData;
    FLAPGraphMetrics Metrics = Data->Metrics;
    u32 Width = Data->Width;
    u32 Height = Data->Height;

    if (!Width || !Height)
        return;

    TempArena Scratch = GetScratch(NULL, 0);
    i32 X0 = (i32) FloorF32(Box->Rect.X0) + (i32) Data->PlotX;
    i32 Y0 = (i32) FloorF32(Box->Rect.Y0) + (i32) Data->PlotY;
    r2f32 PlotRect = Rng(
        (f32) X0,
        (f32) Y0,
        (f32) (X0 + (i32) Width),
        (f32) (Y0 + (i32) Height)
    );
    r1u64 View = Data->View;
    r1u64 ViewRange = Data->TimeRangeUSecs;
    u64 Step = Data->StepUSecs;
    // NOTE(nc): the columns are at fixed steps of time and the view is not.
    //         : so a position on the screen comes from a time, and not from
    //         : the index of a column.
    f64 PXPerUSec = Step
        ? (f64) Width / (f64) Length(ViewRange)
        : 0.0;
    f32 ColumnPX = (f32) ((f64) Step * PXPerUSec);

    if (Step) {
        // NOTE(nc): the modes, as a tint of the plot from one mode record
        //         : to the next. the last mode goes to the end. the name
        //         : is at the top of a span that has the room for it.
        for (u32 Index = 0; Index < Data->ModesCount; ++Index) {
            u64 Start = Data->Modes[Index].TimeUSecs;
            u64 End = (Index + 1 < Data->ModesCount)
                ? Data->Modes[Index + 1].TimeUSecs
                : U64_MAX;

            if (End <= ViewRange.Min)
                continue;

            if (Start >= ViewRange.Max)
                break;

            f32 SpanX0 = FloorF32(
                PlotRect.X0 + (f32) (
                    (f64) (MAX(Start, ViewRange.Min) - ViewRange.Min) * PXPerUSec
                )
            );
            f32 SpanX1 = FloorF32(
                PlotRect.X0 + (f32) (
                    (f64) (MIN(End, ViewRange.Max) - ViewRange.Min) * PXPerUSec
                )
            );
            u32 Number = Data->Modes[Index].Mode;
            v4f32 Colour = Data->ModeColours[Number % FLAP_GRAPH_MODE_COLOURS];
            Str8 Name = FLAPGraphModeName(
                Scratch.MemPool,
                Data->ModeNames,
                Data->ModeNamesCount,
                Number
            );
            f32 NameWidth = FontDimensionsFromTagSizeString(
                Box->Font,
                Box->FontSize,
                0.0f,
                Box->TabSize,
                Name
            ).X;

            Colour.W *= 0.15f;
            DrawRect(
                Rng(SpanX0, PlotRect.Y0, SpanX1, PlotRect.Y1),
                Colour,
                0.0f,
                0.0f,
                0.0f
            );

            if (SpanX1 - SpanX0 >= NameWidth + Box->FontSize) {
                FLAPGraphText(
                    Box,
                    Vec(SpanX0 + 0.5f * Box->FontSize, PlotRect.Y0),
                    Name,
                    Data->LabelColour
                );
            }
        }

        // NOTE(nc): the flags, one lane for each at the bottom edge of the
        //         : plot, the first flag at the bottom. the ranges of a
        //         : flag that share a pixel are one rect.
        if (Data->FlagsCount) {
            u8* Bands = ArenaPushArray(Scratch.MemPool, u8, Width);
            f32 Lane = MAX(3.0f, FloorF32(0.4f * Box->FontSize));

            for (u32 FlagIndex = 0; FlagIndex < Data->FlagsCount; ++FlagIndex) {
                FLVRenderFlag const* Flag = Data->Flags[FlagIndex];
                f32 LaneY1 = PlotRect.Y1 - (f32) FlagIndex * Lane;

                // NOTE(nc): no room for more lanes
                if (LaneY1 - Lane < PlotRect.Y0)
                    break;

                MemZero(Bands, Width);

                for (u64 Index = 0; Index < Flag->Count; ++Index) {
                    r1u64 Range = Flag->Ranges[Index];

                    if (Range.Max <= ViewRange.Min || Range.Min >= ViewRange.Max)
                        continue;

                    u32 First = MIN(
                        (u32) (
                            (f64) (
                                MAX(Range.Min, ViewRange.Min) - ViewRange.Min
                            ) * PXPerUSec
                        ),
                        Width - 1
                    );
                    u32 Last = MIN(
                        (u32) (
                            (f64) (
                                MIN(Range.Max, ViewRange.Max) - 1 - ViewRange.Min
                            ) * PXPerUSec
                        ),
                        Width - 1
                    );

                    for (u32 Pixel = First; Pixel <= Last; ++Pixel)
                        Bands[Pixel] = 1;
                }

                for (u32 Pixel = 0; Pixel < Width; ) {
                    if (!Bands[Pixel]) {
                        ++Pixel;
                        continue;
                    }

                    u32 Start = Pixel;

                    for ( ; Pixel < Width && Bands[Pixel]; ++Pixel);

                    DrawRect(
                        Rng(
                            PlotRect.X0 + (f32) Start,
                            LaneY1 - Lane,
                            PlotRect.X0 + (f32) Pixel,
                            LaneY1
                        ),
                        Data->FlagColours[FlagIndex],
                        0.0f,
                        0.0f,
                        0.0f
                    );
                }
            }
        }

        if (
            Data->HasCursor &&
            ViewRange.Min <= Data->CursorTimeUSecs &&
            Data->CursorTimeUSecs < ViewRange.Max
        ) {
            f32 X = FloorF32(
                PlotRect.X0 + (f32) (
                    (f64) (Data->CursorTimeUSecs - ViewRange.Min) * PXPerUSec
                )
            );

            DrawRect(
                Rng(X, PlotRect.Y0, X + 1.0f, PlotRect.Y1),
                Data->CursorColour,
                0.0f,
                0.0f,
                0.0f
            );
        }
    }

    r2f32 Clip = Intersect(DTopClip(), PlotRect);

    DClipScope(Clip) {
        RendPassCADParams* Pass = DrawCADBegin(
            PlotRect, 
            Mat3(1.0f), 
            Mat3(1.0f)
        );

        Pass->Clip = Clip;

        PROFILE(FLAPGraphDrawCurvesTimer) {
            // NOTE(nc): one column more at each side, so that a curve does
            //         : not stop at the edge of the plot. the Clip cuts it.
            u64 ColumnMin = View.Min - (
                View.Min
                    ? 1
                    : 0
            );
            u64 ColumnMax = View.Max + 1;
            f32 HalfColumnPX = 0.5f * ColumnPX;

            for (
                u32 CurveIndex = 0;
                Step && CurveIndex < Data->CurvesCount;
                ++CurveIndex
            ) {
                FLAPGraphDrawCurve* Curve = &Data->Curves[CurveIndex];
                FLAPGraphColumns const* Columns = Curve->Columns;
                r1f32 Range = Data->ValueRanges[Curve->Axis];
                f32 Scale = (f32) (Height - 1) / (Range.Max - Range.Min);
                b32 HasMarkers = (
                    Curve->Samples &&
                    (f32) Curve->Samples * Metrics.MarkerSpacing <= (f32) Width
                );
                b32 HasPrev = FALSE;
                b32 HasPoint = FALSE;
                b32 IsPrevEdge = FALSE;
                b32 IsPrevRange = FALSE;
                u64 PrevColumn = 0;
                f64 PrevEdgeX = 0.0;
                f32 PrevX = 0.0f;
                f32 PrevTop = 0.0f;
                f32 PrevBottom = 0.0f;
                f32 PrevLast = 0.0f;

                // NOTE(nc): the sample before the columns is the column
                //         : before the first one. its X is far from the
                //         : plot, so it stays in f64.
                if (Columns->Lead.Has) {
                    PrevEdgeX = (f64) PlotRect.X0 + (
                        (f64) Columns->Lead.TimeUSecs - (f64) ViewRange.Min
                    ) * PXPerUSec;
                    PrevLast = PlotRect.Y0 + (Range.Max - Columns->Lead.Value) * Scale + 0.5f;
                    HasPrev = TRUE;
                    IsPrevEdge = TRUE;
                }

                for (u64 Column = ColumnMin; Column < ColumnMax; ++Column) {
                    u64 Slot = 0;

                    if (!FLAPGraphColumnHas(Columns, Column, &Slot))
                        continue;

                    // NOTE(nc): one sample is a point at its time. more
                    //         : than one sample is a range, at the middle
                    //         : of the column.
                    b32 IsRange = (Columns->Samples[Slot] != 1);
                    f32 Offset = IsRange
                        ? 0.5f
                        : Columns->Phase[Slot];
                    f32 X = PlotRect.X0 + (f32) (
                        (f64) (
                            (i64) (Column * Step) - (i64) ViewRange.Min
                        ) * PXPerUSec
                    ) + Offset * ColumnPX;
                    f32 Top = PlotRect.Y0 + (Range.Max - Columns->Max[Slot]) * Scale + 0.5f;
                    f32 Bottom = PlotRect.Y0 + (Range.Max - Columns->Min[Slot]) * Scale + 0.5f;
                    f32 First = PlotRect.Y0 + (Range.Max - Columns->First[Slot]) * Scale + 0.5f;
                    f32 Last = PlotRect.Y0 + (Range.Max - Columns->Last[Slot]) * Scale + 0.5f;
                    b32 IsJoined = (
                        HasPrev &&
                        (Columns->Join[Slot >> 6] & (1ULL << (Slot & 63))) != 0
                    );
                    // NOTE(nc): two columns that touch share one band from
                    //         : range to range. all other joins are a line
                    //         : from the last sample to the first sample.
                    b32 IsAdjacent = (
                        IsJoined &&
                        !IsPrevEdge &&
                        Column == PrevColumn + 1
                    );

                    // NOTE(nc): the column before was one value and no line
                    //         : comes to it or goes from it. a disc, or it
                    //         : draws nothing.
                    if (HasPoint && !IsJoined) {
                        DrawCADDisc(
                            Vec(PrevX, PrevTop),
                            0.5f * Metrics.LineWidth,
                            Curve->Colour,
                            1.0f
                        );
                    }

                    // NOTE(nc): a range with no band at one side has a cap
                    //         : there, so that it is one column wide. the
                    //         : cap ends on a pixel edge.
                    if (IsPrevRange && !IsAdjacent) {
                        DrawCADRibbon(
                            Vec(PrevX, PrevTop),
                            Vec(PrevX + HalfColumnPX, PrevTop),
                            PrevBottom,
                            PrevBottom,
                            Curve->Colour,
                            Metrics.LineWidth,
                            1.0f
                        );
                    }

                    if (IsAdjacent) {
                        DrawCADRibbon(
                            Vec(PrevX, PrevTop),
                            Vec(X, Top),
                            PrevBottom,
                            Bottom,
                            Curve->Colour,
                            Metrics.LineWidth,
                            1.0f
                        );
                    } else if (IsJoined && IsPrevEdge) {
                        FLAPGraphEdgeLine(
                            PlotRect,
                            PrevEdgeX,
                            (f64) PrevLast,
                            (f64) X,
                            (f64) First,
                            Curve->Colour,
                            Metrics.LineWidth
                        );
                    } else if (IsJoined) {
                        DrawCADRibbon(
                            Vec(PrevX, PrevLast),
                            Vec(X, First),
                            PrevLast,
                            First,
                            Curve->Colour,
                            Metrics.LineWidth,
                            1.0f
                        );
                    }

                    if (IsRange && !IsAdjacent) {
                        DrawCADRibbon(
                            Vec(X - HalfColumnPX, Top),
                            Vec(X, Top),
                            Bottom,
                            Bottom,
                            Curve->Colour,
                            Metrics.LineWidth,
                            1.0f
                        );
                    }

                    // NOTE(nc): the arcs draw before the ribbons and the
                    //         : lines in the pass, so a marker is below them.
                    if (HasMarkers) {
                        DrawCADDisc(
                            Vec(X, Top),
                            2.0f * Metrics.LineWidth,
                            Curve->Colour,
                            1.0f
                        );
                    }

                    HasPoint = (!IsRange && !IsJoined && !HasMarkers);
                    HasPrev = TRUE;
                    IsPrevEdge = FALSE;
                    IsPrevRange = IsRange;
                    PrevColumn = Column;
                    PrevX = X;
                    PrevTop = Top;
                    PrevBottom = Bottom;
                    PrevLast = Last;
                }

                if (IsPrevRange) {
                    DrawCADRibbon(
                        Vec(PrevX, PrevTop),
                        Vec(PrevX + HalfColumnPX, PrevTop),
                        PrevBottom,
                        PrevBottom,
                        Curve->Colour,
                        Metrics.LineWidth,
                        1.0f
                    );
                }

                // NOTE(nc): the sample after the columns. it joins the last
                //         : column, or Lead when no column has a value.
                if (HasPrev && Columns->Trail.Has) {
                    f64 TrailX = (f64) PlotRect.X0 + (
                        (f64) Columns->Trail.TimeUSecs - (f64) ViewRange.Min
                    ) * PXPerUSec;
                    f32 TrailY = (
                        PlotRect.Y0 + 
                        (Range.Max - Columns->Trail.Value) * Scale + 
                        0.5f
                    );

                    FLAPGraphEdgeLine(
                        PlotRect,
                        IsPrevEdge
                            ? PrevEdgeX
                            : (f64) PrevX,
                        (f64) PrevLast,
                        TrailX,
                        (f64) TrailY,
                        Curve->Colour,
                        Metrics.LineWidth
                    );
                } else if (HasPoint) {
                    DrawCADDisc(
                        Vec(PrevX, PrevTop),
                        0.5f * Metrics.LineWidth,
                        Curve->Colour,
                        1.0f
                    );
                }

                // NOTE(nc): the sample that the hover box gives the value
                //         : of. the arcs draw before the ribbons, so the
                //         : ring is wider than a marker.
                if (Curve->HasHoverPoint) {
                    f32 HoverX = PlotRect.X0 + (f32) (
                        (
                            (f64) Curve->HoverTimeUSecs - (f64) ViewRange.Min
                        ) * PXPerUSec
                    );
                    f32 HoverY = PlotRect.Y0 + (Range.Max - Curve->HoverValue) * Scale + 0.5f;

                    DrawCADCircle(
                        Vec(HoverX, HoverY),
                        3.0f * Metrics.LineWidth,
                        Curve->Colour,
                        Metrics.LineWidth,
                        1.0f
                    );
                }
            }
        }
    }

    // NOTE(nc): legend
    f32 LegendX = Box->Rect.X0 + Box->FontSize;

    for (
        u32 Index = 0;
        Index < Data->LabelsCount && LegendX < Box->Rect.X1;
        ++Index
    ) {
        FLAPGraphDrawLabel* Label = &Data->Labels[Index];

        FLAPGraphText(
            Box,
            Vec(LegendX, Box->Rect.Y0),
            Label->String,
            Label->Colour
        );
        LegendX += FontDimensionsFromTagSizeString(
            Box->Font,
            Box->FontSize,
            0.0f,
            Box->TabSize,
            Label->String
        ).X + 2.0f * Box->FontSize;
    }

    // NOTE(nc): value labels, left axis then right axis
    for (u32 Axis = 0; Axis < 2; ++Axis) {
        if (!Data->HasRange[Axis])
            continue;

        r1f32 Range = Data->ValueRanges[Axis];
        f32 Scale = (f32) (Height - 1) / (Range.Max - Range.Min);
        f32 ValueStep = FLAPGraphTickStep(
            Range.Max - Range.Min,
            MAX((u32) ((f32) Height / MAX(Metrics.ValueTick, 1.0f)), (u32) 2)
        );
        f32 First = CeilF32(Range.Min / ValueStep);
        char* Fmt = (char*) (
            (ValueStep >= 0.95f) 
                ? "%.0f" 
                : (ValueStep >= 0.095f) 
                    ? "%.1f" 
                    : (ValueStep >= 0.0095f) 
                        ? "%.2f" 
                        : "%.3f"
        );

        for (u32 Tick = 0; ; ++Tick) {
            f32 Value = (First + (f32) Tick) * ValueStep;

            if (Value > Range.Max)
                break;

            Str8 String = ArenaPushStrFmt(Scratch.MemPool, Fmt, (f64) Value);
            f32 TextWidth = FontDimensionsFromTagSizeString(
                Box->Font,
                Box->FontSize,
                0.0f,
                Box->TabSize,
                String
            ).X;
            f32 X = Axis
                ? (f32) PlotRect.X1 + Box->FontSize
                : (f32) PlotRect.X0 - Box->FontSize - TextWidth;
            f32 Y = PlotRect.Y0 + (Range.Max - Value) * Scale - 0.5f * Box->FontSize;

            FLAPGraphText(
                Box,
                Vec(X, Y),
                String,
                Data->LabelColour
            );
        }
    }

    // NOTE(nc): time labels. the ticks are at whole steps from an origin:
    //         : the start of the file, the local midnight, or the cursor.
    //         : from the cursor, the labels are the time before and after
    //         : it, and its own time is at its line. with no cursor, that
    //         : axis has no labels.
    u64 Span = Length(ViewRange);
    FLAPGraphClock const* Clock = Data->Clock;
    b32 IsLocal = (Clock && Clock->Has);
    b32 IsFromCursor = Data->IsTimeFromCursor;

    if (Step && Span && (!IsFromCursor || Data->HasCursor)) {
        u32 MaxTicks = MAX(
            (u32) ((f32) Width / MAX(Metrics.TimeTick, 1.0f)),
            (u32) 1
        );
        u64 TickUSecs = FLAPGraphTickUSecs(
            Span,
            MaxTicks,
            IsLocal && !IsFromCursor
        );
        u32 Digits = (TickUSecs >= MILLION(1))
            ? 0
            : (TickUSecs >= THOUSAND(1))
                ? 3
                : 6;
        u64 CursorUSecs = Data->CursorTimeUSecs;
        u64 Origin = 0;

        if (IsFromCursor)
            Origin = CursorUSecs;
        else if (IsLocal)
            Origin = TickUSecs - (Clock->LocalStartUSecs % TickUSecs);

        // NOTE(nc): the first tick at or after the start of the view
        u64 Phase = Origin % TickUSecs;
        u64 First = ViewRange.Min - (ViewRange.Min % TickUSecs) + Phase;

        if (First < ViewRange.Min)
            First += TickUSecs;

        for (
            u64 TimeUSecs = First;
            TimeUSecs < ViewRange.Max;
            TimeUSecs += TickUSecs
        ) {
            f32 X = FloorF32(
                PlotRect.X0 + (f32) (
                    (f64) (TimeUSecs - ViewRange.Min) * PXPerUSec
                )
            );
            Str8 String = {};

            if (!IsFromCursor) {
                String = FLAPGraphStrFromTime(
                    Scratch.MemPool,
                    Clock,
                    TimeUSecs,
                    Digits,
                    FALSE
                );
            } else if (TimeUSecs == CursorUSecs) {
                // NOTE(nc): the time of the cursor, with its middle at the
                //         : line of the cursor. it stays in the plot.
                String = FLAPGraphStrFromTime(
                    Scratch.MemPool,
                    Clock,
                    TimeUSecs,
                    MAX(Digits, (u32) 3),
                    FALSE
                );

                f32 TextWidth = FontDimensionsFromTagSizeString(
                    Box->Font,
                    Box->FontSize,
                    0.0f,
                    Box->TabSize,
                    String
                ).X;

                X = CLAMP(
                    PlotRect.X0,
                    FloorF32(X - 0.5f * TextWidth),
                    MAX(PlotRect.X0, PlotRect.X1 - TextWidth)
                );
            } else {
                b32 IsAfter = (TimeUSecs > CursorUSecs);
                u64 Offset = IsAfter
                    ? TimeUSecs - CursorUSecs
                    : CursorUSecs - TimeUSecs;
                char* Fmt = (char*) (IsAfter ? "+%S" : "-%S");

                String = ArenaPushStrFmt(
                    Scratch.MemPool,
                    Fmt,
                    PRINT_STR(
                        FLAPGraphStrFromTime(
                            Scratch.MemPool,
                            NULL,
                            Offset,
                            Digits,
                            FALSE
                        )
                    )
                );
            }

            FLAPGraphText(
                Box,
                Vec((f32) X, PlotRect.Y1),
                String,
                Data->LabelColour
            );
        }
    }

    ReleaseScratch(Scratch);
}

// TODO(nc): check if this is the same on linux
// NOTE(nc): Windows sends 120 for one notch, on all machines. the UI
//         : divides it by 30, so one notch is 4 here. each of the 4 is the
//         : 4th root of 0.8 or of 1.25, so one notch is a zoom of 0.8 or of
//         : 1.25. a wheel with a finer step gets a finer zoom.
INTERNAL f64
FLAPGraphZoomFactor(i32 Notches)
{
    f64 Result = 1.0;

    for (
        i32 Notch = Notches;
        Notch;
        Notch += (Notch < 0)
            ? 1
            : -1
    ) {
        Result *= (Notch < 0)
            ? 0.9457416090960342
            : 1.0573712634405641;
    }

    return Result;
}

INTERNAL u32
FLAPGraphMinimapPixel(v4f32 Colour)
{
    u32 Red = (u32) (CLAMP(0.0f, Colour.X, 1.0f) * 255.0f + 0.5f);
    u32 Green = (u32) (CLAMP(0.0f, Colour.Y, 1.0f) * 255.0f + 0.5f);
    u32 Blue = (u32) (CLAMP(0.0f, Colour.Z, 1.0f) * 255.0f + 0.5f);
    u32 Result = Red | (Green << 8) | (Blue << 16) | 0xFF000000;

    return Result;
}

INTERNAL void
FLAPGraphMinimapSpan(u32* Pixels, i32 X, i32 FromY, i32 ToY, u32 Pixel)
{
    i32 Top = CLAMP(0, MIN(FromY, ToY), FLAP_GRAPH_MINIMAP_HEIGHT - 1);
    i32 Bottom = CLAMP(0, MAX(FromY, ToY), FLAP_GRAPH_MINIMAP_HEIGHT - 1);

    for (i32 Y = Top; Y <= Bottom; ++Y)
        Pixels[Y * FLAP_GRAPH_MINIMAP_WIDTH + X] = Pixel;
}

// NOTE(nc): the texture of a minimap is an object of the cache. the key
//         : has all that the picture comes from, so the cache can make it
//         : again after it evicts it. this runs on an async lane.
OCObject
FLAPGraphMinimapObjectCreate(
    Str8 Key,
    b32* CancelSignal,
    OUT b32* Retry,
    OUT u64* Generation
) {
    UNUSED(CancelSignal);
    UNUSED(Generation);

    OCObject Result = {};
    TempArena Scratch = GetScratch(NULL, 0);
    Access* Acc = AccessOpen();
    FLAPGraphMinimapKey Head = {};
    u64 Offset = 0;
    u64 KeysCount = 0;
    u64 SearchPathsCount = 0;
    Str8 Path = {};

    MemCpy(&Head, Key.Str + Offset, sizeof(Head));
    Offset += sizeof(Head);

    u32* Colours = ArenaPushArray(
        Scratch.MemPool, 
        u32, 
        Head.ColoursCount
    );

    MemCpy(Colours, Key.Str + Offset, sizeof(u32) * Head.ColoursCount);
    Offset += sizeof(u32) * Head.ColoursCount;

    u32* FlagColours = ArenaPushArray(
        Scratch.MemPool,
        u32,
        Head.FlagColoursCount
    );

    MemCpy(
        FlagColours, 
        Key.Str + Offset, 
        sizeof(u32) * Head.FlagColoursCount
    );
    Offset += sizeof(u32) * Head.FlagColoursCount;

    u32* ModeColours = ArenaPushArray(
        Scratch.MemPool,
        u32,
        Head.ModeColoursCount
    );

    MemCpy(
        ModeColours, 
        Key.Str + Offset, 
        sizeof(u32) * Head.ModeColoursCount
    );
    Offset += sizeof(u32) * Head.ModeColoursCount;

    // NOTE(nc): the source of the view is after all the colours
    MemCpy(&KeysCount, Key.Str + Offset, sizeof(KeysCount));
    Offset += sizeof(KeysCount);
    FLIKey* Keys = ArenaPushArray(
        Scratch.MemPool, 
        FLIKey, 
        KeysCount
    );

    MemCpy(Keys, Key.Str + Offset, sizeof(FLIKey) * KeysCount);
    Offset += sizeof(FLIKey) * KeysCount;
    MemCpy(&Path.Size, Key.Str + Offset, sizeof(Path.Size));
    Offset += sizeof(Path.Size);
    Path.Str = Key.Str + Offset;
    Offset += Path.Size;
    MemCpy(
        &SearchPathsCount, 
        Key.Str + Offset, 
        sizeof(SearchPathsCount)
    );
    Offset += sizeof(SearchPathsCount);

    Str8* SearchPaths = ArenaPushArray(
        Scratch.MemPool, 
        Str8, 
        SearchPathsCount
    );

    for (u64 Index = 0; Index < SearchPathsCount; ++Index) {
        MemCpy(
            &SearchPaths[Index].Size, 
            Key.Str + Offset, 
            sizeof(u64)
        );
        Offset += sizeof(u64);
        SearchPaths[Index].Str = Key.Str + Offset;
        Offset += SearchPaths[Index].Size;
    }

    FLVUnit* Unit = FLVUnitFromPath(
        Acc,
        Path,
        Keys,
        KeysCount,
        SearchPaths,
        SearchPathsCount,
        0
    );
    FLIParsed* FLI = FLIParsedFromKey(Acc, Head.Flight, FALSE, 0);
    FLVRenderItem* Items = NULL;
    b32 IsReady = (Unit && FLI != EMPTY_FLI_PARSED_VALUE);
    // NOTE(nc): a unit that is not the unit of the key gives no picture.
    //         : the view asks again with the key of the new unit.
    b32 IsUsable = (
        IsReady && 
        !Unit->HasError && 
        Unit->Hash == Head.UnitHash
    );

    if (IsUsable) {
        Items = FLVResultFromUnit(
            Acc,
            Unit,
            Keys,
            KeysCount,
            Head.Flight,
            Head.AnalysisIndex,
            0
        );
        IsReady = (Items != NULL);
    }

    if (IsUsable && IsReady) {
        FLVProgram const* Program = &Unit->Lowered.Program[Head.AnalysisIndex];
        r1u64 Fit = Head.TimeRangeUSecs;
        u64 Step = FLAPGraphStepFromSpan(
            Length(Fit), 
            FLAP_GRAPH_MINIMAP_WIDTH
        );
        r1u64 View = FLAPGraphViewFromTimeRange(Fit, Step);
        FLAPGraphColumns* Columns = ArenaPushArrayZero(
            Scratch.MemPool,
            FLAPGraphColumns,
            Program->EmitCount
        );
        r1f32 Ranges[2] = {};
        b32 HasRange[2] = {};

        for (
            u32 Index = 0; 
            Step && IsReady && Index < Program->EmitCount; 
            ++Index
        ) {
            if (!FLAPGraphEmitIsCurve(Program, Items, Index))
                continue;

            FLAPGraphKey ColumnsKey = {};
            u32 Axis = (Items[Index].Curve.Axis == 2);

            ColumnsKey.Curve = &Items[Index].Curve;
            ColumnsKey.UnitHash = Head.UnitHash;
            ColumnsKey.Flight = Head.Flight;
            ColumnsKey.StepUSecs = Step;
            IsReady &= FLAPGraphColumnsFromKey(
                Scratch.MemPool,
                &Columns[Index],
                &ColumnsKey,
                View,
                Acc,
                FLI
            );

            for (u64 Column = View.Min; Column < View.Max; ++Column) {
                u64 Slot = 0;

                if (!FLAPGraphColumnHas(&Columns[Index], Column, &Slot))
                    continue;

                if (!HasRange[Axis]) {
                    Ranges[Axis].Min = Columns[Index].Min[Slot];
                    Ranges[Axis].Max = Columns[Index].Max[Slot];
                    HasRange[Axis] = TRUE;
                } else {
                    Ranges[Axis].Min = MIN(
                        Ranges[Axis].Min, 
                        Columns[Index].Min[Slot]
                    );
                    Ranges[Axis].Max = MAX(
                        Ranges[Axis].Max, 
                        Columns[Index].Max[Slot]
                    );
                }
            }
        }

        if (Step && IsReady) {
            u32* Pixels = ArenaPushArrayZero(
                Scratch.MemPool,
                u32,
                FLAP_GRAPH_MINIMAP_WIDTH * FLAP_GRAPH_MINIMAP_HEIGHT
            );
            f64 TexelsPerUSec = (
                (f64) FLAP_GRAPH_MINIMAP_WIDTH / (f64) Length(Fit)
            );
            u32 CurveIndex = 0;

            // NOTE(nc): the modes, as a tint of the whole column. it has a
            //         : low alpha, and the curves draw over it.
            if (Head.ModeColoursCount) {
                u64 ModesCount = 0;
                FLIMode* ModeTable = FLITableFromName(
                    FLI,
                    Modes,
                    MODES,
                    &ModesCount
                );

                // NOTE(nc): the first element of a table of the FLI is
                //         : its null
                for (u64 Index = 1; Index < ModesCount; ++Index) {
                    u64 Start = MAX(ModeTable[Index].TimeUSecs, Fit.Min);
                    u64 End = (Index + 1 < ModesCount)
                        ? MIN(ModeTable[Index + 1].TimeUSecs, Fit.Max)
                        : Fit.Max;

                    if (Start >= End)
                        continue;

                    i32 X0 = (i32) ((f64) (Start - Fit.Min) * TexelsPerUSec);
                    i32 X1 = MIN(
                        (i32) ((f64) (End - Fit.Min) * TexelsPerUSec),
                        FLAP_GRAPH_MINIMAP_WIDTH
                    );
                    u32 Pixel = 0x40000000 | (
                        ModeColours[
                            ModeTable[Index].Mode % Head.ModeColoursCount
                        ] & 0x00FFFFFF
                    );

                    for (i32 X = X0; X < X1; ++X) {
                        FLAPGraphMinimapSpan(
                            Pixels,
                            X,
                            0,
                            FLAP_GRAPH_MINIMAP_HEIGHT - 1,
                            Pixel
                        );
                    }
                }
            }

            for (u32 Index = 0; Index < Program->EmitCount; ++Index) {
                if (!FLAPGraphEmitIsCurve(Program, Items, Index))
                    continue;

                FLAPGraphColumns* Curve = &Columns[Index];
                r1f32 Range = Ranges[Items[Index].Curve.Axis == 2];
                f32 Scale = (Range.Max > Range.Min)
                    ? (f32) (FLAP_GRAPH_MINIMAP_HEIGHT - 1) / (Range.Max - Range.Min)
                    : 0.0f;
                u32 Pixel = (CurveIndex < Head.ColoursCount)
                    ? Colours[CurveIndex]
                    : 0xFFFFFFFF;
                b32 HasPrev = FALSE;
                i32 PrevX = 0;
                i32 PrevY = 0;

                ++CurveIndex;

                for (u64 Column = View.Min; Column < View.Max; ++Column) {
                    u64 Slot = 0;

                    if (!FLAPGraphColumnHas(Curve, Column, &Slot))
                        continue;

                    // NOTE(nc): the middle of the column, as a texel
                    f64 Time = (
                        (f64) (Column * Step) + 0.5 * (f64) Step - (f64) Fit.Min
                    );
                    i32 X = CLAMP(
                        0,
                        (i32) (Time * TexelsPerUSec),
                        FLAP_GRAPH_MINIMAP_WIDTH - 1
                    );
                    i32 Top = (i32) ((Range.Max - Curve->Max[Slot]) * Scale + 0.5f);
                    i32 Bottom = (i32) ((Range.Max - Curve->Min[Slot]) * Scale + 0.5f);
                    i32 First = (i32) ((Range.Max - Curve->First[Slot]) * Scale + 0.5f);
                    i32 Last = (i32) ((Range.Max - Curve->Last[Slot]) * Scale + 0.5f);
                    b32 IsJoined = (
                        HasPrev &&
                        (Curve->Join[Slot >> 6] & (1ULL << (Slot & 63))) != 0
                    );

                    // NOTE(nc): the line from the column before, one span
                    //         : for each texel that it crosses
                    if (IsJoined) {
                        i32 LineY = PrevY;

                        for (i32 Gap = PrevX + 1; Gap <= X; ++Gap) {
                            i32 Y = PrevY + (
                                (First - PrevY) * (Gap - PrevX)
                            ) / (X - PrevX);

                            FLAPGraphMinimapSpan(Pixels, Gap, LineY, Y, Pixel);
                            LineY = Y;
                        }
                    }

                    FLAPGraphMinimapSpan(Pixels, X, Top, Bottom, Pixel);
                    HasPrev = TRUE;
                    PrevX = X;
                    PrevY = Last;
                }
            }

            // NOTE(nc): the flags, one lane for each at the bottom, in the
            //         : order of the graph. over the curves, since the
            //         : strip is small. a range is one texel at the least.
            u32 FlagIndex = 0;

            for (
                u32 Index = 0;
                Head.FlagColoursCount && Index < Program->EmitCount;
                ++Index
            ) {
                if (
                    Program->Emits[Index].DisplayKind != TOKEN_KIND_FLAG ||
                    Items[Index].IsMissing
                ) {
                    continue;
                }

                FLVRenderFlag const* Flag = &Items[Index].Flag;
                i32 Bottom = FLAP_GRAPH_MINIMAP_HEIGHT - 1 - (i32) (
                    FlagIndex * FLAP_GRAPH_MINIMAP_LANE
                );
                u32 Pixel = FlagColours[FlagIndex % Head.FlagColoursCount];

                ++FlagIndex;

                // NOTE(nc): no room for more lanes
                if (Bottom < FLAP_GRAPH_MINIMAP_LANE - 1)
                    break;

                for (
                    u64 RangeIndex = 0; 
                    RangeIndex < Flag->Count; 
                    ++RangeIndex
                ) {
                    u64 Start = MAX(Flag->Ranges[RangeIndex].Min, Fit.Min);
                    u64 End = MIN(Flag->Ranges[RangeIndex].Max, Fit.Max);

                    if (Start >= End)
                        continue;

                    i32 X0 = (i32) ((f64) (Start - Fit.Min) * TexelsPerUSec);
                    i32 X1 = MIN(
                        MAX(
                            (i32) ((f64) (End - Fit.Min) * TexelsPerUSec),
                            X0 + 1
                        ),
                        FLAP_GRAPH_MINIMAP_WIDTH
                    );

                    for (i32 X = X0; X < X1; ++X) {
                        FLAPGraphMinimapSpan(
                            Pixels,
                            X,
                            Bottom - (FLAP_GRAPH_MINIMAP_LANE - 1),
                            Bottom,
                            Pixel
                        );
                    }
                }
            }

            RendHandle Texture = Tex2DAlloc(
                RESOURCE_KIND_STATIC,
                Vec(
                    (i32) FLAP_GRAPH_MINIMAP_WIDTH,
                    (i32) FLAP_GRAPH_MINIMAP_HEIGHT
                ),
                TEX2D_FMT_RGBA8,
                Pixels
            );

            Result.V[0] = Texture.U64[0];
        }
    }

    // NOTE(nc): V[1] says that the object is done, with a texture or with
    //         : none, so that the view does not ask for frames for ever.
    if (!IsReady)
        *Retry = TRUE;
    else
        Result.V[1] = 1;

    AccessClose(Acc);
    ReleaseScratch(Scratch);

    return Result;
}

void
FLAPGraphMinimapObjectDestroy(OCObject Object)
{
    RendHandle Texture = {};

    Texture.U64[0] = Object.V[0];

    if (Object.V[0])
        Tex2DRelease(Texture);
}

INTERNAL OCObject
FLAPGraphMinimapObject(
    FLAPGraphParams* Params,
    FLAPGraphDrawData* Data,
    r1u64 Fit,
    v4f32* FlagColours,
    u32 FlagColoursCount,
    v4f32* ModeColours
) {
    TempArena Scratch = GetScratch(NULL, 0);
    FLAPGraphMinimapKey Head = {};
    u32 ModeColoursCount = ModeColours 
        ? FLAP_GRAPH_MODE_COLOURS 
        : 0;
    u32 ColoursCount = (
        Data->CurvesCount + 
        FlagColoursCount + 
        ModeColoursCount
    );
    u32* Colours = ArenaPushArray(
        Scratch.MemPool, 
        u32, 
        ColoursCount
    );
    u32 Count = 0;
    Str8List Parts = {};

    Head.UnitHash = Params->UnitHash;
    Head.Flight = Params->Flight;
    Head.TimeRangeUSecs = Fit;
    Head.AnalysisIndex = Params->AnalysisIndex;
    Head.ColoursCount = Data->CurvesCount;
    Head.FlagColoursCount = FlagColoursCount;
    Head.ModeColoursCount = ModeColoursCount;

    for (u32 Index = 0; Index < Data->CurvesCount; ++Index)
        Colours[Count++] = FLAPGraphMinimapPixel(
            Data->Curves[Index].Colour
        );

    for (u32 Index = 0; Index < FlagColoursCount; ++Index) {
        Colours[Count++] = FLAPGraphMinimapPixel(
            FlagColours[Index]
        );
    }

    for (u32 Index = 0; Index < ModeColoursCount; ++Index) {
        Colours[Count++] = FLAPGraphMinimapPixel(
            ModeColours[Index]
        );
    }

    ListPush(Scratch.MemPool, &Parts, Str8Struct(&Head));
    ListPush(
        Scratch.MemPool,
        &Parts,
        Str((u8*) Colours, sizeof(u32) * ColoursCount)
    );
    ListPush(Scratch.MemPool, &Parts, Params->MinimapSource);

    OCObject Result = OCObjectFromKey(
        Params->Acc,
        StrListJoin(Scratch.MemPool, &Parts, NULL),
        FLAPGraphMinimapObjectCreate,
        FLAPGraphMinimapObjectDestroy,
        0
    );

    ReleaseScratch(Scratch);

    return Result;
}

INTERNAL
UI_BOX_CUSTOM_DRAW(FLAPGraphMinimapDraw)
{
    FLAPGraphMinimapDrawData* Data = (FLAPGraphMinimapDrawData*) UserData;

    if (!Data->Width)
        return;

    r1u64 Fit = Data->FitRangeUSecs;
    r1u64 View = Data->TimeRangeUSecs;
    r2f32 Strip = Rng(
        Box->Rect.X0 + (f32) Data->PlotX,
        Box->Rect.Y0,
        Box->Rect.X0 + (f32) (Data->PlotX + Data->Width),
        Box->Rect.Y1
    );

    if (Data->HasTexture) {
        DEFER(
            DPushTex2DSampleKind(TEX2D_SAMPLE_LINEAR), 
            DPopTex2DSampleKind()
        ) {
            DrawImage(
                Strip,
                Rng(
                    0.0f,
                    0.0f,
                    (f32) FLAP_GRAPH_MINIMAP_WIDTH,
                    (f32) FLAP_GRAPH_MINIMAP_HEIGHT
                ),
                Data->Texture,
                Vec(1.0f, 1.0f, 1.0f, 1.0f),
                0.0f,
                0.0f,
                0.0f
            );
        }
    }

    // NOTE(nc): the part of the strip that is not in the view is dimmed, at
    //         : each side of the view. the view itself has nothing over
    //         : it. a view that is all past the fit range dims the strip.
    if (Fit.Min < Fit.Max) {
        f64 PXPerUSec = (f64) Data->Width / (f64) Length(Fit);
        f32 X0 = Strip.X0 + (f32) (
            (f64) (CLAMP(Fit.Min, View.Min, Fit.Max) - Fit.Min) * PXPerUSec
        );
        f32 X1 = Strip.X0 + (f32) (
            (f64) (CLAMP(Fit.Min, View.Max, Fit.Max) - Fit.Min) * PXPerUSec
        );

        if (View.Min < Fit.Max && View.Max > Fit.Min)
            X1 = MAX(X1, X0 + 2.0f);

        X0 = FloorF32(X0);
        X1 = MIN(FloorF32(X1), Strip.X1);

        if (X0 > Strip.X0) {
            DrawRect(
                Rng(Strip.X0, Strip.Y0, X0, Strip.Y1),
                Data->DimColour,
                0.0f,
                0.0f,
                0.0f
            );
        }

        if (X1 < Strip.X1) {
            DrawRect(
                Rng(X1, Strip.Y0, Strip.X1, Strip.Y1),
                Data->DimColour,
                0.0f,
                0.0f,
                0.0f
            );
        }

        // NOTE(nc): the view has a solid border, inside its own rect, so
        //         : it does not go over the dim. it is the last thing
        //         : drawn. a view that is all past the fit range has none.
        if (X1 > X0) {
            DrawRect(
                Rng(X0, Strip.Y0, X1, Strip.Y1),
                Data->BorderColour,
                0.0f,
                1.0f,
                0.0f
            );
        }
    }
}

INTERNAL void
FLAPGraph(FLAPGraphParams* Params, r2f32 Rect)
{
    FLVProgram const* Program = Params->Program;
    FLAPGraphMetrics Metrics = Params->Metrics;
    FLVRenderItem* Items = Params->Items;
    FLAPGraphState* State = Params->State;
    FLAPGraphDrawData* Data = ArenaPushArrayZero(
        UIBuildMemPool(),
        FLAPGraphDrawData,
        1
    );
    u32 Width = (u32) Length(Rect).X;
    u32 Height = (u32) Length(Rect).Y;

    // NOTE(nc): the minimap takes its height from the bottom of the graph
    b32 HasMinimap = (
        (Params->Settings & (1 << FLAP_GRAPH_SETTING_KIND_SHOW_MINIMAP)) &&
        Params->MinimapSource.Size &&
        Height > 2 * (u32) Metrics.Minimap
    );

    if (HasMinimap)
        Height -= (u32) Metrics.Minimap;

    f32 RowHeight = UIHeadPXHeight();
    b32 IsReady = TRUE;
    b32 IsCapture = (
        State->CaptureKind &&
        !State->IsCaptureRequested &&
        State->HasHotKey &&
        State->HotKey == Params->Key
    );
    b32 HasCursor = State->HasCursor;
    FLAPGraphClock const* Clock = (
        Params->TimeKind == FLAP_GRAPH_TIME_KIND_LOCAL
    )
        ? Params->Clock
        : NULL;

    if (!Metrics.ColumnsPerUnit)
        Metrics = FLAPGraphMetricsFromFontSize(UIHeadFontSize());

    u32 Gutter = (u32) Metrics.Gutter;
    u32 Gutters = Gutter;

    for (u32 Index = 0; Index < Program->EmitCount; ++Index) {
        if (
            FLAPGraphEmitIsCurve(Program, Items, Index) &&
            Items[Index].Curve.Axis == 2
        ) {
            Gutters = 2 * Gutter;
            break;
        }
    }

    Data->Curves = ArenaPushArrayZero(
        UIBuildMemPool(),
        FLAPGraphDrawCurve,
        Program->EmitCount
    );
    Data->Labels = ArenaPushArrayZero(
        UIBuildMemPool(),
        FLAPGraphDrawLabel,
        Program->EmitCount + 1
    );
    Data->Metrics = Metrics;
    Data->PlotX = Gutter;
    Data->PlotY = (u32) RowHeight;
    Data->Width = (Width > Gutters) ? Width - Gutters : 0;
    Data->Height = (Height > 2 * (u32) RowHeight) 
        ? Height - 2 * (u32) RowHeight 
        : 0;

    UITag("weak"_s8) {
        Data->LabelColour = UIColourFromName("text"_s8);
    }

    Data->CursorColour = UIColourFromName("selection"_s8);
    Data->HasCursor = State->HasCursor;
    Data->CursorTimeUSecs = State->CursorTimeUSecs;
    Data->Clock = Clock;
    Data->IsTimeFromCursor = !!(
        Params->Settings & (1 << FLAP_GRAPH_SETTING_KIND_TIME_FROM_CURSOR)
    );

    // NOTE(nc): the flags and the modes each have a toggle for the graph
    //         : and one for the minimap. what both are off for is not
    //         : looked up.
    b32 HasGraphFlags = !!(
        Params->Settings & (1 << FLAP_GRAPH_SETTING_KIND_SHOW_FLAGS_ON_GRAPH)
    );
    b32 HasGraphModes = !!(
        Params->Settings & (1 << FLAP_GRAPH_SETTING_KIND_SHOW_MODES_ON_GRAPH)
    );
    b32 HasMinimapFlags = (
        HasMinimap &&
        (Params->Settings & (1 << FLAP_GRAPH_SETTING_KIND_SHOW_FLAGS_ON_MINIMAP))
    );
    b32 HasMinimapModes = (
        HasMinimap &&
        (Params->Settings & (1 << FLAP_GRAPH_SETTING_KIND_SHOW_MODES_ON_MINIMAP))
    );
    Str8* FlagNames = NULL;
    v4f32* FlagColours = NULL;
    v4f32* ModeColours = NULL;
    u32 FlagColoursCount = 0;

    // NOTE(nc): a flag takes its colour from the far end of the palette of
    //         : the curves, so that the first flags and the first curves
    //         : do not share one.
    if (HasGraphFlags || HasMinimapFlags) {
        Data->Flags = ArenaPushArrayZero(
            UIBuildMemPool(),
            FLVRenderFlag const*,
            Program->EmitCount
        );
        FlagNames = ArenaPushArrayZero(
            UIBuildMemPool(),
            Str8,
            Program->EmitCount
        );
        FlagColours = ArenaPushArrayZero(
            UIBuildMemPool(),
            v4f32,
            Program->EmitCount
        );

        for (u32 Index = 0; Index < Program->EmitCount; ++Index) {
            if (
                Program->Emits[Index].DisplayKind != TOKEN_KIND_FLAG ||
                Items[Index].IsMissing
            ) {
                continue;
            }

            Data->Flags[FlagColoursCount] = &Items[Index].Flag;
            FlagNames[FlagColoursCount] = Program->Strings[
                Program->Emits[Index].LabelStringIndex
            ];
            FlagColours[FlagColoursCount] = UIColourFromName(
                ArenaPushStrFmt(
                    UIBuildMemPool(),
                    "curve_%u",
                    7 - (FlagColoursCount % 8)
                )
            );
            ++FlagColoursCount;
        }

        Data->FlagColours = FlagColours;
        Data->FlagsCount = HasGraphFlags ? FlagColoursCount : 0;
    }

    if (HasGraphModes || HasMinimapModes) {
        ModeColours = ArenaPushArrayZero(
            UIBuildMemPool(),
            v4f32,
            FLAP_GRAPH_MODE_COLOURS
        );

        for (u32 Index = 0; Index < FLAP_GRAPH_MODE_COLOURS; ++Index) {
            ModeColours[Index] = UIColourFromName(
                ArenaPushStrFmt(UIBuildMemPool(), "curve_%u", Index)
            );
        }
    }

    // NOTE(nc): the mode records of the flight, and the table of mode
    //         : names of its vehicle
    if (HasGraphModes) {
        u64 ModesCount = 0;
        u64 InfoCount = 0;
        FLIMode* ModeTable = FLITableFromName(
            Params->FLI,
            Modes,
            MODES,
            &ModesCount
        );
        FLIFileInfo* Infos = FLITableFromName(
            Params->FLI,
            FileInfo,
            FILE_INFO,
            &InfoCount
        );
        FLVVehicleKind VehicleKind = FLV_VEHICLE_KIND_UNKNOWN;

        if (InfoCount) {
            u64 Length = 0;
            u8* Base = FLIStrFromIndex(
                Params->FLI,
                Infos[InfoCount - 1].VehicleStringIndex,
                &Length
            );

            VehicleKind = FLVVehicleKindFromStr(Str(Base, Length));
        }

        if (VehicleKind < FLV_VEHICLE_KIND_COUNT) {
            Data->ModeNames = FLV_MODE_TABLE[VehicleKind];
            Data->ModeNamesCount = FLV_MODE_TABLE_COUNT[VehicleKind];
        }

        // NOTE(nc): the first element of a table of the FLI is its null
        if (ModesCount > 1) {
            Data->Modes = ModeTable + 1;
            Data->ModesCount = (u32) (ModesCount - 1);
        }

        Data->ModeColours = ModeColours;
    }

    if (HasCursor) {
        FLAPGraphDrawLabel* Label = &Data->Labels[Data->LabelsCount++];

        Label->String = FLAPGraphStrFromTime(
            UIBuildMemPool(),
            Clock,
            State->CursorTimeUSecs,
            3,
            TRUE
        );
        Label->Colour = Data->LabelColour;
    }

    if (!State->IsFitted) {
        r1u64 Fit = {};
        b32 IsFitReady = FLAPGraphFitTimeRange(Params, &Fit);

        IsReady &= IsFitReady;
        State->IsFitReady &= IsFitReady;

        if (Fit.Min < Fit.Max) {
            if (State->FitRangeUSecs.Min < State->FitRangeUSecs.Max) {
                State->FitRangeUSecs.Min = MIN(
                    State->FitRangeUSecs.Min, 
                    Fit.Min
                );
                State->FitRangeUSecs.Max = MAX(
                    State->FitRangeUSecs.Max, 
                    Fit.Max
                );
            } else {
                State->FitRangeUSecs = Fit;
            }
        }
    }

    // NOTE(nc): Count is the columns that the plot asks for. the step comes
    //         : from the ladder, so the columns that are reduced are from
    //         : Count to two times Count. the view is the time range as it
    //         : is: it does not move to the edge of a column.
    u32 Count = Data->Width * Metrics.ColumnsPerUnit;
    u64 Span = Length(State->TimeRangeUSecs);
    u64 Step = FLAPGraphStepFromSpan(Span, Count);
    r1u64 View = FLAPGraphViewFromTimeRange(State->TimeRangeUSecs, Step);
    r1u64 Reduce = Rng(View.Min - (View.Min ? 1 : 0), View.Max + 1);
    // NOTE(nc): the time that the pixel under the cursor covers
    u64 HalfPixelUSecs = Data->Width
        ? Span / (2 * (u64) Data->Width)
        : 0;
    r1u64 CursorRange = Rng(
        State->CursorTimeUSecs - MIN(State->CursorTimeUSecs, HalfPixelUSecs),
        State->CursorTimeUSecs + HalfPixelUSecs + 1
    );

    Data->StepUSecs = Step;
    Data->View = View;
    Data->TimeRangeUSecs = State->TimeRangeUSecs;

    for (u32 Index = 0; Index < Program->EmitCount; ++Index) {
        if (Program->Emits[Index].DisplayKind != TOKEN_KIND_GRAPH)
            continue;

        FLAPGraphDrawLabel* Label = &Data->Labels[Data->LabelsCount++];
        Str8 Name = Program->Strings[Program->Emits[Index].LabelStringIndex];

        if (!FLAPGraphEmitIsCurve(Program, Items, Index)) {
            Label->String = ArenaPushStrFmt(
                UIBuildMemPool(),
                "%S (missing)",
                PRINT_STR(Name)
            );
            Label->Colour = Data->LabelColour;
            continue;
        }

        FLAPGraphColumns* Columns = &Params->Columns[Index];
        FLAPGraphDrawCurve* Curve = &Data->Curves[Data->CurvesCount];
        FLAPGraphKey Key = {};

        Key.Curve = &Items[Index].Curve;
        Key.UnitHash = Params->UnitHash;
        Key.Flight = Params->Flight;
        Key.StepUSecs = Step;
        IsReady &= FLAPGraphColumnsFromKey(
            Params->MemPool,
            Columns,
            &Key,
            Step ? Reduce : Rng((u64) 0ULL, (u64) 0ULL),
            Params->Acc,
            Params->FLI
        );

        Curve->Columns = Columns;
        Curve->Name = Name;
        Curve->Axis = (Key.Curve->Axis == 2);
        Curve->Colour = UIColourFromName(
            ArenaPushStrFmt(
                UIBuildMemPool(),
                "curve_%u",
                Data->CurvesCount % 8
            )
        );
        Label->String = ArenaPushStrFmt(
            UIBuildMemPool(),
            "%S%S%S",
            PRINT_STR(Name),
            PRINT_STR(Curve->Axis ? ":2"_s8 : ""_s8),
            PRINT_STR(
                HasCursor
                    ? FLAPGraphCursorValue(
                        UIBuildMemPool(),
                        Columns,
                        View,
                        CursorRange
                    )
                    : ""_s8
            )
        );
        Label->Colour = Curve->Colour;
        Curve->Samples = FLAPGraphFitValueRange(
            Columns,
            View,
            Data->TimeRangeUSecs,
            &Data->ValueRanges[Curve->Axis],
            &Data->HasRange[Curve->Axis]
        );

        ++Data->CurvesCount;
    }

    // NOTE(nc): a flag that has a lane on the graph is in the legend, in
    //         : the colour of its lane
    for (u32 Index = 0; Index < Data->FlagsCount; ++Index) {
        FLAPGraphDrawLabel* Label = &Data->Labels[Data->LabelsCount++];

        Label->String = FlagNames[Index];
        Label->Colour = FlagColours[Index];
    }

    // NOTE(nc): the ranges of the data in view, before the hold replaces
    //         : them. a double click fits the value axes to them.
    r1f32 AutoRanges[2] = {
        Data->ValueRanges[0],
        Data->ValueRanges[1]
    };
    b32 HasAutoRange[2] = {
        Data->HasRange[0],
        Data->HasRange[1]
    };

    // NOTE(nc): the value axis is view state, as the time range is. it is
    //         : taken from the data in view one time for each fit, and when
    //         : the graph has none for its unit and its flight. after that
    //         : only the user moves it.
    FLAPGraphHold* Hold = Params->Hold;
    b32 IsLockY = !!(
        Params->Settings & (1 << FLAP_GRAPH_SETTING_KIND_LOCK_Y)
    );

    if (Hold) {
        b32 IsAtFit = (
            State->IsFitted &&
            State->TimeRangeUSecs.Min == State->FitRangeUSecs.Min &&
            State->TimeRangeUSecs.Max == State->FitRangeUSecs.Max
        );
        b32 IsFitWaiting = (Hold->FitGeneration != State->FitGeneration);
        b32 IsStale = (
            !Hold->IsHeld ||
            Hold->UnitHash != Params->UnitHash ||
            Hold->Flight.U64[0] != Params->Flight.U64[0] ||
            Hold->Flight.U64[1] != Params->Flight.U64[1]
        );

        if (IsReady && Step && (IsStale || (IsFitWaiting && IsAtFit))) {
            Hold->UnitHash = Params->UnitHash;
            Hold->Flight = Params->Flight;
            Hold->FitGeneration = State->FitGeneration;

            for (u32 Axis = 0; Axis < 2; ++Axis) {
                Hold->ValueRanges[Axis] = Data->ValueRanges[Axis];
                Hold->HasRange[Axis] = Data->HasRange[Axis];
            }

            Hold->IsHeld = TRUE;
            IsStale = FALSE;
        }

        if (!IsStale) {
            for (u32 Axis = 0; Axis < 2; ++Axis) {
                if (!Hold->HasRange[Axis])
                    continue;

                Data->ValueRanges[Axis] = Hold->ValueRanges[Axis];
                Data->HasRange[Axis] = TRUE;
            }
        }
    }

    for (u32 Axis = 0; Axis < 2; ++Axis) {
        if (Data->ValueRanges[Axis].Max <= Data->ValueRanges[Axis].Min) {
            Data->ValueRanges[Axis].Min -= 1.0f;
            Data->ValueRanges[Axis].Max += 1.0f;
        }
    }

    if (!IsReady)
        AppRequestFrame();

    UISetNextPreferredSize(AXIS_2D_X, UI_PERCENT(1.0f, 0.0f));
    UISetNextPreferredSize(AXIS_2D_Y, UI_PX((f32) Height, 1.0f));

    UIBox* Box = UIBuildBoxFromStrFmt(
        UI_BOX_KIND_MOUSE_CLICKABLE | UI_BOX_KIND_SCROLL,
        "###graph_%u",
        Params->Key
    );

    UIBoxEquipCustomDraw(Box, FLAPGraphDraw, Data);

    UISignal Sig = UISignalFromBox(Box);

    if (UI_PRESSED(Sig))
        AppCmd(APP_COMMAND_KIND_FOCUS_PANEL);

    if (!State->HasHotKey || UI_HOVERING(Sig)) {
        State->HotKey = Params->Key;
        State->HasHotKey = TRUE;
    }

    if (IsCapture) {
        State->CaptureRect = Box->Rect;
        State->IsCaptureRequested = TRUE;
        AppRequestFrame();
    }

    if (Count && Step) {
        f64 USecsPerPX = (f64) Span / (f64) Data->Width;
        v2f32 Mouse = UIMouse();
        f32 PlotX0 = Box->Rect.X0 + (f32) Data->PlotX;
        f32 PlotY0 = Box->Rect.Y0 + (f32) Data->PlotY;

        if (
            UI_HOVERING(Sig) &&
            Mouse.X >= PlotX0 &&
            Mouse.X < PlotX0 + (f32) Data->Width &&
            Mouse.Y >= PlotY0 &&
            Mouse.Y < PlotY0 + (f32) Data->Height
        ) {
            f32 Pixel = FloorF32(Mouse.X - PlotX0);
            // NOTE(nc): the middle of the pixel that the mouse is over
            u64 HoverUSecs = State->TimeRangeUSecs.Min + (u64) (
                ((f64) Pixel + 0.5) * USecsPerPX
            );
            u64 RadiusUSecs = (u64) ((f64) Metrics.SnapRadius * USecsPerPX);
            u64 SnapUSecs = 0;
            u64 Best = U64_MAX;

            // NOTE(nc): the nearest sample of any curve takes the cursor.
            //         : only a column with one sample is a target, so a
            //         : dense plot does not snap.
            for (u32 Index = 0; Index < Data->CurvesCount; ++Index) {
                u64 SampleUSecs = 0;

                if (
                    !FLAPGraphSampleNearTime(
                        Data->Curves[Index].Columns,
                        HoverUSecs,
                        RadiusUSecs,
                        &SampleUSecs
                    )
                ) {
                    continue;
                }

                u64 Distance = (SampleUSecs > HoverUSecs)
                    ? SampleUSecs - HoverUSecs
                    : HoverUSecs - SampleUSecs;

                if (Distance < Best) {
                    Best = Distance;
                    SnapUSecs = SampleUSecs;
                }
            }

            b32 IsSnapped = (Best != U64_MAX);

            if (IsSnapped)
                HoverUSecs = SnapUSecs;

            State->NextCursorTimeUSecs = HoverUSecs;
            State->HasNextCursor = TRUE;

            // NOTE(nc): the frame of a capture has no box and no ring
            if (!IsCapture) {
                // NOTE(nc): a snap reads the column of the sample. with no
                //         : snap, the box is for the time of one pixel.
                r1u64 HoverRange = IsSnapped
                    ? Rng(HoverUSecs, HoverUSecs + 1)
                    : Rng(
                        HoverUSecs - MIN(HoverUSecs, HalfPixelUSecs),
                        HoverUSecs + HalfPixelUSecs + 1
                    );

                UITooltip() {
                    UIPreferredWidth(UI_TEXT_DIM(10.0f, 1.0f)) {
                        UILabel(
                            "%S###hover_time",
                            PRINT_STR(
                                FLAPGraphStrFromTime(
                                    UIBuildMemPool(),
                                    Clock,
                                    HoverUSecs,
                                    IsSnapped ? 6 : 3,
                                    TRUE
                                )
                            )
                        );

                        // NOTE(nc): the mode at the time of the cursor is
                        //         : the last record that is not after it
                        if (Data->ModesCount) {
                            u32 ModeIndex = 0;
                            b32 HasMode = FALSE;

                            for (u32 Index = 0; Index < Data->ModesCount; ++Index) {
                                if (Data->Modes[Index].TimeUSecs > HoverUSecs)
                                    break;

                                ModeIndex = Index;
                                HasMode = TRUE;
                            }

                            if (HasMode) {
                                UITag("weak"_s8) {
                                    UILabel(
                                        "%S###hover_mode",
                                        PRINT_STR(
                                            FLAPGraphModeName(
                                                UIBuildMemPool(),
                                                Data->ModeNames,
                                                Data->ModeNamesCount,
                                                Data->Modes[ModeIndex].Mode
                                            )
                                        )
                                    );
                                }
                            }
                        }

                        for (
                            u32 Index = 0;
                            Index < Data->CurvesCount;
                            ++Index
                        ) {
                            FLAPGraphDrawCurve* Curve = &Data->Curves[Index];
                            FLAPGraphProbe Probe = {};

                            FLAPGraphProbeFromTimeRange(
                                Curve->Columns,
                                HoverRange,
                                &Probe
                            );

                            if (Probe.Count == 1) {
                                Curve->HasHoverPoint = TRUE;
                                Curve->HoverTimeUSecs = Probe.TimeUSecs;
                                Curve->HoverValue = Probe.Low;
                                UITextColour(Curve->Colour) {
                                    UILabel(
                                        "%.3f###hover_%u",
                                        (f64) Probe.Low,
                                        Index
                                    );
                                }
                            } else if (Probe.Count > 1) {
                                UITextColour(Curve->Colour) {
                                    UILabel(
                                        "min %.3f  max %.3f  mean %.3f  n %u###hover_%u",
                                        (f64) Probe.Low,
                                        (f64) Probe.High,
                                        (f64) (Probe.Sum / (f32) Probe.Count),
                                        Probe.Count,
                                        Index
                                    );
                                }
                            }
                        }
                    }
                }
            }
        }

        // NOTE(nc): a zoom changes the span. the step changes only when the
        //         : span goes over a power of two, so most zooms use the
        //         : columns that are in the cache. the middle of the view
        //         : stays.
        if (Sig.Scroll.Y) {
            f64 Factor = FLAPGraphZoomFactor(Sig.Scroll.Y);
            u64 NewSpan = (u64) ((f64) Span * Factor + 0.5);

            // NOTE(nc): a small span does not change when it is rounded
            if (NewSpan == Span)
                NewSpan = (Factor > 1.0) ? Span + 1 : Span - 1;

            // NOTE(nc): one microsecond for each column is the limit
            NewSpan = CLAMP(
                (u64) Count,
                NewSpan,
                (u64) FLAP_GRAPH_SPAN_MAX_USECS
            );

            u64 Middle = State->TimeRangeUSecs.Min + Span / 2;
            u64 Half = NewSpan / 2;
            u64 NewMin = (Middle > Half) ? Middle - Half : 0;

            State->TimeRangeUSecs.Min = NewMin;
            State->TimeRangeUSecs.Max = NewMin + NewSpan;

            // NOTE(nc): the value axes zoom with time, about their middle
            for (u32 Axis = 0; Hold && !IsLockY && Axis < 2; ++Axis) {
                if (!Data->HasRange[Axis])
                    continue;

                r1f32 Range = Data->ValueRanges[Axis];
                f32 Middle = 0.5f * (Range.Min + Range.Max);
                f32 Half = 0.5f * (Range.Max - Range.Min) * (f32) Factor;

                // NOTE(nc): a range that f32 cannot hold does not change
                if (Middle - Half < Middle + Half) {
                    Hold->ValueRanges[Axis] = Rng(Middle - Half, Middle + Half);
                    Hold->HasRange[Axis] = TRUE;
                }
            }

            AppRequestFrame();
        }

        // NOTE(nc): shift and the wheel is the X of the scroll. it zooms
        //         : the value axes about their middle.
        if (Sig.Scroll.X && Hold && !IsLockY) {
            f32 Factor = (f32) FLAPGraphZoomFactor(Sig.Scroll.X);

            for (u32 Axis = 0; Axis < 2; ++Axis) {
                if (!Data->HasRange[Axis])
                    continue;

                r1f32 Range = Data->ValueRanges[Axis];
                f32 Middle = 0.5f * (Range.Min + Range.Max);
                f32 Half = 0.5f * (Range.Max - Range.Min) * Factor;

                // NOTE(nc): a range that f32 cannot hold does not change
                if (Middle - Half < Middle + Half) {
                    Hold->ValueRanges[Axis] = Rng(Middle - Half, Middle + Half);
                    Hold->HasRange[Axis] = TRUE;
                }
            }

            AppRequestFrame();
        }

        // NOTE(nc): a double click fits the value axes to the data in
        //         : view. the second press also starts a pan, so the draw
        //         : data takes the new ranges and the pan starts from them.
        if (UI_DOUBLE_CLICKED(Sig) && Hold) {
            for (u32 Axis = 0; Axis < 2; ++Axis) {
                if (!HasAutoRange[Axis])
                    continue;

                Hold->ValueRanges[Axis] = AutoRanges[Axis];
                Hold->HasRange[Axis] = TRUE;
                Data->ValueRanges[Axis] = AutoRanges[Axis];
            }

            AppRequestFrame();
        }

        // NOTE(nc): a pan moves the view in time and in value. the columns
        //         : do not move in time, so the ones that stay on screen
        //         : are in the cache. the value axes move by the pixels of
        //         : the mouse, each at its own scale.
        if (UI_PRESSED(Sig)) {
            FLAPGraphDrag Drag = {};

            Drag.TimeRangeUSecs = State->TimeRangeUSecs;
            Drag.ValueRanges[0] = Data->ValueRanges[0];
            Drag.ValueRanges[1] = Data->ValueRanges[1];
            UIStoreDragStruct(&Drag);
        } else if (UI_DRAGGING(Sig)) {
            FLAPGraphDrag Start = *UIGetDragStruct(FLAPGraphDrag);
            v2f32 Shift = UIDragDelta();
            u64 Delta = (u64) ((f64) AbsF32(Shift.X) * USecsPerPX);
            u64 NewMin = Start.TimeRangeUSecs.Min + Delta;

            if (Shift.X > 0.0f) {
                NewMin = (Delta <= Start.TimeRangeUSecs.Min)
                    ? Start.TimeRangeUSecs.Min - Delta
                    : 0;
            }

            State->TimeRangeUSecs.Min = NewMin;
            State->TimeRangeUSecs.Max = NewMin + Length(Start.TimeRangeUSecs);

            if (Hold && !IsLockY && Data->Height > 1) {
                for (u32 Axis = 0; Axis < 2; ++Axis) {
                    if (!Data->HasRange[Axis])
                        continue;

                    r1f32 Range = Start.ValueRanges[Axis];
                    f32 Move = Shift.Y * (
                        (Range.Max - Range.Min) / (f32) (Data->Height - 1)
                    );

                    Hold->ValueRanges[Axis] = Rng(
                        Range.Min + Move,
                        Range.Max + Move
                    );
                    Hold->HasRange[Axis] = TRUE;
                }
            }

            AppRequestFrame();
        }
    }

    // NOTE(nc): a right click is the settings of the view, as
    //         : the small palette, at the mouse. the query hangs below its
    //         : anchor box, so the Y of the offset is from the bottom.
    if (UI_RIGHT_CLICKED(Sig)) {
        State->HotKey = Params->Key;
        State->HasHotKey = TRUE;
        AppCmd(
            APP_COMMAND_KIND_PUSH_QUERY,
            __Registers.UI = Box->Key,
            __Registers.OffsetPX = Vec(
                UIMouse().X - Box->Rect.X0,
                UIMouse().Y - Box->Rect.Y1
            ),
            __Registers.Expression = ArenaPushStrFmt(
                UIBuildMemPool(),
                "query:config.$%llx",
                Registers()->View
            ),
            __Registers.DoImplicitRoot = TRUE,
            __Registers.DoSmall = TRUE,
            __Registers.DoLister = TRUE
        );
    }

    // NOTE(nc): the minimap is the fit range, with a box for the view. a
    //         : drag of the box is a pan, a drag of an edge changes the
    //         : span, and a press outside the box puts its middle at the
    //         : mouse. it moves time only.
    if (HasMinimap) {
        r1u64 Fit = State->FitRangeUSecs;
        b32 HasFit = (State->IsFitted && Fit.Min < Fit.Max);
        OCObject Object = {};
        FLAPGraphMinimapDrawData* MiniData = ArenaPushArrayZero(
            UIBuildMemPool(),
            FLAPGraphMinimapDrawData,
            1
        );

        if (HasFit) {
            Object = FLAPGraphMinimapObject(
                Params,
                Data,
                Fit,
                HasMinimapFlags 
                    ? FlagColours 
                    : NULL,
                HasMinimapFlags 
                    ? FlagColoursCount 
                    : 0,
                HasMinimapModes 
                    ? ModeColours 
                    : NULL
            );

            // NOTE(nc): the cache has not made the texture yet
            if (!Object.V[1])
                AppRequestFrame();
        }

        MiniData->Texture.U64[0] = Object.V[0];
        MiniData->HasTexture = (Object.V[0] != 0);
        MiniData->FitRangeUSecs = Fit;
        MiniData->TimeRangeUSecs = State->TimeRangeUSecs;
        MiniData->DimColour = UIColourFromName("background"_s8);
        MiniData->DimColour.W = 0.6f;
        MiniData->BorderColour = UIColourFromName("text"_s8);
        MiniData->PlotX = Data->PlotX;
        MiniData->Width = Data->Width;

        UISetNextPreferredSize(AXIS_2D_X, UI_PERCENT(1.0f, 0.0f));
        UISetNextPreferredSize(AXIS_2D_Y, UI_PX(Metrics.Minimap, 1.0f));

        UIBox* MiniBox = UIBuildBoxFromStrFmt(
            UI_BOX_KIND_MOUSE_CLICKABLE,
            "###graph_minimap_%u",
            Params->Key
        );

        UIBoxEquipCustomDraw(MiniBox, FLAPGraphMinimapDraw, MiniData);

        UISignal MiniSig = UISignalFromBox(MiniBox);

        if (HasFit && Data->Width) {
            f64 USecsPerPX = (f64) Length(Fit) / (f64) Data->Width;
            f32 StripX0 = MiniBox->Rect.X0 + (f32) Data->PlotX;
            r1u64 View = State->TimeRangeUSecs;
            f32 WindowX0 = StripX0 + (f32) (
                ((f64) View.Min - (f64) Fit.Min) / USecsPerPX
            );
            f32 WindowX1 = StripX0 + (f32) (
                ((f64) View.Max - (f64) Fit.Min) / USecsPerPX
            );
            f32 MouseX = UIMouse().X;
            FLAPGraphMinimapDragKind Kind = FLAP_GRAPH_MINIMAP_DRAG_KIND_MOVE;

            if (AbsF32(MouseX - WindowX0) <= Metrics.SnapRadius)
                Kind = FLAP_GRAPH_MINIMAP_DRAG_KIND_RESIZE_MIN;
            else if (AbsF32(MouseX - WindowX1) <= Metrics.SnapRadius)
                Kind = FLAP_GRAPH_MINIMAP_DRAG_KIND_RESIZE_MAX;

            if (UI_HOVERING(MiniSig)) {
                MiniBox->HoverCursor = (Kind == FLAP_GRAPH_MINIMAP_DRAG_KIND_MOVE)
                    ? CURSOR_KIND_HAND_POINT
                    : CURSOR_KIND_LEFT_RIGHT;
            }

            if (UI_PRESSED(MiniSig)) {
                FLAPGraphMinimapDrag Drag = {};

                AppCmd(APP_COMMAND_KIND_FOCUS_PANEL);

                if (
                    Kind == FLAP_GRAPH_MINIMAP_DRAG_KIND_MOVE &&
                    (MouseX < WindowX0 || MouseX > WindowX1)
                ) {
                    u64 ViewSpan = Length(View);
                    f64 NewMin = (f64) Fit.Min + (
                        (f64) (MouseX - StripX0) * USecsPerPX
                    ) - 0.5 * (f64) ViewSpan;

                    View.Min = (NewMin > 0.0) ? (u64) NewMin : 0;
                    View.Max = View.Min + ViewSpan;
                    State->TimeRangeUSecs = View;
                }

                Drag.TimeRangeUSecs = View;
                Drag.Kind = Kind;
                UIStoreDragStruct(&Drag);
                AppRequestFrame();
            } else if (UI_DRAGGING(MiniSig)) {
                FLAPGraphMinimapDrag Drag = *UIGetDragStruct(FLAPGraphMinimapDrag);
                f64 Delta = (f64) UIDragDelta().X * USecsPerPX;
                f64 NewMin = (f64) Drag.TimeRangeUSecs.Min;
                f64 NewMax = (f64) Drag.TimeRangeUSecs.Max;
                // NOTE(nc): one microsecond for each column is the limit
                f64 MinSpan = (f64) MAX(Count, (u32) 1);

                if (Drag.Kind != FLAP_GRAPH_MINIMAP_DRAG_KIND_RESIZE_MAX)
                    NewMin += Delta;

                if (Drag.Kind != FLAP_GRAPH_MINIMAP_DRAG_KIND_RESIZE_MIN)
                    NewMax += Delta;

                if (Drag.Kind == FLAP_GRAPH_MINIMAP_DRAG_KIND_MOVE && NewMin < 0.0) {
                    NewMax -= NewMin;
                    NewMin = 0.0;
                }

                if (Drag.Kind == FLAP_GRAPH_MINIMAP_DRAG_KIND_RESIZE_MIN)
                    NewMin = MIN(NewMin, NewMax - MinSpan);

                NewMin = MAX(NewMin, 0.0);

                if (Drag.Kind == FLAP_GRAPH_MINIMAP_DRAG_KIND_RESIZE_MAX)
                    NewMax = MAX(NewMax, NewMin + MinSpan);

                State->TimeRangeUSecs.Min = (u64) NewMin;
                State->TimeRangeUSecs.Max = (u64) NewMax;
                AppRequestFrame();
            }
        }
    }
}
