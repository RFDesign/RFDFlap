#if !defined(__NC_DRAW_H__)
#define __NC_DRAW_H__

#include "nc_types.h"
#include "nc_string.h"
#include "nc_render.h"
#include "cache/nc_font_cache.h"

// @defines____________________________________________________________________
#define DBucketStackDecls struct {                \
    Texture2DSampleKindNode* HeadTex2DSampleKind; \
    XForm2DNode*             HeadXForm2D;         \
    ClipNode*                HeadClip;            \
    TransparencyNode*        HeadTransparency;    \
}

#define DBucketStackInit(X) do {                                                            \
    (X)->HeadTex2DSampleKind = (Texture2DSampleKindNode*) &EMPTY_TEX_2D_SAMPLE_KIND_NODE;   \
    (X)->HeadXForm2D         = (XForm2DNode*) &EMPTY_XFORM_2D_NODE;                         \
    (X)->HeadClip            = (ClipNode*) &EMPTY_CLIP_NODE;                                \
    (X)->HeadTransparency    = (TransparencyNode*) &EMPTY_TRANSPARENCY_NODE;                \
} while (FALSE)

#define DBucketScope(X)  DEFER(DPushBucket(X), DPopBucket())
#define DClipScope(X)    DEFER(DPushClip(X), DPopClip())
#define DXForm2DScope(X) DEFER(DPushXForm2D(X), DPopXForm2D())

// @types______________________________________________________________________
struct FancyStrParams {
    FontTag        Font;
    FontRasterKind RasterKind;
    v4f32          Colour;
    f32            Size;
    f32            UnderlineThickness;
    f32            StrikethroughThickness;
};

struct FancyStr {
    Str8           String;
    FancyStrParams Params;
};

struct FancyStrNode {
    FancyStrNode* Next;
    FancyStr      V;
};

struct FancyStrList {
    FancyStrNode* Head;
    FancyStrNode* Tail;
    u64           NodeCount;
    u64           TotalSize;
};

struct FancyRun {
    FontRun Run;
    v4f32   Colour;
    f32     UnderlineThickness;
    f32     StrikethroughThickness;
    b32     Icon;
};

struct FancyRunNode {
    FancyRunNode* Next;
    FancyRun      V;
};

struct FancyRunList {
    FancyRunNode* Head;
    FancyRunNode* Tail;
    u64           NodeCount;
    v2f32         Dimensions;
};

struct Texture2DSampleKindNode {
    Texture2DSampleKindNode* Next;
    RendTex2DSampleKind      V;
};

struct XForm2DNode {
    XForm2DNode* Next;
    m3f32        V;
};

struct ClipNode {
    ClipNode* Next;
    r2f32     V;
};

struct TransparencyNode {
    TransparencyNode* Next;
    f32               V;
};

struct DBucket {
    RendPassList Passes;
    u64          StackGeneration;
    u64          LastCommandStackgeneration;
    DBucketStackDecls;
};

struct DBucketSelectionNode {
    DBucketSelectionNode* Next;
    DBucket*              Bucket;
};

struct DThreadContext {
    Arena*                MemPool;
    u64                   MemPoolFrameStartPosition;
    FontTag               IconFont;
    DBucketSelectionNode* HeadBucket;
    DBucketSelectionNode* FreeBucketSelection;
};

// @runtime____________________________________________________________________
constexpr readonly global Texture2DSampleKindNode EMPTY_TEX_2D_SAMPLE_KIND_NODE = {
    NULL, 
    TEX2D_SAMPLE_NEAREST
};

constexpr readonly global XForm2DNode EMPTY_XFORM_2D_NODE = {
    NULL, 
    {
        1.0f, 0.0f, 0.0f, 
        0.0f, 1.0f, 0.0f, 
        0.0f, 0.0f, 1.0f
    }
};

constexpr readonly global ClipNode EMPTY_CLIP_NODE = {
    NULL, 
    {}
};

constexpr readonly global TransparencyNode EMPTY_TRANSPARENCY_NODE = {
    NULL, 
    0.0f
};

// @functions__________________________________________________________________
u64 DHash(Str8 String);
void ListPush(Arena* MemPool, FancyStrList* List, FancyStr* String);
void ListPush(Arena* MemPool, FancyStrList* List, FancyStrParams* Params, FancyStrParams* Overrides, Str8 String);
void ListCat(FancyStrList* Dst, FancyStrList* Src);
FancyStrList ListCpy(Arena* MemPool, FancyStrList* List);
Str8 Str8FromFancyStrList(Arena* MemPool, FancyStrList* List);
FancyRunList FancyRunListFromFancyStrList(Arena* MemPool, f32 TabSizePX, FancyStrList* List);
v2f32 DDimensionsFromFancyStrList(f32 TabSizePX, FancyStrList* List);
void DBeginFrame(FontTag IconFont);
void DSubmitBucket(Handle WindowHandle, RendHandle RendHandle, DBucket* Bucket);
DBucket* DBucketAlloc(void);
void DPushBucket(DBucket* Bucket);
void DPopBucket(void);
DBucket* DTopBucket(void);
RendTex2DSampleKind DPushTex2DSampleKind(RendTex2DSampleKind Kind);
m3f32 DPushXForm2D(m3f32 Mat);
r2f32 DPushClip(r2f32 Clip);
f32 DPushTransparency(f32 Transparency);
RendTex2DSampleKind DPopTex2DSampleKind(void);
m3f32 DPopXForm2D(void);
r2f32 DPopClip(void);
f32 DPopTransparency(void);
RendTex2DSampleKind DTopTex2DSampleKind(void);
m3f32 DTopXForm2D(void);
r2f32 DTopClip(void);
f32 DTopTransparency(void);
Rect2DInstr* DrawRect(r2f32 Destination, v4f32 Colour, f32 CornerRadius, f32 BorderThickness, f32 EdgeSoftness);
Rect2DInstr* DrawImage(r2f32 Destination, r2f32 Source, RendHandle Texture, v4f32 Colour, f32 CornerRadius, f32 BorderThickness, f32 EdgeSoftness);
RendPassBlurParams* DrawBlur(r2f32 Rect, f32 BlurSize, f32 CornerRadius);
RendPassGeo3DParams* DrawGeo3DBegin(r2f32 ViewPort, m4f32 View, m4f32 Projection);
Mesh3DInstr* DrawMesh(RendHandle Vertices, RendHandle Indices, RendGeoTopKind Topology, GeoVertFlag VertexFlags, RendHandle AlbedoTexture, m4f32 XForm);
void DrawSubBucket(DBucket* Bucket);
void DrawTruncatedFancyRunList(v2f32 P, FancyRunList* List, f32 MaxX, FontRun TrailerRun);
void DrawTextRun(v2f32 P, v4f32 Colour, FontRun Run);
void NC_DrawText(FontTag Font, f32 Size, f32 BaseAlignPX, f32 TabAlignPX, FontRasterKind Kind, v2f32 P, v4f32 Colour, Str8 String);
FMRangeList FuzzyFindFStrs(Arena* MemPool, FancyStrList* FancyStrings, Str8 Needle);
void DrawTruncatedFancyRunFuzzyMatches(v2f32 Position, FancyRunList* List, f32 MaxX, FMRangeList* Ranges, v4f32 Colour);
RendPassCADParams* DrawCADBegin(r2f32 ViewPort, m3f32 View, m3f32 Projection);
CADLineInstr* DrawCADLine(v2f32 PointA, v2f32 PointB, v4f32 Colour, f32 ThicknessPX, f32 SoftnessPX, CADLineCapKind CapKind);
CADRibbonInstr* DrawCADRibbon(v2f32 TopA, v2f32 TopB, f32 BottomA, f32 BottomB, v4f32 Colour, f32 ThicknessPX, f32 SoftnessPX);
CADArcInstr* DrawCADArc(v2f32 CentrePoint,f32 Radius, f32 StartAngle, f32 EndAngle, v4f32 Colour, f32 ThicknessPX, f32 SoftnessPX);
CADArcInstr* DrawCADCircle(v2f32 CentrePoint, f32 Radius, v4f32 Colour, f32 ThicknessPX, f32 SoftnessPX);
CADArcInstr* DrawCADDisc(v2f32 CentrePoint, f32 Radius, v4f32 Colour, f32 SoftnessPX);
CADTriangleInstr* DrawCADTriangle(v2f32 PointA, v2f32 PointB, v2f32 PointC, v4f32 Colour);

#endif // __NC_DRAW_H__
