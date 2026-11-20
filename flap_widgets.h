#if !defined(__FLAP_WIDGETS_H__)
#define __FLAP_WIDGETS_H__

#include "nc_types.h"
#include "nc_math.h"
#include "nc_string.h"
#include "nc_ui.h"

#include "fli.h"

#include "flv_lower.h"
#include "flv_visualisation.h"

#include "flap_graph.h"

// @defines____________________________________________________________________
#define FLAP_GRAPH_SPAN_MAX_USECS (1ULL << 40)

typedef u32 FLAPGraphCaptureKind;
enum : u32 {
    FLAP_GRAPH_CAPTURE_KIND_NULL,
    FLAP_GRAPH_CAPTURE_KIND_FILE,
    FLAP_GRAPH_CAPTURE_KIND_CLIPBOARD
};

// @types______________________________________________________________________
struct FLAPTableParams {
    Access*           Acc;
    FLIParsed*        FLI;
    FLVProgram const* Program;
    FLVRenderItem*    Items;
    f32**             Percents;
    UIScrollPoint*    Scroll;
    v2i64*            Cursor;
    v2i64*            Mark;
    FLIKey            Flight;
    u32               HeadEmit;
    u32               Key;
};

struct FLAPTableCtx {
    FLVRenderTable const* Table;
    FLVProgram const*     Program;
    FLVColumn*            Columns;
    FLVColumn             Time;
    FLVRenderFlag const** Flags;
    u32                   FlagsCount;
    u32                   __Padding;
    u64                   StartTimeUSecs;
};

struct FLAPFlagListParams {
    FLVProgram const* Program;
    FLVRenderItem*    Items;
    UIScrollPoint*    Scroll;
    v2i64*            Cursor;
    v2i64*            Mark;
};

struct FLAPErrorListParams {
    Str8List*      Errors;
    UIScrollPoint* Scroll;
    v2i64*         Cursor;
    v2i64*         Mark;
};

struct FLAPGraphMetrics {
    u32 ColumnsPerUnit;
    f32 Gutter;
    f32 ValueTick;
    f32 TimeTick;
    f32 MarkerSpacing;
    f32 LineWidth;
    f32 SnapRadius;
    u32 __Padding;
};

// TODO(nc): can squash the bools into an bitmask
struct FLAPGraphState {
    r1u64                TimeRangeUSecs;
    r1u64                FitRangeUSecs;
    u64                  CursorTimeUSecs;
    u64                  NextCursorTimeUSecs;
    r2f32                CaptureRect;
    b32                  IsFitted;
    b32                  IsFitReady;
    b32                  HasCursor;
    b32                  HasNextCursor;
    u32                  HotKey;
    b32                  HasHotKey;
    FLAPGraphCaptureKind CaptureKind;
    b32                  IsCaptureRequested;
};

struct FLAPGraphParams {
    Arena*            MemPool;
    Access*           Acc;
    FLIParsed*        FLI;
    FLVProgram const* Program;
    FLVRenderItem*    Items;
    FLAPGraphState*   State;
    FLAPGraphColumns* Columns;
    u64               UnitHash;
    FLIKey            Flight;
    FLAPGraphMetrics  Metrics;
    u32               Key;
    u32               __Padding;
};

struct FLAPGraphDrawCurve {
    FLAPGraphColumns const* Columns;
    u64                     Samples;
    u64                     HoverTimeUSecs;
    Str8                    Name;
    v4f32                   Colour;
    u32                     Axis;
    f32                     HoverValue;
    b32                     HasHoverPoint;
    u32                     __Padding;
};

struct FLAPGraphDrawLabel {
    Str8  String;
    v4f32 Colour;
};

struct FLAPGraphDrawData {
    FLAPGraphDrawCurve*   Curves;
    FLAPGraphDrawLabel*   Labels;
    FLVRenderFlag const** Flags;
    r1u64                 TimeRangeUSecs;
    r1u64                 View;
    u64                   StepUSecs;
    u64                   CursorTimeUSecs;
    r1f32                 ValueRanges[2];
    b32                   HasRange[2];
    v4f32                 LabelColour;
    v4f32                 BandColour;
    v4f32                 CursorColour;
    FLAPGraphMetrics      Metrics;
    u32                   CurvesCount;
    u32                   LabelsCount;
    u32                   FlagsCount;
    b32                   HasCursor;
    u32                   PlotX;
    u32                   PlotY;
    u32                   Width;
    u32                   Height;
};

// @runtime____________________________________________________________________


// @functions__________________________________________________________________
void FLAPTable(FLAPTableParams* Params, r2f32 Rect);
void FLAPFlagList(FLAPFlagListParams* Params, r2f32 Rect);
void FLAPErrorList(FLAPErrorListParams* Params, r2f32 Rect);
FLAPGraphMetrics FLAPGraphMetricsFromFontSize(f32 FontSize);
void FLAPGraph(FLAPGraphParams* Params, r2f32 Rect);
b32 FLAPTableCtxFromParams(Arena* MemPool, FLAPTableParams* Params, OUT FLAPTableCtx* Ctx);

INTERNAL UI_BOX_CUSTOM_DRAW(FLAPFlaggedRowDraw);
INTERNAL UI_BOX_CUSTOM_DRAW(FLAPGraphDraw);

#endif // __FLAP_WIDGETS_H__
