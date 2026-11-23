#if !defined(__FLAP_GRAPH_H__)
#define __FLAP_GRAPH_H__

#include "nc_types.h"
#include "nc_math.h"

#include "fli.h"

#include "flv_visualisation.h"

struct Arena;

// @defines____________________________________________________________________
#define FLAP_GRAPH_COLUMNS_MIN 64

// @types______________________________________________________________________
struct FLAPGraphKey {
    FLVRenderCurve const* Curve;
    u64                   UnitHash;
    FLIKey                Flight;
    u64                   StepUSecs;
};

struct FLAPGraphRun {
    u64 First;
    f64 Sum;
    f32 Low;
    f32 High;
    f32 FirstValue;
    f32 LastValue;
    u32 Count;
    b32 HasLeadGap;
    b32 HasTrailGap;
    u32 __Padding;
};

struct FLAPGraphEdge {
    u64 TimeUSecs;
    f32 Value;
    b32 Has;
};

struct FLAPGraphProbe {
    u64 TimeUSecs;
    f32 Low;
    f32 High;
    f32 Sum;
    u32 Count;
};

struct FLAPGraphColumns {
    f32*          Min;
    f32*          Max;
    f32*          First;
    f32*          Last;
    f32*          Sum;
    f32*          Phase;
    u32*          Samples;
    u64*          Has;
    u64*          Join;
    FLAPGraphKey  Key;
    r1u64         Valid;
    FLAPGraphEdge Lead;
    FLAPGraphEdge Trail;
    u32           Capacity;
    b32           HasBridge;
};

// @runtime____________________________________________________________________


// @functions__________________________________________________________________
u64 FLAPGraphStepFromSpan(u64 SpanUSecs, u32 Count);
r1u64 FLAPGraphViewFromTimeRange(r1u64 TimeRangeUSecs, u64 StepUSecs);
b32 FLAPGraphColumnHas(FLAPGraphColumns const* Columns, u64 Column, OUT u64* Slot);
b32 FLAPGraphColumnsFromKey(Arena* MemPool, FLAPGraphColumns* Columns, FLAPGraphKey* Key, r1u64 View, Access* Acc, FLIParsed* FLI);
b32 FLAPGraphProbeFromTimeRange(FLAPGraphColumns const* Columns, r1u64 TimeRangeUSecs, OUT FLAPGraphProbe* Probe);
b32 FLAPGraphSampleNearTime(FLAPGraphColumns const* Columns, u64 TimeUSecs, u64 RadiusUSecs, OUT u64* SampleTimeUSecs);
u64 FLAPGraphFitValueRange(FLAPGraphColumns const* Columns, r1u64 View, r1u64 TimeRangeUSecs, r1f32* Range, b32* HasRange);
b32 FLAPGraphClipLine(r2f32 Rect, f64 Pad, f64* FromX, f64* FromY, f64* ToX, f64* ToY);

#endif // __FLAP_GRAPH_H__
