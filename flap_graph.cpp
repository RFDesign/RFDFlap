#include "nc_arena.h"
#include "nc_memory.h"
#include "nc_profile.h"

#include "flv_run.h"

#include "flap_graph.h"

// NOTE(nc): the step is a power of two. a zoom between two powers moves the
//         : columns on the screen and does not reduce them again. the step
//         : is not above SpanUSecs / Count, so a column is one unit wide
//         : or less, and it is more than half of one.
INTERNAL u64
FLAPGraphStepFromSpan(u64 SpanUSecs, u32 Count)
{
    u64 Result = 0;

    if (SpanUSecs && Count) {
        u64 Ideal = MAX(SpanUSecs / (u64) Count, (u64) 1);

        for (Result = 1; Result <= Ideal / 2; Result <<= 1);
    }

    return Result;
}

// NOTE(nc): the columns that have a part in the time range
INTERNAL r1u64
FLAPGraphViewFromTimeRange(r1u64 TimeRangeUSecs, u64 StepUSecs)
{
    r1u64 Result = {};

    if (StepUSecs && TimeRangeUSecs.Min < TimeRangeUSecs.Max) {
        Result.Min = TimeRangeUSecs.Min / StepUSecs;
        Result.Max = (TimeRangeUSecs.Max + StepUSecs - 1) / StepUSecs;
    }

    return Result;
}

INTERNAL u64
FLAPGraphSampleFromTime(FLVColumn const* Time, r1u64 Samples, u64 TimeUSecs)
{
    u64 Low = Samples.Min;
    u64 High = Samples.Max;

    while (Low < High) {
        u64 Mid = Low + (High - Low) / 2;

        if (FLVTimeAt(Time, Mid) < TimeUSecs)
            Low = Mid + 1;
        else
            High = Mid;
    }

    return Low;
}

INTERNAL b32
FLAPGraphColumnHas(
    FLAPGraphColumns const* Columns,
    u64 Column,
    OUT u64* Slot
) {
    b32 Result = FALSE;

    *Slot = 0;

    if (
        Columns->Valid.Min <= Column &&
        Columns->Valid.Max > Column
    ) {
        *Slot = Column & SafeTruncateU64(Columns->Capacity - 1);
        Result = !!(Columns->Has[*Slot >> 6] & (1ULL << (*Slot & 63)));
    }

    return Result;
}

INTERNAL b32
FLAPGraphSampleValue(
    FLVRenderCurve const* Curve,
    FLVColumn const* Value,
    u64 Position,
    u64 Sample,
    OUT f32* SampleValue
){
    b32 Result = FLVSeriesBit(Curve->Mask, Position);

    *SampleValue = 0.0f;

    if (Result) {
        if (Curve->ValueColumn == FLV_COLUMN_NONE) {
            *SampleValue = Curve->Values[
                FLVSeriesRank(Curve->Mask, Curve->Rank, Position)
            ];
        } else {
            *SampleValue = (f32) FLVColumnF64At(Value, Sample);
            Result = (*SampleValue * 0.0f == 0.0f);
        }
    }

    return Result;
}

INTERNAL b32
FLAPGraphReduceSamples(
    FLVRenderCurve const* Curve,
    FLVColumn const* Value,
    u64 Position,
    r1u64 Samples,
    OUT FLAPGraphRun* Run
) {
    b32 Result = FALSE;

    MemZero(Run, sizeof(*Run));
    Run->First = Samples.Min;

    if (Curve->ValueColumn == FLV_COLUMN_NONE) {
        u64 ValueIndex = FLVSeriesRank(
            Curve->Mask,
            Curve->Rank,
            Position
        );
        u64 ValueEnd = FLVSeriesRank(
            Curve->Mask,
            Curve->Rank,
            Position + Length(Samples)
        );

        if (ValueIndex < ValueEnd) {
            Run->Low = Curve->Values[ValueIndex];
            Run->High = Run->Low;
            Run->Sum = (f64) Run->Low;
            Run->FirstValue = Run->Low;
            Run->LastValue = Curve->Values[ValueEnd - 1];
            Run->Count = SafeTruncateU64(ValueEnd - ValueIndex);
            Result = TRUE;

            for (
                ++ValueIndex;
                ValueIndex < ValueEnd;
                ++ValueIndex
            ) {
                f32 V = Curve->Values[ValueIndex];

                Run->Low = MIN(Run->Low, V);
                Run->High = MAX(Run->High, V);
                Run->Sum += (f64) V;
            }

            // NOTE(nc): fewer values than samples, so the mask cut some of
            //         : them. only then are the two ends of the run read.
            if ((u64) Run->Count < Length(Samples)) {
                for (
                    ;
                    !FLVSeriesBit(
                        Curve->Mask,
                        Position + (Run->First - Samples.Min)
                    );
                    ++Run->First
                );

                Run->HasLeadGap = (Run->First != Samples.Min);
                Run->HasTrailGap = !FLVSeriesBit(
                    Curve->Mask,
                    Position + Length(Samples) - 1
                );
            }
        }
    } else {
        b32 IsBroken = FALSE;

        for (u64 Index = Samples.Min; Index < Samples.Max; ++Index) {
            if (!FLVSeriesBit(Curve->Mask, Position + (Index - Samples.Min))) {
                IsBroken = TRUE;
                continue;
            }

            f32 V = (f32) FLVColumnF64At(Value, Index);

            if (V * 0.0f != 0.0f) {
                IsBroken = TRUE;
                continue;
            }

            if (!Result) {
                Run->Low = V;
                Run->High = V;
                Run->FirstValue = V;
                Run->First = Index;
                Run->HasLeadGap = IsBroken;
                Result = TRUE;
            } else {
                Run->Low = MIN(Run->Low, V);
                Run->High = MAX(Run->High, V);
            }

            Run->Sum += (f64) V;
            Run->LastValue = V;
            ++Run->Count;
            IsBroken = FALSE;
        }

        Run->HasTrailGap = IsBroken;
    }

    return Result;
}

INTERNAL void
FLAPGraphClear(FLAPGraphColumns* Columns, r1u64 Span)
{
    u64 Mask = SafeTruncateU64(Columns->Capacity - 1);

    for (u64 Column = Span.Min; Column < Span.Max; ++Column) {
        u64 Slot = Column & Mask;
        u64 Bit = 1ULL << (Slot & 63);

        Columns->Has[Slot >> 6] &= ~Bit;
        Columns->Join[Slot >> 6] &= ~Bit;
        Columns->Samples[Slot] = 0;
    }
}

INTERNAL void
FLAPGraphReduce(
    FLAPGraphColumns* Columns,
    FLVColumn const* Time,
    FLVColumn const* Value,
    r1u64 Span
) {
    FLVRenderCurve const* Curve = Columns->Key.Curve;
    u64 Step = Columns->Key.StepUSecs;
    u64 Mask = SafeTruncateU64(Columns->Capacity - 1);
    r1u64 TimeRange = Rng(Span.Min, Span.Max) * Step;
    u64 PositionBase = 0;

    for (
        u64 RangeIndex = 0;
        RangeIndex < Curve->RangesCount;
        PositionBase += Length(Curve->Ranges[RangeIndex]), ++RangeIndex
    ) {
        r1u64 Range = Curve->Ranges[RangeIndex];

        if (Range.Min == Range.Max)
            continue;

        if (
            FLVTimeAt(Time, Range.Max - 1) < TimeRange.Min ||
            FLVTimeAt(Time, Range.Min) >= TimeRange.Max
        ) {
            continue;
        }

        u64 Sample = FLAPGraphSampleFromTime(Time, Range, TimeRange.Min);
        f32 Before = 0.0f;

        // NOTE(nc): a line comes to a sample only from the sample before it
        //         : in the same range, and only if that sample has a value.
        //         : so a cut sample, a NaN or the start of a range is a gap,
        //         : and the edge of a column is not.
        b32 IsBroken = !(
            Sample > Range.Min &&
            FLAPGraphSampleValue(
                Curve,
                Value,
                PositionBase + (Sample - 1 - Range.Min),
                Sample - 1,
                &Before
            )
        );

        while (Sample < Range.Max) {
            u64 TimeUSecs = FLVTimeAt(Time, Sample);

            if (TimeUSecs >= TimeRange.Max)
                break;

            u64 Column = TimeUSecs / Step;
            u64 End = FLAPGraphSampleFromTime(
                Time,
                Rng(Sample + 1, Range.Max),
                (Column + 1) * Step
            );
            FLAPGraphRun Run = {};
            b32 HasValue = FLAPGraphReduceSamples(
                Curve,
                Value,
                PositionBase + (Sample - Range.Min),
                Rng(Sample, End),
                &Run
            );

            if (HasValue) {
                u64 Slot = Column & Mask;
                u64 Bit = 1ULL << (Slot & 63);

                if (Columns->Has[Slot >> 6] & Bit) {
                    Columns->Min[Slot] = MIN(Columns->Min[Slot], Run.Low);
                    Columns->Max[Slot] = MAX(Columns->Max[Slot], Run.High);
                    Columns->Last[Slot] = Run.LastValue;
                    Columns->Sum[Slot] += (f32) Run.Sum;
                } else {
                    u64 FirstUSecs = (Run.First == Sample)
                        ? TimeUSecs
                        : FLVTimeAt(Time, Run.First);

                    Columns->Min[Slot] = Run.Low;
                    Columns->Max[Slot] = Run.High;
                    Columns->First[Slot] = Run.FirstValue;
                    Columns->Last[Slot] = Run.LastValue;
                    Columns->Sum[Slot] = (f32) Run.Sum;
                    // NOTE(nc): where the first sample is in the column,
                    //         : from 0 to 1. the draw uses it when the
                    //         : column has one sample.
                    Columns->Phase[Slot] = (
                        (f32) (FirstUSecs - Column * Step) / (f32) Step
                    );
                    Columns->Has[Slot >> 6] |= Bit;

                    if (!IsBroken && !Run.HasLeadGap)
                        Columns->Join[Slot >> 6] |= Bit;
                }

                Columns->Samples[Slot] += Run.Count;
                IsBroken = Run.HasTrailGap;
            } else {
                IsBroken = TRUE;
            }

            Sample = End;
        }
    }
}

// NOTE(nc): the caller clears Lead, Trail and HasBridge before this.
INTERNAL void
FLAPGraphEdges(
    FLAPGraphColumns* Columns,
    FLVColumn const* Time,
    FLVColumn const* Value
) {
    FLVRenderCurve const* Curve = Columns->Key.Curve;
    r1u64 TimeRange = (
        Rng(Columns->Valid.Min, Columns->Valid.Max) * Columns->Key.StepUSecs
    );
    u64 PositionBase = 0;

    for (
        u64 RangeIndex = 0;
        RangeIndex < Curve->RangesCount;
        PositionBase += Length(Curve->Ranges[RangeIndex]), ++RangeIndex
    ) {
        r1u64 Range = Curve->Ranges[RangeIndex];

        if (Range.Min == Range.Max)
            continue;

        if (
            FLVTimeAt(Time, Range.Max - 1) < TimeRange.Min ||
            FLVTimeAt(Time, Range.Min) >= TimeRange.Max
        ) {
            continue;
        }

        // NOTE(nc): Head is the first sample in the columns or after them,
        //         : Tail is the first sample after them. the range is not
        //         : all before the columns and not all after them, so
        //         : Head is below Range.Max and Tail is above Range.Min.
        u64 Head = FLAPGraphSampleFromTime(Time, Range, TimeRange.Min);
        u64 Tail = FLAPGraphSampleFromTime(
            Time,
            Rng(Head, Range.Max),
            TimeRange.Max
        );
        f32 Inner = 0.0f;

        // NOTE(nc): the Join bit of the first column with a value says if
        //         : the line from Lead is drawn.
        if (Head > Range.Min) {
            Columns->Lead.TimeUSecs = FLVTimeAt(Time, Head - 1);
            Columns->Lead.Has = FLAPGraphSampleValue(
                Curve,
                Value,
                PositionBase + (Head - 1 - Range.Min),
                Head - 1,
                &Columns->Lead.Value
            );
        }

        // NOTE(nc): no column has a bit for Trail, so the sample before it
        //         : is tested here.
        if (Tail < Range.Max) {
            Columns->Trail.TimeUSecs = FLVTimeAt(Time, Tail);
            Columns->Trail.Has = (
                FLAPGraphSampleValue(
                    Curve,
                    Value,
                    PositionBase + (Tail - 1 - Range.Min),
                    Tail - 1,
                    &Inner
                ) &&
                FLAPGraphSampleValue(
                    Curve,
                    Value,
                    PositionBase + (Tail - Range.Min),
                    Tail,
                    &Columns->Trail.Value
                )
            );
            // NOTE(nc): no sample is in the columns, so Lead and Trail are
            //         : the two ends of one line.
            Columns->HasBridge = (Columns->Trail.Has && Head == Tail);
        }
    }
}

INTERNAL b32
FLAPGraphColumnsFromKey(
    Arena *MemPool,
    FLAPGraphColumns *Columns,
    FLAPGraphKey *Key,
    r1u64 View,
    Access *Acc,
    FLIParsed *FLI
) {
    b32 Result = TRUE;
    b32 IsMatch = (
        Columns->Key.Curve == Key->Curve &&
        Columns->Key.UnitHash == Key->UnitHash &&
        Columns->Key.Flight.U64[0] == Key->Flight.U64[0] &&
        Columns->Key.Flight.U64[1] == Key->Flight.U64[1] &&
        Columns->Key.StepUSecs == Key->StepUSecs
    );
    u64 Count = Length(View);

    if (!IsMatch || SafeTruncateU64(Columns->Capacity) < Count)
        Columns->Valid = Rng((u64) 0ULL, (u64) 0ULL);

    Columns->Key = *Key;

    if (!Count || !Key->StepUSecs)
        return Result;

    r1u64 Spans[2] = {};
    u32 SpansCount = 0;

    if (
        Columns->Valid.Min >= View.Max ||
        Columns->Valid.Max <= View.Min
    ) {
        Columns->Valid = Rng((u64) 0ULL, (u64) 0ULL);
        Spans[SpansCount++] = View;
    } else {
        if (View.Min < Columns->Valid.Min)
            Spans[SpansCount++] = Rng(View.Min, Columns->Valid.Min);

        if (View.Max > Columns->Valid.Max)
            Spans[SpansCount++] = Rng(Columns->Valid.Max, View.Max);
    }

    // NOTE(nc): the columns and the edges are for this view already. this
    //         : is the frame where nothing moved.
    if (
        !SpansCount &&
        Columns->Valid.Min == View.Min &&
        Columns->Valid.Max == View.Max
    ) {
        return Result;
    }

    // NOTE(nc): the edges are for the old view. if the columns are not
    //         : ready, no edge is drawn.
    Columns->Lead.Has = FALSE;
    Columns->Trail.Has = FALSE;
    Columns->HasBridge = FALSE;

    FLVColumn Time = {};
    FLVColumn Value = {};

    Result = FLVColumnInit(
        Acc,
        Key->Flight,
        FLI,
        Key->Curve->TimeColumn,
        0,
        &Time
    );

    if (Key->Curve->ValueColumn != FLV_COLUMN_NONE) {
        Result &= FLVColumnInit(
            Acc,
            Key->Flight,
            FLI,
            Key->Curve->ValueColumn,
            0,
            &Value
        );
    }

    if (Result) {
        if ((u64) Columns->Capacity < Count) {
            u32 Capacity = FLAP_GRAPH_COLUMNS_MIN;

            while ((u64) Capacity < Count)
                Capacity <<= 1;

            Columns->Capacity = Capacity;
            Columns->Min = ArenaPushArray(
                MemPool,
                f32,
                Columns->Capacity
            );
            Columns->Max = ArenaPushArray(
                MemPool,
                f32,
                Columns->Capacity
            );
            Columns->First = ArenaPushArray(
                MemPool,
                f32,
                Columns->Capacity
            );
            Columns->Last = ArenaPushArray(
                MemPool,
                f32,
                Columns->Capacity
            );
            Columns->Sum = ArenaPushArray(
                MemPool,
                f32,
                Columns->Capacity
            );
            Columns->Phase = ArenaPushArray(
                MemPool,
                f32,
                Columns->Capacity
            );
            Columns->Samples = ArenaPushArray(
                MemPool,
                u32,
                Columns->Capacity
            );
            Columns->Has = ArenaPushArray(
                MemPool,
                u64,
                FLVSeriesWordCount(Columns->Capacity)
            );
            Columns->Join = ArenaPushArray(
                MemPool,
                u64,
                FLVSeriesWordCount(Columns->Capacity)
            );
        }

        PROFILE(FLAPGraphReduceTimer) {
            for (u32 Index = 0; Index < SpansCount; ++Index) {
                FLAPGraphClear(Columns, Spans[Index]);

                if (!Time.IsMissing && !Value.IsMissing)
                    FLAPGraphReduce(Columns, &Time, &Value, Spans[Index]);
            }

            Columns->Valid = View;

            if (!Time.IsMissing && !Value.IsMissing)
                FLAPGraphEdges(Columns, &Time, &Value);
        }
    }

    return Result;
}

// NOTE(nc): the time of the one sample of a column. the phase is the
//         : place of the sample in the column, so the time is exact to
//         : the microsecond.
INTERNAL u64
FLAPGraphSampleTime(FLAPGraphColumns const* Columns, u64 Column, u64 Slot)
{
    u64 Step = Columns->Key.StepUSecs;
    u64 Result = Column * Step + (u64) (
        (f64) Columns->Phase[Slot] * (f64) Step + 0.5
    );

    return Result;
}

// NOTE(nc): all that the columns hold for a range of time. it reads the
//         : columns only, so it is for the view that they were reduced
//         : for. the result is FALSE when the range has no sample.
INTERNAL b32
FLAPGraphProbeFromTimeRange(
    FLAPGraphColumns const* Columns,
    r1u64 TimeRangeUSecs,
    OUT FLAPGraphProbe* Probe
) {
    r1u64 Span = FLAPGraphViewFromTimeRange(
        TimeRangeUSecs,
        Columns->Key.StepUSecs
    );

    MemZero(Probe, sizeof(*Probe));

    for (u64 Column = Span.Min; Column < Span.Max; ++Column) {
        u64 Slot = 0;

        if (!FLAPGraphColumnHas(Columns, Column, &Slot))
            continue;

        if (!Probe->Count) {
            Probe->TimeUSecs = FLAPGraphSampleTime(Columns, Column, Slot);
            Probe->Low = Columns->Min[Slot];
            Probe->High = Columns->Max[Slot];
        } else {
            Probe->Low = MIN(Probe->Low, Columns->Min[Slot]);
            Probe->High = MAX(Probe->High, Columns->Max[Slot]);
        }

        Probe->Sum += Columns->Sum[Slot];
        Probe->Count += Columns->Samples[Slot];
    }

    b32 Result = (Probe->Count != 0);

    return Result;
}

// NOTE(nc): the nearest column that holds one sample, within the radius.
//         : a column with more than one sample is a range and has no one
//         : time, so it is not a target.
INTERNAL b32
FLAPGraphSampleNearTime(
    FLAPGraphColumns const* Columns,
    u64 TimeUSecs,
    u64 RadiusUSecs,
    OUT u64* SampleTimeUSecs
) {
    r1u64 Span = FLAPGraphViewFromTimeRange(
        Rng(
            TimeUSecs - MIN(TimeUSecs, RadiusUSecs),
            TimeUSecs + RadiusUSecs + 1
        ),
        Columns->Key.StepUSecs
    );
    u64 Best = U64_MAX;
    b32 Result = FALSE;

    *SampleTimeUSecs = 0;

    for (u64 Column = Span.Min; Column < Span.Max; ++Column) {
        u64 Slot = 0;

        if (
            !FLAPGraphColumnHas(Columns, Column, &Slot) ||
            Columns->Samples[Slot] != 1
        ) {
            continue;
        }

        u64 SampleUSecs = FLAPGraphSampleTime(Columns, Column, Slot);
        u64 Distance = (SampleUSecs > TimeUSecs)
            ? SampleUSecs - TimeUSecs
            : TimeUSecs - SampleUSecs;

        if (Distance <= RadiusUSecs && Distance < Best) {
            Best = Distance;
            *SampleTimeUSecs = SampleUSecs;
            Result = TRUE;
        }
    }

    return Result;
}

INTERNAL u64
FLAPGraphFitValueRange(
    FLAPGraphColumns const* Columns,
    r1u64 View,
    r1u64 TimeRangeUSecs,
    r1f32* Range,
    b32* HasRange
) {
    u64 Result = 0;

    if (!Columns->Key.StepUSecs)
        return Result;

    for (u64 Column = View.Min; Column < View.Max; ++Column) {
        u64 Slot = 0;

        if (!FLAPGraphColumnHas(Columns, Column, &Slot))
            continue;

        Result += Columns->Samples[Slot];

        if (!*HasRange) {
            Range->Min = Columns->Min[Slot];
            Range->Max = Columns->Max[Slot];
            *HasRange = TRUE;
        } else {
            Range->Min = MIN(Range->Min, Columns->Min[Slot]);
            Range->Max = MAX(Range->Max, Columns->Max[Slot]);
        }
    }

    if (Columns->HasBridge) {
        FLAPGraphEdge Lead = Columns->Lead;
        FLAPGraphEdge Trail = Columns->Trail;
        f64 Span = (f64) (Trail.TimeUSecs - Lead.TimeUSecs);
        f32 Rise = Trail.Value - Lead.Value;
        f32 Enter = Lead.Value + Rise * (f32) (
            ((f64) TimeRangeUSecs.Min - (f64) Lead.TimeUSecs) / Span
        );
        f32 Leave = Lead.Value + Rise * (f32) (
            ((f64) TimeRangeUSecs.Max - (f64) Lead.TimeUSecs) / Span
        );

        if (!*HasRange) {
            Range->Min = MIN(Enter, Leave);
            Range->Max = MAX(Enter, Leave);
            *HasRange = TRUE;
        } else {
            Range->Min = MIN(Range->Min, MIN(Enter, Leave));
            Range->Max = MAX(Range->Max, MAX(Enter, Leave));
        }
    }

    return Result;
}

// NOTE(nc): the part of a line that is in the rect, with Pad more at each
//         : side of it. an end of the line can be far from the rect, so
//         : the line is cut in f64. the result is FALSE when no part of
//         : the line is in the rect.
INTERNAL b32
FLAPGraphClipLine(
    r2f32 Rect,
    f64 Pad,
    f64* FromX,
    f64* FromY,
    f64* ToX,
    f64* ToY
) {
    f64 DeltaX = *ToX - *FromX;
    f64 DeltaY = *ToY - *FromY;
    f64 Deltas[4] = {
        -DeltaX,
        DeltaX,
        -DeltaY,
        DeltaY
    };
    f64 Distances[4] = {
        *FromX - ((f64) Rect.X0 - Pad),
        ((f64) Rect.X1 + Pad) - *FromX,
        *FromY - ((f64) Rect.Y0 - Pad),
        ((f64) Rect.Y1 + Pad) - *FromY
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

    b32 Result = (IsVisible && Enter < Leave);

    if (Result) {
        f64 X = *FromX;
        f64 Y = *FromY;

        *FromX = X + Enter * DeltaX;
        *FromY = Y + Enter * DeltaY;
        *ToX = X + Leave * DeltaX;
        *ToY = Y + Leave * DeltaY;
    }

    return Result;
}
