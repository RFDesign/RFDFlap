#include "nc_arena.h"
#include "nc_application.h"
#include "nc_draw.h"
#include "nc_memory.h"
#include "nc_ui_components.h"
#include "nc_render.h"
#include "nc_profile.h"

#include "flv_token.h"
#include "flv_run.h"

#include "flap_graph.h"
#include "flap_widgets.h"

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
        u8* Bands = ArenaPushArrayZero(Scratch.MemPool, u8, Width);

        for (u32 FlagIndex = 0; FlagIndex < Data->FlagsCount; ++FlagIndex) {
            FLVRenderFlag const* Flag = Data->Flags[FlagIndex];

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
                    PlotRect.Y0,
                    PlotRect.X0 + (f32) Pixel,
                    PlotRect.Y1
                ),
                Data->BandColour,
                0.0f,
                0.0f,
                0.0f
            );
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

    // NOTE(nc): time labels
    u64 Span = Length(ViewRange);

    if (Step && Span) {
        u32 MaxTicks = MAX(
            (u32) ((f32) Width / MAX(Metrics.TimeTick, 1.0f)),
            (u32) 1
        );
        f32 TickStep = FLAPGraphTickStep(
            (f32) Span / 1000000.0f,
            MaxTicks
        );
        u64 TickUSecs = MAX((u64) (TickStep * 1000000.0f + 0.5f), (u64) 1);

        for (
            u64 TimeUSecs = (
                (ViewRange.Min + TickUSecs - 1) / TickUSecs
            ) * TickUSecs;
            TimeUSecs < ViewRange.Max;
            TimeUSecs += TickUSecs
        ) {
            f32 X = FloorF32(
                PlotRect.X0 + (f32) (
                    (f64) (TimeUSecs - ViewRange.Min) * PXPerUSec
                )
            );
            Str8 String = (TickUSecs >= MILLION(1))
                ? ArenaPushStrFmt(
                    Scratch.MemPool,
                    "%llus",
                    TimeUSecs / MILLION(1)
                )
                : ArenaPushStrFmt(
                    Scratch.MemPool,
                    "%llu.%03llus",
                    TimeUSecs / MILLION(1),
                    (TimeUSecs % MILLION(1)) / THOUSAND(1)
                );

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
    f32 RowHeight = UIHeadPXHeight();
    b32 IsReady = TRUE;
    b32 IsCapture = (
        State->CaptureKind &&
        !State->IsCaptureRequested &&
        State->HasHotKey &&
        State->HotKey == Params->Key
    );
    b32 HasCursor = State->HasCursor;

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

    Data->BandColour = UIColourFromName("hover"_s8);
    Data->BandColour.W *= 0.2f;
    Data->CursorColour = UIColourFromName("selection"_s8);
    Data->HasCursor = State->HasCursor;
    Data->CursorTimeUSecs = State->CursorTimeUSecs;
    Data->Flags = ArenaPushArrayZero(
        UIBuildMemPool(),
        FLVRenderFlag const*,
        Program->EmitCount
    );

    for (u32 Index = 0; Index < Program->EmitCount; ++Index) {
        if (
            Program->Emits[Index].DisplayKind == TOKEN_KIND_FLAG &&
            !Items[Index].IsMissing
        ) {
            Data->Flags[Data->FlagsCount++] = &Items[Index].Flag;
        }
    }

    if (HasCursor) {
        FLAPGraphDrawLabel* Label = &Data->Labels[Data->LabelsCount++];

        Label->String = ArenaPushStrFmt(
            UIBuildMemPool(),
            "%llu.%03llus",
            State->CursorTimeUSecs / MILLION(1),
            (State->CursorTimeUSecs % MILLION(1)) / THOUSAND(1)
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

        r1f32* Range = &Data->ValueRanges[Curve->Axis];

        for (
            u64 Column = View.Min; 
            Step && Column < View.Max; 
            ++Column
        ) {
            u64 Slot = 0;

            if (!FLAPGraphColumnHas(Columns, Column, &Slot))
                continue;

            Curve->Samples += Columns->Samples[Slot];

            if (!Data->HasRange[Curve->Axis]) {
                Range->Min = Columns->Min[Slot];
                Range->Max = Columns->Max[Slot];
                Data->HasRange[Curve->Axis] = TRUE;
            } else {
                Range->Min = MIN(Range->Min, Columns->Min[Slot]);
                Range->Max = MAX(Range->Max, Columns->Max[Slot]);
            }
        }

        // NOTE(nc): no sample is in the view, but the line between the
        //         : sample before it and the sample after it is. the range
        //         : takes the part of that line that is in the view.
        if (Step && Columns->HasBridge) {
            FLAPGraphEdge Lead = Columns->Lead;
            FLAPGraphEdge Trail = Columns->Trail;
            f64 Span = (f64) (Trail.TimeUSecs - Lead.TimeUSecs);
            f32 Rise = Trail.Value - Lead.Value;
            f32 Enter = Lead.Value + Rise * (f32) (
                ((f64) Data->TimeRangeUSecs.Min - (f64) Lead.TimeUSecs) / Span
            );
            f32 Leave = Lead.Value + Rise * (f32) (
                ((f64) Data->TimeRangeUSecs.Max - (f64) Lead.TimeUSecs) / Span
            );

            if (!Data->HasRange[Curve->Axis]) {
                Range->Min = MIN(Enter, Leave);
                Range->Max = MAX(Enter, Leave);
                Data->HasRange[Curve->Axis] = TRUE;
            } else {
                Range->Min = MIN(Range->Min, MIN(Enter, Leave));
                Range->Max = MAX(Range->Max, MAX(Enter, Leave));
            }
        }

        ++Data->CurvesCount;
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
                        if (IsSnapped) {
                            UILabel(
                                "%llu.%06llus###hover_time",
                                HoverUSecs / MILLION(1),
                                HoverUSecs % MILLION(1)
                            );
                        } else {
                            UILabel(
                                "%llu.%03llus###hover_time",
                                HoverUSecs / MILLION(1),
                                (HoverUSecs % MILLION(1)) / THOUSAND(1)
                            );
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
            f64 Factor = 1.0;

            // TODO(nc): check if this is the same on linux
            // NOTE(nc): Windows sends 120 for one notch, on all machines.
            //         : the UI divides it by 30, so one notch is 4 here.
            //         : each of the 4 is the 4th root of 0.8 or of 1.25, so
            //         : one notch is a zoom of 0.8 or of 1.25. a wheel with
            //         : a finer step gets a finer zoom.
            for (
                i32 Notch = Sig.Scroll.Y;
                Notch;
                Notch += (Notch < 0)
                    ? 1
                    : -1
            ) {
                Factor *= (Notch < 0)
                    ? 0.9457416090960342
                    : 1.0573712634405641;
            }

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
            AppRequestFrame();
        }

        // NOTE(nc): a pan moves the view in time, by less than a column if
        //         : the mouse does. the columns do not move in time, so the
        //         : ones that stay on screen are in the cache.
        if (UI_PRESSED(Sig)) {
            UIStoreDragStruct(&State->TimeRangeUSecs);
        } else if (UI_DRAGGING(Sig)) {
            r1u64 Start = *UIGetDragStruct(r1u64);
            f32 Shift = UIDragDelta().X;
            u64 Delta = (u64) ((f64) AbsF32(Shift) * USecsPerPX);
            u64 NewMin = Start.Min + Delta;

            if (Shift > 0.0f) {
                NewMin = (Delta <= Start.Min)
                    ? Start.Min - Delta
                    : 0;
            }

            State->TimeRangeUSecs.Min = NewMin;
            State->TimeRangeUSecs.Max = NewMin + Length(Start);
            AppRequestFrame();
        }
    }
}
