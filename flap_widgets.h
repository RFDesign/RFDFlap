#if !defined(__FLAP_WIDGETS_H__)
#define __FLAP_WIDGETS_H__

#include "nc_types.h"
#include "nc_math.h"
#include "nc_string.h"
#include "nc_ring.h"
#include "nc_ui.h"

#include "fli.h"

#include "flv_lower.h"
#include "flv_stdlib.h"
#include "flv_visualisation.h"

#include "flap_graph.h"

// @defines____________________________________________________________________
#define FLAP_GRAPH_SPAN_MAX_USECS      (1ULL << 40)
#define FLAP_GRAPH_MINIMAP_WIDTH       512
#define FLAP_GRAPH_MINIMAP_HEIGHT      32
#define FLAP_GRAPH_MODE_COLOURS        8
#define FLAP_GRAPH_FLAGS_MAX           64
#define FLAP_GRAPH_FLAG_ALPHA          0.5f
#define FLAP_GRAPH_MINIMAP_LANE        3
#define FLAP_GRAPH_MODE_COLOURS        8
#define FLAP_GPS_EPOCH_UNIX_SECS       315964800ULL
#define FLAP_GPS_WEEK_SECS             604800ULL
#define FLAP_GPS_LEAP_SECS             18ULL
#define FLAP_DAY_SECS                  86400ULL
#define FLAP_GRAPH_VALUE_ZOOM_MIN      1.0e-5f
#define FLAP_GRAPH_PLOT_PAD_PX         65536.0f
#define FLAP_MAP_FIT_MARGIN            1.1
#define FLAP_MAP_METRES_PER_PX_MIN     1.0e-3
#define FLAP_MAP_METRES_PER_PX_MAX     1.0e5
#define FLAP_MAP_CMD_NAV_LAST          95
#define FLAP_MAP_EARTH_RADIUS_METRES   6378100.0
#define FLAP_MAP_LATITUDE_MAX          85.05112878
#define FLAP_MAP_TILE_SIZE             256
#define FLAP_MAP_TILES_MAX             256
#define FLAP_MAP_TILE_PARENTS          3
#define FLAP_MAP_SETTLE_USECS          SECONDS(4)
#define FLAP_MAP_FETCH_SLOTS           128
#define FLAP_MAP_FETCH_RING_SIZE       MB(4)
#define FLAP_MAP_FETCH_TIMEOUT_USECS   SECONDS(30)
#define FLAP_MAP_TILE_NAME_MAX         64
#define FLAP_MAP_FETCH_TIMEOUT_USECS   SECONDS(30)
#define FLAP_MAP_LAYER_START_UNIX_SECS 1448323200ULL
#define FLAP_MAP_FILETIME_UNIX_EPOCH   116444736000000000ULL
#define FLAP_MAP_SECS_PER_DAY          86400ULL

typedef u32 FLAPMapFetchStatus;
enum : u32 {
    FLAP_MAP_FETCH_STATUS_NULL,
    FLAP_MAP_FETCH_STATUS_PENDING,
    FLAP_MAP_FETCH_STATUS_FAILED
};

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
    FLAP_GRAPH_SETTING_KIND_SHOW_FLAGS,
    FLAP_GRAPH_SETTING_KIND_SHOW_MODES_ON_GRAPH,
    FLAP_GRAPH_SETTING_KIND_SHOW_MODES_ON_MINIMAP,
    FLAP_GRAPH_SETTING_KIND_TIME_FROM_CURSOR,
    FLAP_GRAPH_SETTING_KIND_COUNT
};

typedef u32 FLAPGraphTimeKind;
enum : u32 {
    FLAP_GRAPH_TIME_KIND_RELATIVE,
    FLAP_GRAPH_TIME_KIND_LOCAL,
    FLAP_GRAPH_TIME_KIND_COUNT
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

struct FLAPGraphClock {
    FLIKey Flight;
    u64    LocalStartUSecs;
    b32    IsResolved;
    b32    Has;
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

struct FLAPGraphMinimapKey {
    u64    UnitHash;
    FLIKey Flight;
    r1u64  TimeRangeUSecs;
    u32    AnalysisIndex;
    u32    ColoursCount;
    u32    ModeColoursCount;
    u32    __Padding;
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
    b32                  IsTabCapture;
    u32                  __Padding;
};

struct FLAPGraphParams {
    Arena*                MemPool;
    Access*               Acc;
    FLIParsed*            FLI;
    FLVProgram const*     Program;
    FLVRenderItem*        Items;
    FLAPGraphState*       State;
    FLAPGraphColumns*     Columns;
    FLAPGraphHold*        Hold;
    FLAPGraphClock const* Clock;
    Str8                  MinimapSource;
    u64                   UnitHash;
    FLIKey                Flight;
    FLAPGraphMetrics      Metrics;
    u32                   Key;
    u32                   Settings;
    u32                   AnalysisIndex;
    FLAPGraphTimeKind     TimeKind;
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
    FLAPGraphClock const* Clock;
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
    b32                   IsTimeFromCursor;
    u32                   __Padding;
};

struct FLAPMapImagery {
    Str8  Name;
    char* URL;
    u32   Level;
    u32   Servers;
};

struct FLAPMapTileKey {
    u32 Level;
    u32 X;
    u32 Y;
    u32 __Padding;
};

struct FLAPMapFetch {
    u64                ID;
    u64                EndTimeUSecs;
    u64                NameSize;
    FLAPMapFetchStatus Status;
    u32                __Padding;
    u8                 Name[FLAP_MAP_TILE_NAME_MAX];
};

struct FLAPMapShared {
    Arena*        MemPool;
    Handle        Mtx;
    SafeByteRing* Ring;
    Str8          CachePath;
    u64           NextWaitUSecs;
    FLAPMapFetch  Fetches[FLAP_MAP_FETCH_SLOTS];
};

struct FLAPMapImage {
    RendHandle Texture;
    r2f32      Dst;
    r2f32      Src;
};

struct FLAPMapState {
    u64    UnitHash;
    FLIKey Flight;
    f64    CentreNorth;
    f64    CentreEast;
    f64    MetresPerPX;
    u64    SettleEndUSecs;
    b32    IsFitted;
    u32    __Padding;
};

struct FLAPMapDrag {
    f64 CentreNorth;
    f64 CentreEast;
};

struct FLAPMapSegment {
    v2f32 From;
    v2f32 To;
};

struct FLAPMapParams {
    Access*               Acc;
    FLIParsed*            FLI;
    FLVRenderMap const*   Map;
    FLAPGraphState const* Graph;
    FLAPMapState*         State;
    u64                   UnitHash;
    FLIKey                Flight;
    u32                   Key;
    f32                   LineWidth;
};

struct FLAPMapDrawData {
    FLAPMapSegment* Segments;
    FLAPMapSegment* Legs;
    v2f32*          Waypoints;
    FLAPMapImage*   Images;
    u64             ImagesCount;
    u64             SegmentsCount;
    u64             LegsCount;
    u64             WaypointsCount;
    v4f32           Colour;
    v4f32           MissionColour;
    f32             LineWidth;
    f32             WaypointRadius;
};

// @runtime____________________________________________________________________


// @functions__________________________________________________________________
void FLAPTable(FLAPTableParams* Params, r2f32 Rect);
void FLAPFlagList(FLAPFlagListParams* Params, r2f32 Rect);
void FLAPErrorList(FLAPErrorListParams* Params, r2f32 Rect);
FLAPGraphMetrics FLAPGraphMetricsFromFontSize(f32 FontSize);
b32 FLAPGraphEmitIsCurve(FLVProgram const* Program, FLVRenderItem* Items, u32 Index);
f32 FLAPGraphTickStep(f32 Range, u32 MaxTicks);
u64 FLAPGraphTickUSecs(u64 SpanUSecs, u32 MaxTicks, b32 IsClock);
Str8 FLAPGraphStrFromTime(Arena* MemPool, FLAPGraphClock const* Clock, u64 TimeUSecs, u32 Digits, b32 HasDate);
u32 FLAPGraphMinimapPixel(v4f32 Colour);
Str8 FLAPGraphModeName(Arena* MemPool, FLVModeName const* Names, u32 Count, u32 Number);
void FLAPGraph(FLAPGraphParams* Params, r2f32 Rect);
void FLAPMap(FLAPMapParams* Params, r2f32 Rect);
void FLAPMapInit(void);
OCObject FLAPMapTileObjectCreate(Str8 Key, b32* CancelSignal, OUT b32* Retry, OUT u64* Generation);
void FLAPMapTileObjectDestroy(OCObject Object);
b32 FLAPTableCtxFromParams(Arena* MemPool, FLAPTableParams* Params, OUT FLAPTableCtx* Ctx);
u64* FLAPGraphFlagMasks(Arena* MemPool, FLAPGraphDrawData const* Data);

INTERNAL UI_BOX_CUSTOM_DRAW(FLAPFlaggedRowDraw);
INTERNAL UI_BOX_CUSTOM_DRAW(FLAPGraphDraw);
INTERNAL UI_BOX_CUSTOM_DRAW(FLAPGraphMinimapDraw);
INTERNAL UI_BOX_CUSTOM_DRAW(FLAPMapDraw);

#endif // __FLAP_WIDGETS_H__
