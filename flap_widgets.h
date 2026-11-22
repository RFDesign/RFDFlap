#if !defined(__FLAP_WIDGETS_H__)
#define __FLAP_WIDGETS_H__

#include "nc_types.h"
#include "nc_math.h"
#include "nc_string.h"
#include "nc_ui.h"

#include "fli.h"

#include "flv_lower.h"
#include "flv_stdlib.h"
#include "flv_visualisation.h"

#include "flap_graph.h"

// @defines____________________________________________________________________
#define FLAP_GRAPH_SPAN_MAX_USECS (1ULL << 40)
#define FLAP_GRAPH_MINIMAP_WIDTH  512
#define FLAP_GRAPH_MINIMAP_HEIGHT 32
#define FLAP_GRAPH_MINIMAP_LANE   3
#define FLAP_GRAPH_MODE_COLOURS   8

typedef u32 FLAPGraphCaptureKind;
enum : u32 {
    FLAP_GRAPH_CAPTURE_KIND_NULL,
    FLAP_GRAPH_CAPTURE_KIND_FILE,
    FLAP_GRAPH_CAPTURE_KIND_CLIPBOARD
};

typedef u32 FLAPGraphSettingKind;
enum : u32 {
    FLAP_GRAPH_SETTING_KIND_LOCK_Y,
    FLAP_GRAPH_SETTING_KIND_SHOW_MINIMAP,
    FLAP_GRAPH_SETTING_KIND_SHOW_FLAGS_ON_GRAPH,
    FLAP_GRAPH_SETTING_KIND_SHOW_MODES_ON_GRAPH,
    FLAP_GRAPH_SETTING_KIND_SHOW_FLAGS_ON_MINIMAP,
    FLAP_GRAPH_SETTING_KIND_SHOW_MODES_ON_MINIMAP,
    FLAP_GRAPH_SETTING_KIND_COUNT
};

typedef u32 FLAPGraphMinimapDragKind;
enum : u32 {
    FLAP_GRAPH_MINIMAP_DRAG_KIND_MOVE,
    FLAP_GRAPH_MINIMAP_DRAG_KIND_RESIZE_MIN,
    FLAP_GRAPH_MINIMAP_DRAG_KIND_RESIZE_MAX
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
    f32 Minimap;
};

struct FLAPGraphSettingInfo {
    Str8 Name;
    Str8 DisplayName;
};

struct FLAPGraphHold {
    u64    UnitHash;
    FLIKey Flight;
    r1f32  ValueRanges[2];
    b32    HasRange[2];
    b32    IsHeld;
    u32    FitGeneration;
};

struct FLAPGraphDrag {
    r1u64 TimeRangeUSecs;
    r1f32 ValueRanges[2];
};

struct FLAPGraphMinimapDrag {
    r1u64                    TimeRangeUSecs;
    FLAPGraphMinimapDragKind Kind;
    u32                      __Padding;
};

// NOTE(nc): the head of the key of a minimap texture. the colours follow
//         : it: the curves, then the flags, then the modes. a count of
//         : zero is a layer that is off. the source of the view is last.
struct FLAPGraphMinimapKey {
    u64    UnitHash;
    FLIKey Flight;
    r1u64  TimeRangeUSecs;
    u32    AnalysisIndex;
    u32    ColoursCount;
    u32    FlagColoursCount;
    u32    ModeColoursCount;
};

struct FLAPGraphMinimapDrawData {
    RendHandle Texture;
    r1u64      FitRangeUSecs;
    r1u64      TimeRangeUSecs;
    v4f32      DimColour;
    v4f32      BorderColour;
    u32        PlotX;
    u32        Width;
    b32        HasTexture;
    u32        __Padding;
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
    b32                  IsMenuOpened;
    u32                  FitGeneration;
};

struct FLAPGraphParams {
    Arena*            MemPool;
    Access*           Acc;
    FLIParsed*        FLI;
    FLVProgram const* Program;
    FLVRenderItem*    Items;
    FLAPGraphState*   State;
    FLAPGraphColumns* Columns;
    FLAPGraphHold*    Hold;
    Str8              MinimapSource;
    u64               UnitHash;
    FLIKey            Flight;
    FLAPGraphMetrics  Metrics;
    u32               Key;
    u32               Settings;
    u32               AnalysisIndex;
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
    v4f32*                FlagColours;
    FLIMode const*        Modes;
    FLVModeName const*    ModeNames;
    v4f32*                ModeColours;
    r1u64                 TimeRangeUSecs;
    r1u64                 View;
    u64                   StepUSecs;
    u64                   CursorTimeUSecs;
    r1f32                 ValueRanges[2];
    b32                   HasRange[2];
    v4f32                 LabelColour;
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
    u32                   ModesCount;
    u32                   ModeNamesCount;
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
INTERNAL UI_BOX_CUSTOM_DRAW(FLAPGraphMinimapDraw);

#endif // __FLAP_WIDGETS_H__
