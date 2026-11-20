#include "nc_application.h"
#include "nc_arena.h"
#include "nc_config.h"
#include "nc_render.h"
#include "nc_ui_components.h"
#include "nc_window.h"

#include "flv_token.h"
#include "flv_run.h"

#include "flap_export.h"
#include "flap_views.h"
#include "flap_widgets.h"

Str8 
AppViewFilePath(void)
{
    ConfigNode* View = ConfigNodeFromID(Registers()->View);
    ConfigNode* File = ConfigNodeChildFromStr(View, "file"_s8);
    Str8 String = File->Head->String;

    return String;
}

INTERNAL b32
FLAPFlightIsChecked(ConfigNode* File)
{
    Str8 Value = ConfigNodeChildFromStr(File, "enabled"_s8)->Head->String;
    b32 Result = (!Value.Size || U64FromStr(Value) != 0);

    return Result;
}

INTERNAL void
FLAPFlightCheckStore(ConfigNode* File, b32 IsChecked)
{
    ConfigNode* Enabled = ConfigNodeChildFromStrOrAlloc(
        APP_STATE->Config,
        File,
        "enabled"_s8
    );

    ConfigNodeNewReplace(
        APP_STATE->Config,
        Enabled,
        IsChecked ? "1"_s8 : "0"_s8
    );
}

internal FLAPFlightSet
FLAPFlightSetFromConfig(Arena* MemPool)
{
    TempArena Scratch = GetScratch(&MemPool, 1);
    ConfigNodePtrList FileList = ConfigNodeTopLevelListFromStr(
        Scratch.MemPool,
        "file"_s8
    );
    ConfigNodePtrArray Files = ConfigNodePtrArrayFromList(
        Scratch.MemPool,
        &FileList
    );
    ConfigNode* Transient = ConfigNodeChildFromStr(
        ConfigNodeRoot(),
        "transient"_s8
    );
    Str8 SelectedPath = ConfigNodeChildFromStr(
        Transient,
        "flight"_s8
    )->Head->String;
    FLAPFlightSet Result = {};
    u32 CursorFlight = U32_MAX;

    Result.Selected = U32_MAX;
    Result.Keys.Count = Files.Count;
    Result.Keys.V = ArenaPushArrayZero(MemPool, FLIKey, Result.Keys.Count);

    for (u64 Index = 0; Index < Files.Count; ++Index) {
        Str8 Path = Files.V[Index]->Head->String;

        Result.Keys.V[Index] = FLIKeyFromPathTimeStamp(Path, 0);

        if (!FLAPFlightIsChecked(Files.V[Index]))
            continue;

        if (Result.Selected == U32_MAX)
            Result.Selected = (u32) Index;

        if (SelectedPath.Size && PathMatchNormalised(Path, SelectedPath))
            CursorFlight = (u32) Index;
    }

    if (CursorFlight != U32_MAX)
        Result.Selected = CursorFlight;
 
    ReleaseScratch(Scratch);
 
    return Result;
}

INTERNAL f32**
FLAPTablePercents(FLAPViewTabCtx* Ctx, u32 NodeIndex, u32 Count)
{
    FLAPViewTabState* State = Ctx->State;
 
    if (State->PercentsCount[NodeIndex] != Count) {
        f32 Share = 1.0f / (f32) Count;
 
        State->Percents[NodeIndex] = ArenaPushArray(
            AppViewMemPool(), 
            f32, 
            Count
        );
        State->PercentsCount[NodeIndex] = Count;
 
        for (u32 Index = 0; Index < Count; ++Index)
            State->Percents[NodeIndex][Index] = Share;
    }
 
    f32** Result = ArenaPushArray(UIBuildMemPool(), f32*, Count);
 
    for (u32 Index = 0; Index < Count; ++Index)
        Result[Index] = &State->Percents[NodeIndex][Index];
 
    return Result;
}

INTERNAL FLAPGraphColumns*
FLAPGraphColumnsArray(FLAPViewTabCtx* Ctx, u32 NodeIndex, u32 Count)
{
    FLAPViewTabState* State = Ctx->State;

    if (State->GraphColumnsCount[NodeIndex] != Count) {
        State->GraphColumns[NodeIndex] = ArenaPushArrayZero(
            AppViewMemPool(),
            FLAPGraphColumns,
            Count
        );
        State->GraphColumnsCount[NodeIndex] = Count;
    }

    return State->GraphColumns[NodeIndex];
}

// NOTE(nc): <folder of the FLV>/<name of the FLV>_<name of the analysis>_
//         : <timestamp>.<extension of the kind>
INTERNAL Str8
FLAPExportPath(
    Arena* MemPool,
    FLAPViewTabCtx* Ctx,
    u32 NodeIndex,
    FLAPExportKind Kind
) {
    Str8 Path = AppViewFilePath();
    Str8 Folder = StrChopLastSlash(Path);
    Str8 Name = StrSkipLastSlash(Path);
    Str8 Title = Ctx->Unit->LayoutNames[NodeIndex];

    // NOTE(nc): the name of the FLV file with no extension
    for (u64 Index = Name.Size; Index--; ) {
        if (Name.Str[Index] == '.') {
            Name.Size = Index;
            break;
        }
    }

    if (!Title.Size) {
        Title = ArenaPushStrFmt(
            MemPool,
            "%u",
            Ctx->Unit->Lowered.Layout[NodeIndex].Ordinal
        );
    }

    Str8 Result = ArenaPushStrFmt(
        MemPool,
        "%S/%S_%S_%llu.%S",
        PRINT_STR(Folder),
        PRINT_STR(Name),
        PRINT_STR(Title),
        TimeGetTimestamp(),
        PRINT_STR(FLAP_EXPORT_KIND_EXTENSION_TABLE[Kind])
    );

    return Result;
}

INTERNAL b32
FLAPToolButton(
    AppIconKind Icon,
    Str8 Key,
    Str8 Tip,
    Str8 CommandName
){
    UISignal Sig = {};

    UIPreferredWidth(UI_EM(2.5f, 1.0f)) {
        Sig = AppIconButton(Icon, NULL, Key);
    }

    b32 Result = UI_CLICKED(Sig);

    if (Tip.Size && !Result && UI_HOVERING(Sig)) {
        UITooltip() {
            UISetNextPreferredWidth(UI_SUM_OF_CHILDREN(1.0f));

            UIRow() {
                UIPreferredWidth(UI_TEXT_DIM(10.0f, 1.0f)) {
                    UILabel(Tip);
                }

                if (CommandName.Size) {
                    UISpacer(UI_EM(1.0f, 1.0f));
                    AppCommandBindingButtons(
                        CommandName,
                        ""_s8,
                        1,
                        APP_COMMAND_BINDING_BTN_FLAG_NO_EDIT
                    );
                }
            }
        }
    }

    return Result;
}

INTERNAL void
FLAPAnalysisUI(FLAPViewTabCtx* Ctx, u32 NodeIndex, r2f32 Rect)
{
    FLVLayoutNode const* Node = &Ctx->Unit->Lowered.Layout[NodeIndex];
    FLVProgram const* Program = &Ctx->Unit->Lowered.Program[
        Node->AnalysisIndex
    ];
    FLAPViewTabState* State = Ctx->State;
    Str8 Title = Ctx->Unit->LayoutNames[NodeIndex];
    FLVRenderItem* Items = FLVResultFromUnit(
        Ctx->Acc,
        Ctx->Unit,
        Ctx->Keys,
        Ctx->KeysCount,
        Ctx->Keys[Ctx->Flight],
        Node->AnalysisIndex,
        0
    );
    u32 TableEmit = U32_MAX;
    u32 FlagCount = 0;
    u32 CurveCount = 0;

    if (!Title.Size)
        Title = ArenaPushStrFmt(UIBuildMemPool(), "%u", Node->Ordinal);

    // NOTE(nc): what the analysis shows is known before the title row, so
    //         : that the row has the buttons for it.
    if (Items) {
        for (u32 Index = 0; Index < Program->EmitCount; ++Index) {
            u8 Kind = Program->Emits[Index].DisplayKind;

            if (
                Kind == TOKEN_KIND_TABLE &&
                TableEmit == U32_MAX &&
                !Items[Index].IsMissing &&
                Items[Index].Kind == FLV_RENDER_KIND_TABLE
            ) {
                TableEmit = Index;
            }

            if (Kind == TOKEN_KIND_FLAG)
                ++FlagCount;

            if (
                Kind == TOKEN_KIND_GRAPH &&
                !Items[Index].IsMissing &&
                Items[Index].Kind == FLV_RENDER_KIND_CURVE
            ) {
                ++CurveCount;
            }
        }
    }

    b32 IsGraph = (CurveCount != 0);
    b32 IsTable = (!IsGraph && TableEmit != U32_MAX);

    UISetNextChildLayoutAxis(AXIS_2D_Y);

    UIBox* Box = UIBuildBoxFromStrFmt(
        UI_BOX_KIND_CLIP,
        "###analysis_%u",
        NodeIndex
    );

    UIParent(Box) {
        f32 RowHeight = UIHeadPXHeight();

        UITag("menu_bar"_s8) {
            UIWidthFill() {
                UISetNextPreferredHeight(UI_PX(RowHeight, 1.0f));
                UISetNextFlags(UI_BOX_KIND_DRAW_BACKGROUND);

                UINamedRow("###analysis_toolbar"_s8) {
                    UILabel(Title);

                    if (IsGraph) {
                        FLAPGraphState* Graph = &State->Graph;
                        // NOTE(nc): a frame that is drawn for a capture
                        //         : has no tips, so a tip is never in the
                        //         : image.
                        b32 HasTips = !Graph->CaptureKind;
                        b32 FitClicked = FLAPToolButton(
                            APP_ICON_KIND_REFRESH,
                            "###fit"_s8,
                            HasTips ? "Fit to data (Z)"_s8 : ""_s8,
                            ""_s8
                        );
                        b32 CopyClicked = FLAPToolButton(
                            APP_ICON_KIND_CLIPBOARD,
                            "###copy_image"_s8,
                            HasTips ? "Copy graph as image"_s8 : ""_s8,
                            APP_COMMAND_KIND_INFO_TABLE[
                                APP_COMMAND_KIND_COPY_GRAPH_IMAGE
                            ].String
                        );
                        b32 PNGClicked = FLAPToolButton(
                            APP_ICON_KIND_BITMAP,
                            "###export_png"_s8,
                            HasTips ? "Export graph as PNG"_s8 : ""_s8,
                            APP_COMMAND_KIND_INFO_TABLE[
                                APP_COMMAND_KIND_EXPORT_GRAPH_PNG
                            ].String
                        );

                        // NOTE(nc): the fit is for all the graphs of the
                        //         : tab, so the view starts it at the top
                        //         : of the next frame.
                        if (FitClicked) {
                            State->IsFitRequested = TRUE;
                            AppRequestFrame();
                        }

                        // NOTE(nc): the button makes its own graph the hot
                        //         : graph. the graph is built after this
                        //         : row, so this frame is the capture.
                        if ((CopyClicked || PNGClicked) && !Graph->CaptureKind) {
                            Graph->HotKey = NodeIndex;
                            Graph->HasHotKey = TRUE;
                            Graph->CaptureKind = PNGClicked
                                ? FLAP_GRAPH_CAPTURE_KIND_FILE
                                : FLAP_GRAPH_CAPTURE_KIND_CLIPBOARD;
                        }
                    } else if (IsTable) {
                        b32 CSVClicked = FLAPToolButton(
                            APP_ICON_KIND_GRID,
                            "###export_csv"_s8,
                            "Export table as CSV"_s8,
                            APP_COMMAND_KIND_INFO_TABLE[
                                APP_COMMAND_KIND_EXPORT_TABLE_CSV
                            ].String
                        );
                        b32 TXTClicked = FLAPToolButton(
                            APP_ICON_KIND_LIST,
                            "###export_txt"_s8,
                            "Export table as text"_s8,
                            APP_COMMAND_KIND_INFO_TABLE[
                                APP_COMMAND_KIND_EXPORT_TABLE_TXT
                            ].String
                        );

                        // NOTE(nc): the button makes its own table the hot
                        //         : table. the export is in the block of
                        //         : the table, below.
                        if ((CSVClicked || TXTClicked) && !State->TableExportKind) {
                            State->HotTable = NodeIndex;
                            State->HasHotTable = TRUE;
                            State->TableExportKind = CSVClicked
                                ? FLAP_EXPORT_KIND_CSV
                                : FLAP_EXPORT_KIND_TXT;
                        }
                    }

                    if (IsGraph || IsTable)
                        UISpacer(UI_EM(0.5f, 1.0f));
                }
            }
        }

        Rect.Y0 += RowHeight;

        if (!Items) {
            UITag("weak"_s8) {
                UIWidthFill() {
                    UILabel("running..."_s8);
                }
            }

            AppRequestFrame();
        } else if (IsGraph) {
            FLAPGraphParams Params = {};

            Params.Acc = Ctx->Acc;
            Params.FLI = Ctx->FLI;
            Params.Program = Program;
            Params.Items = Items;
            Params.State = &State->Graph;
            Params.Columns = FLAPGraphColumnsArray(
                Ctx,
                NodeIndex,
                Program->EmitCount
            );
            Params.MemPool = AppViewMemPool();
            Params.UnitHash = Ctx->Unit->Hash;
            Params.Flight = Ctx->Keys[Ctx->Flight];
            Params.Key = NodeIndex;
            Params.Metrics = FLAPGraphMetricsFromFontSize(
                UIHeadFontSize()
            );
            FLAPGraph(&Params, Rect);
        } else if (IsTable) {
            FLAPTableParams Params = {};

            Params.Acc = Ctx->Acc;
            Params.FLI = Ctx->FLI;
            Params.Program = Program;
            Params.Items = Items;
            Params.Percents = FLAPTablePercents(
                Ctx,
                NodeIndex,
                Items[TableEmit].Table.ColumnCount + 1
            );
            Params.Scroll = &State->Scroll[NodeIndex];
            Params.Cursor = &State->Cursor[NodeIndex];
            Params.Mark = &State->Mark[NodeIndex];
            Params.Flight = Ctx->Keys[Ctx->Flight];
            Params.HeadEmit = TableEmit;
            Params.Key = NodeIndex;

            // NOTE(nc): the hot table is the one that the mouse was
            //         : last over. it is the first table until the
            //         : mouse is over one.
            if (!State->HasHotTable || InRange(Box->Rect, UIMouse())) {
                State->HotTable = NodeIndex;
                State->HasHotTable = TRUE;
            }

            if (State->TableExportKind && State->HotTable == NodeIndex) {
                TempArena Scratch = GetScratch(NULL, 0);
                FLAPTableCtx TableCtx = {};

                // NOTE(nc): the export reads all the rows, so all the
                //         : columns must be loaded. if one is not, the
                //         : export waits for the next frame.
                if (
                    FLAPTableCtxFromParams(
                        Scratch.MemPool,
                        &Params,
                        &TableCtx
                    )
                ) {
                    FLAPExportSource Source = {};

                    Source.Table = &Params;
                    FLAPExport(
                        State->TableExportKind,
                        &Source,
                        FLAPExportPath(
                            Scratch.MemPool,
                            Ctx,
                            NodeIndex,
                            State->TableExportKind
                        )
                    );
                    State->TableExportKind = FLAP_EXPORT_KIND_NULL;
                } else {
                    State->IsTableExportWaiting = TRUE;
                    AppRequestFrame();
                }

                ReleaseScratch(Scratch);
            }

            FLAPTable(&Params, Rect);
        } else if (FlagCount) {
            FLAPFlagListParams Params = {};

            Params.Program = Program;
            Params.Items = Items;
            Params.Scroll = &State->Scroll[NodeIndex];
            Params.Cursor = &State->Cursor[NodeIndex];
            Params.Mark = &State->Mark[NodeIndex];
            FLAPFlagList(&Params, Rect);
        }
    }
}

INTERNAL Str8
FLAPLayoutKey(FLAPViewTabCtx* Ctx, u32 NodeIndex, Str8 ParentKey)
{
    FLVLayoutNode const* Node = &Ctx->Unit->Lowered.Layout[NodeIndex];
    Str8 Name = Ctx->Unit->LayoutNames[NodeIndex];
    Str8 Result = {};

    if (!Name.Size)
        Name = ArenaPushStrFmt(UIBuildMemPool(), "%u", Node->Ordinal);

    Result = ParentKey.Size
        ? ArenaPushStrFmt(
            UIBuildMemPool(),
            "%S.%S",
            PRINT_STR(ParentKey),
            PRINT_STR(Name)
        )
        : Name;

    return Result;
}

INTERNAL void
FLAPOverrideStore(ConfigNode* View, Str8 Key, f32 PercentOfParent)
{
    ConfigNode* Overrides = ConfigNodeChildFromStrOrAlloc(
        APP_STATE->Config,
        View,
        "overrides"_s8
    );
    ConfigNode* Node = ConfigNodeChildFromStrOrAlloc(
        APP_STATE->Config,
        Overrides,
        Key
    );

    ConfigNodeNewReplace(APP_STATE->Config, Node, "%f", PercentOfParent);
}

INTERNAL FLAPLayoutRecord
FLAPLayoutRecDepthFirstPre(FLVLayoutNode const* Layout, u32 NodeIndex)
{
    FLAPLayoutRecord Result = {};
    FLVLayoutNode const* Node = &Layout[NodeIndex];

    Result.Next = U32_MAX;

    if (Node->AnalysisIndex == U32_MAX && Node->ChildCount) {
        Result.Next = Node->HeadChild;
        ++Result.PushCount;
    } else {
        for (
            u32 P = NodeIndex; 
            P != 0; 
            P = Layout[P].Parent, ++Result.PopCount
        ) {
            FLVLayoutNode const* Parent = &Layout[Layout[P].Parent];

            if (P + 1 < Parent->HeadChild + Parent->ChildCount) {
                Result.Next = P + 1;
                break;
            }
        }
    }

    return Result;
}

internal Str8
FLAPLayoutShape(Arena* MemPool, FLVUnit const* Unit) {
    FLVLayoutNode const* Layout = Unit->Lowered.Layout;
    Str8 Result = {};

    for (
        u32 Index = 0;
        Index != U32_MAX;
        Index = FLAPLayoutRecDepthFirstPre(Layout, Index).Next
    ) {
        FLVLayoutNode const* Node = &Layout[Index];

        if (Node->AnalysisIndex != U32_MAX)
            continue;

        Result = Result.Size
            ? ArenaPushStrFmt(
                MemPool,
                "%S.%u",
                PRINT_STR(Result),
                Node->ChildCount
            )
            : ArenaPushStrFmt(MemPool, "%u", Node->ChildCount);
    }

    return Result;
}

internal void
FLAPLayoutUI(FLAPViewTabCtx* Ctx, r2f32 Rect) {
    TempArena Scratch = GetScratch(NULL, 0);
    FLVLayoutNode const* Layout = Ctx->Unit->Lowered.Layout;
    ConfigNode* Overrides = ConfigNodeChildFromStr(Ctx->View, "overrides"_s8);
    FLAPLayoutTask* Tasks = ArenaPushArrayZero(
        Scratch.MemPool,
        FLAPLayoutTask,
        Ctx->Unit->Lowered.LayoutCount
    );

    Tasks[0].Rect = Rect;
    Tasks[0].PercentOfParent = 1.0f;

    f32 Boundary = UIHeadFontSize();
    f32 PaneMin = 3.0f * Boundary;

    for (u32 Index = 0; Index != U32_MAX; ) {
        FLVLayoutNode const* Node = &Layout[Index];
        FLAPLayoutTask* Task = &Tasks[Index];
        FLAPLayoutRecord Rec = FLAPLayoutRecDepthFirstPre(Layout, Index);

        if (Index) {
            FLVLayoutNode const* Parent = &Layout[Node->Parent];
            FLAPLayoutTask* ParentTask = &Tasks[Node->Parent];
            Axis2D ParentAxis = (Parent->Depth & 1) ? AXIS_2D_X : AXIS_2D_Y;

            if (Index != Parent->HeadChild) {
                FLAPLayoutTask* PrevTask = Task - 1;
                f32 TotalSize = ParentTask->ChildrenSize;
                UIBox* BoundaryBox = EMPTY_UI_BOX_VALUE;

                UITag("menu_bar"_s8) {
                    UISetNextPreferredSize(
                        ParentAxis, 
                        UI_PX(Boundary, 1.0f)
                    );
                    UISetNextPreferredSize(
                        FLIP_AXIS(ParentAxis),
                        UI_PERCENT(1.0f, 0.0f)
                    );
                    UISetNextHoverCursor(
                        (ParentAxis == AXIS_2D_X)
                            ? CURSOR_KIND_LEFT_RIGHT
                            : CURSOR_KIND_UP_DOWN
                    );
                    BoundaryBox = UIBuildBoxFromStrFmt(
                        (
                            UI_BOX_KIND_MOUSE_CLICKABLE |
                            UI_BOX_KIND_DRAW_BACKGROUND |
                            UI_BOX_KIND_DRAW_HOT_EFFECTS |
                            UI_BOX_KIND_DRAW_ACTIVE_EFFECTS
                        ),
                        "###layout_boundary_%u",
                        Index
                    );
                }

                UISignal Sig = UISignalFromBox(BoundaryBox);

                if (UI_PRESSED(Sig)) {
                    v2f32 Percents = Vec(
                        PrevTask->PercentOfParent, 
                        Task->PercentOfParent
                    );

                    UIStoreDragStruct(&Percents);
                } else if (UI_DRAGGING(Sig)) {
                    v2f32 Percents = *UIGetDragStruct(v2f32);
                    f32 BothSize = (Percents.X + Percents.Y) * TotalSize;

                    if (BothSize >= 2.0f * PaneMin) {
                        f32 MinSize = CLAMP(
                            PaneMin,
                            Percents.X * TotalSize + UIDragDelta().V[ParentAxis],
                            BothSize - PaneMin
                        );

                        FLAPOverrideStore(
                            Ctx->View,
                            PrevTask->Key,
                            MinSize / TotalSize
                        );
                        FLAPOverrideStore(
                            Ctx->View,
                            Task->Key,
                            (BothSize - MinSize) / TotalSize
                        );
                    }
                }
            }

            UISetNextPreferredSize(
                ParentAxis,
                UI_PX(Length(Task->Rect).V[ParentAxis], 0.0f)
            );
            UISetNextPreferredSize(
                FLIP_AXIS(ParentAxis),
                UI_PERCENT(1.0f, 0.0f)
            );
        }

        if (Node->AnalysisIndex != U32_MAX) {
            FLAPAnalysisUI(Ctx, Index, Task->Rect);
        } else {
            Axis2D Axis = (Node->Depth & 1) ? AXIS_2D_X : AXIS_2D_Y;
            u32 Count = Node->ChildCount;
            f32 Remaining = 1.0f;
            f32 Offset = 0.0f;
            u32 FreeCount = 0;

            Task->ChildrenSize = Length(Task->Rect).V[Axis];

            if (Count)
                Task->ChildrenSize -= (f32) (Count - 1) * Boundary;

            for (u32 Child = 0; Child < Count; ++Child) {
                FLAPLayoutTask* ChildTask = &Tasks[Node->HeadChild + Child];

                ChildTask->Key = FLAPLayoutKey(
                    Ctx,
                    Node->HeadChild + Child,
                    Task->Key
                );

                Str8 Value = ConfigNodeChildFromStr(
                    Overrides,
                    ChildTask->Key
                )->Head->String;

                if (Value.Size) {
                    ChildTask->PercentOfParent = (f32) F64FromStr(Value);
                    Remaining -= ChildTask->PercentOfParent;
                } else {
                    ++FreeCount;
                }
            }

            Remaining = CLAMP_BOT(0.0f, Remaining);

            for (u32 Child = 0; Child < Count; ++Child) {
                FLAPLayoutTask* ChildTask = &Tasks[Node->HeadChild + Child];

                if (ChildTask->PercentOfParent == 0.0f)
                    ChildTask->PercentOfParent = Remaining / (f32) FreeCount;

                ChildTask->Rect = Task->Rect;
                ChildTask->Rect.Point0.V[Axis] = RoundF32(
                    Task->Rect.Point0.V[Axis] + Offset
                );
                Offset += ChildTask->PercentOfParent * Task->ChildrenSize;
                ChildTask->Rect.Point1.V[Axis] = RoundF32(
                    Task->Rect.Point0.V[Axis] + Offset
                );
                Offset += Boundary;
            }

            UISetNextChildLayoutAxis(Axis);

            UIBox* Box = UIBuildBoxFromStrFmt(0, "###layout_%u", Index);

            if (Rec.PushCount)
                UIPushParent(Box);
        }

        for (i32 Pop = 0; Pop < Rec.PopCount; ++Pop)
            UIPopParent();

        Index = Rec.Next;
    }

    ReleaseScratch(Scratch);
}

INTERNAL void
FLAPGraphCaptureExport(
    Arena* MemPool,
    FLAPViewTabCtx* Ctx,
    RendCapture* Capture
) {
    FLAPGraphState* Graph = &Ctx->State->Graph;

    if (Graph->CaptureKind == FLAP_GRAPH_CAPTURE_KIND_CLIPBOARD) {
        SetClipboardImage(Capture->Size, Capture->Pixels);
    } else if (Graph->HotKey < Ctx->Unit->Lowered.LayoutCount) {
        FLAPExportSource Source = {};

        Source.Pixels = Capture->Pixels;
        Source.Size = Capture->Size;
        FLAPExport(
            FLAP_EXPORT_KIND_PNG,
            &Source,
            FLAPExportPath(
                MemPool,
                Ctx,
                Graph->HotKey,
                FLAP_EXPORT_KIND_PNG
            )
        );
    }
}

APP_VIEW_UI_FUNC_DEF(FLVView)
{
    UNUSED(Eval);

    TempArena Scratch = GetScratch(NULL, 0);
    FLAPViewTabState* State = GetAppViewState(FLAPViewTabState);
    Access* Acc = APP_STATE->FrameAccess;
    Str8 Path = AppViewFilePath();
    FLAPFlightSet Flights = FLAPFlightSetFromConfig(Scratch.MemPool);
    ConfigNodePtrList SearchPathList = ConfigNodeTopLevelListFromStr(
        Scratch.MemPool,
        "flv_search_path"_s8
    );
    ConfigNodePtrArray SearchPathNodes = ConfigNodePtrArrayFromList(
        Scratch.MemPool,
        &SearchPathList
    );
    Str8* SearchPaths = ArenaPushArray(
        Scratch.MemPool,
        Str8,
        SearchPathNodes.Count
    );

    for (u64 Index = 0; Index < SearchPathNodes.Count; ++Index)
        SearchPaths[Index] = SearchPathNodes.V[Index]->Head->String;

    FLVUnit* Unit = FLVUnitFromPath(
        Acc,
        Path,
        Flights.Keys.V,
        Flights.Keys.Count,
        SearchPaths,
        SearchPathNodes.Count,
        0
    );
    Str8 Message = ""_s8;
 
    if (!Unit) {
        Message = "compiling..."_s8;
        AppRequestFrame();
    } else if (Unit->HasError) {
        FLAPErrorListParams Params = {};
        UIScrollPoint2D ScrollPosition = AppViewScrollPosition();

        Params.Errors = &Unit->Errors;
        Params.Scroll = &ScrollPosition.Y;
        Params.Cursor = &State->ErrorCursor;
        Params.Mark = &State->ErrorMark;
        FLAPErrorList(&Params, Rect);
        AppStoreViewScrollPosition(ScrollPosition);
    } else if (!Flights.Keys.Count) {
        Message = "There are no flights in the list."_s8;
    } else if (Flights.Selected == U32_MAX) {
        Message = "No flight is selected."_s8;
    } else {
        FLIParsed* FLI = FLIParsedFromKey(
            Acc,
            Flights.Keys.V[Flights.Selected],
            FALSE,
            0
        );
 
        if (FLI == EMPTY_FLI_PARSED_VALUE) {
            Message = "loading flight..."_s8;
            AppRequestFrame();
        } else {
            FLAPViewTabCtx Ctx = {};
            ConfigNode* View = ConfigNodeFromID(Registers()->View);

            if (State->UnitHash != Unit->Hash) {
                Str8 Shape = FLAPLayoutShape(Scratch.MemPool, Unit);
                ConfigNode* ShapeNode = ConfigNodeChildFromStrOrAlloc(
                    APP_STATE->Config,
                    View,
                    "shape"_s8
                );
                ConfigNode* Overrides = ConfigNodeChildFromStr(
                    View,
                    "overrides"_s8
                );

                if (!StrMatch(ShapeNode->Head->String, Shape, 0)) {
                    if (Overrides != EMPTY_CFG_NODE_VALUE) {
                        ConfigNodeReleaseAllChildren(
                            APP_STATE->Config,
                            Overrides
                        );
                    }

                    ConfigNodeNewReplace(APP_STATE->Config, ShapeNode, Shape);
                }

                State->Graph.HasHotKey = FALSE;
                State->HasHotTable = FALSE;
                State->UnitHash = Unit->Hash;
            }

            for (u32 Index = 0; Index < Unit->Lowered.LayoutCount; ++Index) {

                UIScrollPoint* Scroll = &State->Scroll[Index];
                f32 Diff = -Scroll->Offset;

                Scroll->Offset += Diff * APP_STATE->ScrollingAnimationRate;

                if (AbsF32(Diff) > 0.01f)
                    AppRequestFrame();
                else
                    Scroll->Offset = 0.0f;
            }
 
            Ctx.Acc = Acc;
            Ctx.Unit = Unit;
            Ctx.Keys = Flights.Keys.V;
            Ctx.KeysCount = Flights.Keys.Count;
            Ctx.Flight = Flights.Selected;
            Ctx.FLI = FLI;
            Ctx.State = State;
            Ctx.View = View;

            AppWindowState* WindowState = AppWindowStateFromConfig(
                AppWindowFromConfig(View)
            );

            // NOTE(nc): the frame before this one was drawn for a capture,
            //         : and the renderer made the copy at its end.
            if (State->Graph.IsCaptureRequested) {
                RendCapture Capture = {};

                if (
                    RendTakeWindowCapture(
                        WindowState->RendHandle,
                        Scratch.MemPool,
                        &Capture
                    )
                ) {
                    FLAPGraphCaptureExport(Scratch.MemPool, &Ctx, &Capture);
                }

                State->Graph.CaptureKind = FLAP_GRAPH_CAPTURE_KIND_NULL;
                State->Graph.IsCaptureRequested = FALSE;
            }

            for (AppCommand* Cmd = NULL; AppNextViewCommand(&Cmd); ) {
                AppCommandKind Kind = AppCommandKindFromStr(Cmd->Name);

                switch (Kind) {
                    default: {} break;

                    case APP_COMMAND_KIND_EXPORT_GRAPH_PNG: {
                        State->Graph.CaptureKind = FLAP_GRAPH_CAPTURE_KIND_FILE;
                    } break;

                    case APP_COMMAND_KIND_COPY_GRAPH_IMAGE: {
                        State->Graph.CaptureKind = FLAP_GRAPH_CAPTURE_KIND_CLIPBOARD;
                    } break;

                    case APP_COMMAND_KIND_EXPORT_TABLE_CSV: {
                        State->TableExportKind = FLAP_EXPORT_KIND_CSV;
                    } break;

                    case APP_COMMAND_KIND_EXPORT_TABLE_TXT: {
                        State->TableExportKind = FLAP_EXPORT_KIND_TXT;
                    } break;
                }
            }

            State->IsTableExportWaiting = FALSE;

            UIFocus(UI_FOCUS_KIND_ON) {
                if (UIIsFocusActive() && UIText('z'))
                    State->Graph.IsFitted = FALSE;
            }

            if (State->IsFitRequested) {
                State->Graph.IsFitted = FALSE;
                State->IsFitRequested = FALSE;
            }

            if (!State->Graph.IsFitted) {
                State->Graph.FitRangeUSecs = {};
                State->Graph.IsFitReady = TRUE;
            }
 
            UISetNextPreferredSize(AXIS_2D_X, UI_PERCENT(1.0f, 0.0f));
            UISetNextPreferredSize(AXIS_2D_Y, UI_PERCENT(1.0f, 0.0f));
            FLAPLayoutUI(&Ctx, Rect);

            // NOTE(nc): no table took the export in this frame and no
            //         : table waits for data, so the command is dropped.
            if (!State->IsTableExportWaiting)
                State->TableExportKind = FLAP_EXPORT_KIND_NULL;

            // NOTE(nc): the hot graph asked for a capture in this frame. if
            //         : no graph did, the command is dropped.
            if (State->Graph.IsCaptureRequested) {
                r2i32 CaptureRect = {};

                CaptureRect.X0 = (i32) FloorF32(State->Graph.CaptureRect.X0);
                CaptureRect.Y0 = (i32) FloorF32(State->Graph.CaptureRect.Y0);
                CaptureRect.X1 = (i32) CeilF32(State->Graph.CaptureRect.X1);
                CaptureRect.Y1 = (i32) CeilF32(State->Graph.CaptureRect.Y1);
                RendRequestWindowCapture(WindowState->RendHandle, CaptureRect);
            } else {
                State->Graph.CaptureKind = FLAP_GRAPH_CAPTURE_KIND_NULL;
            }

            if (
                !State->Graph.IsFitted &&
                State->Graph.IsFitReady &&
                State->Graph.FitRangeUSecs.Min < State->Graph.FitRangeUSecs.Max
            ) {
                State->Graph.TimeRangeUSecs = State->Graph.FitRangeUSecs;
                State->Graph.IsFitted = TRUE;
                AppRequestFrame();
            }

            if (
                State->Graph.HasCursor != State->Graph.HasNextCursor ||
                State->Graph.CursorTimeUSecs != State->Graph.NextCursorTimeUSecs
            ) {
                AppRequestFrame();
            }

            State->Graph.HasCursor = State->Graph.HasNextCursor;
            State->Graph.CursorTimeUSecs = State->Graph.NextCursorTimeUSecs;
            State->Graph.HasNextCursor = FALSE;
        }
    }
 
    AppStoreViewLoadingInfo(!Unit, 0, 0);
 
    if (Message.Size) {
        UIPadding(UI_PERCENT(1.0f, 0.0f)) {
            UIWidthFill() {
                UITextAlignment(UI_TEXT_ALIGN_CENTRE) {
                    UITag("weak"_s8) {
                        UILabel(Message);
                    }
                }
            }
        }
    }
 
    ReleaseScratch(Scratch);
}

INTERNAL void
FLAPExplorerCollapseStore(ConfigNode* View, Str8 Key, b32 IsCollapsed)
{
    ConfigNode* Collapsed = ConfigNodeChildFromStrOrAlloc(
        APP_STATE->Config,
        View,
        "collapsed"_s8
    );
    ConfigNode* Node = ConfigNodeChildFromStr(Collapsed, Key);

    if (IsCollapsed && Node == EMPTY_CFG_NODE_VALUE)
        ConfigNodeNew(APP_STATE->Config, Collapsed, Key);
    else if (!IsCollapsed && Node != EMPTY_CFG_NODE_VALUE)
        ConfigNodeRelease(APP_STATE->Config, Node);
}

INTERNAL void
FLAPExplorerCollapseToggle(ConfigNode* View, Str8 Key)
{
    ConfigNode* Collapsed = ConfigNodeChildFromStr(View, "collapsed"_s8);
    ConfigNode* Node = ConfigNodeChildFromStr(Collapsed, Key);

    FLAPExplorerCollapseStore(View, Key, Node == EMPTY_CFG_NODE_VALUE);
}

INTERNAL void
FLAPExplorerPushList(
    Arena* MemPool,
    FLAPExplorerRowArray* Rows,
    ConfigNode* Collapsed,
    ConfigNodePtrArray All,
    Str8 ListName,
    Str8 Label,
    Str8 Filter,
    b32 IsFlights
) {
    ConfigNode** Files = ArenaPushArray(MemPool, ConfigNode*, All.Count);
    ConfigNode** Sorted = ArenaPushArray(MemPool, ConfigNode*, All.Count);
    u32* FolderOf = ArenaPushArray(MemPool, u32, All.Count);
    FLAPExplorerFolder* Folders = ArenaPushArrayZero(
        MemPool,
        FLAPExplorerFolder,
        All.Count
    );
    u32 Count = 0;
    u32 FoldersCount = 0;
    u32 Last = 0;
    u32 Offset = 0;
    u32 Checked = 0;

    for (u64 Index = 0; Index < All.Count; ++Index) {
        Str8 Path = All.V[Index]->Head->String;

        if (Filter.Size) {
            FMRangeList Matches = FuzzyFind(MemPool, Filter, Path);

            if (Matches.Count != Matches.StrPartCount)
                continue;
        }

        Str8 Folder = StrChopLastSlash(Path);
        u32 FolderIndex = Last;

        if (!FoldersCount || !StrMatch(Folders[Last].Path, Folder, 0)) {
            for (FolderIndex = 0; FolderIndex < FoldersCount; ++FolderIndex) {
                if (StrMatch(Folders[FolderIndex].Path, Folder, 0))
                    break;
            }

            if (FolderIndex == FoldersCount)
                Folders[FoldersCount++].Path = Folder;
        }

        Last = FolderIndex;
        ++Folders[FolderIndex].Count;
        Files[Count] = All.V[Index];
        FolderOf[Count] = FolderIndex;
        ++Count;
    }

    for (u32 Index = 0; Index < FoldersCount; ++Index) {
        Folders[Index].First = Offset;
        Offset += Folders[Index].Count;
        Folders[Index].Count = 0;
    }

    for (u32 Index = 0; Index < Count; ++Index) {
        FLAPExplorerFolder* Folder = &Folders[FolderOf[Index]];
        b32 IsChecked = IsFlights && FLAPFlightIsChecked(Files[Index]);

        Sorted[Folder->First + Folder->Count++] = Files[Index];
        Folder->Checked += IsChecked;
        Checked += IsChecked;
    }

    FLAPExplorerRow* Root = &Rows->V[Rows->Count++];

    Root->Files = Sorted;
    Root->Label = Label;
    Root->CollapseKey = ListName;
    Root->FilesCount = Count;
    Root->CheckedCount = Checked;
    Root->Kind = FLAP_EXPLORER_ROW_KIND_ROOT;
    Root->Depth = 0;
    Root->IsFlights = IsFlights;
    Root->IsExpanded = (
        Filter.Size ||
        ConfigNodeChildFromStr(Collapsed, ListName) == EMPTY_CFG_NODE_VALUE
    );

    if (!Root->IsExpanded)
        return;

    // NOTE(nc): a list whose files are all in one folder does not get a
    //         : folder row. the files sit directly under the header.
    b32 HasFolderRows = (FoldersCount > 1);

    for (u32 Index = 0; Index < FoldersCount; ++Index) {
        FLAPExplorerFolder* Folder = &Folders[Index];

        if (HasFolderRows) {
            FLAPExplorerRow* Row = &Rows->V[Rows->Count++];

            Row->Files = Sorted + Folder->First;
            Row->Label = StrSkipLastSlash(Folder->Path);
            Row->CollapseKey = ArenaPushStrFmt(
                MemPool,
                "%S:%S",
                PRINT_STR(ListName),
                PRINT_STR(Folder->Path)
            );
            Row->FilesCount = Folder->Count;
            Row->CheckedCount = Folder->Checked;
            Row->Kind = FLAP_EXPLORER_ROW_KIND_FOLDER;
            Row->Depth = 1;
            Row->IsFlights = IsFlights;
            Row->IsExpanded = (
                Filter.Size ||
                ConfigNodeChildFromStr(
                    Collapsed,
                    Row->CollapseKey
                ) == EMPTY_CFG_NODE_VALUE
            );

            if (!Row->IsExpanded)
                continue;
        }

        for (u32 File = 0; File < Folder->Count; ++File) {
            FLAPExplorerRow* FileRow = &Rows->V[Rows->Count++];

            FileRow->Files = Sorted + Folder->First + File;
            FileRow->Label = StrSkipLastSlash(
                FileRow->Files[0]->Head->String
            );
            FileRow->FilesCount = 1;
            FileRow->CheckedCount = (
                IsFlights && FLAPFlightIsChecked(FileRow->Files[0])
            );
            FileRow->Kind = FLAP_EXPLORER_ROW_KIND_FILE;
            FileRow->Depth = HasFolderRows ? 2 : 1;
            FileRow->IsFlights = IsFlights;
        }
    }
}

APP_VIEW_UI_FUNC_DEF(Explorer)
{
    UNUSED(Eval);

    TempArena Scratch = GetScratch(NULL, 0);
    ConfigNode* View = ConfigNodeFromID(Registers()->View);
    AppViewState* VS = AppViewStateFromConfig(View);
    ConfigNode* QueryRoot = ConfigNodeChildFromStr(View, "query"_s8);
    ConfigNode* CmdRoot = ConfigNodeChildFromStr(QueryRoot, "cmd"_s8);
    ConfigNode* Collapsed = ConfigNodeChildFromStr(View, "collapsed"_s8);
    FLAPExplorerViewState* State = GetAppViewState(FLAPExplorerViewState);
    ConfigNodePtrList FlightList = ConfigNodeTopLevelListFromStr(
        Scratch.MemPool,
        "file"_s8
    );
    ConfigNodePtrList ViewList = ConfigNodeTopLevelListFromStr(
        Scratch.MemPool,
        "flv_file"_s8
    );
    ConfigNodePtrArray Flights = ConfigNodePtrArrayFromList(
        Scratch.MemPool,
        &FlightList
    );
    ConfigNodePtrArray Views = ConfigNodePtrArrayFromList(
        Scratch.MemPool,
        &ViewList
    );
    FLAPExplorerRowArray Rows = {};
    FLAPExplorerRow* ToggleExpand = NULL;
    FLAPExplorerRow* ToggleCheck = NULL;
    FLAPExplorerRow* Accepted = NULL;
    FLAPExplorerRow* Activated = NULL;
    FLAPExplorerRow* Removed = NULL;
    Str8 ShowPath = {};
    UIKey ContextMenuKey = UIKeyFromStrFmt(
        EMPTY_UI_KEY_VALUE,
        "###explorer_context_menu_%p",
        View
    );
    b32 AnyContextMenuIsOpen = UIAnyContextMenuIsOpen();
    UIScrollPoint2D ScrollPosition = AppViewScrollPosition();
    Str8 OpenCommandName = APP_COMMAND_KIND_INFO_TABLE[
        APP_COMMAND_KIND_OPEN
    ].String;
    Str8 FilterCommandName = APP_COMMAND_KIND_INFO_TABLE[
        APP_COMMAND_KIND_FILTER
    ].String;
    Str8 Filter = {};
    b32 AddClicked = FALSE;
    b32 FilterClicked = FALSE;
    b32 CollapseClicked = FALSE;

    if (
        VS->QueryIsOpen &&
        StrMatch(CmdRoot->Head->String, FilterCommandName, 0)
    ) {
        Filter = Str(VS->QueryBuffer, VS->QueryStringSize);
    }

    // NOTE(nc): the active flight is the one that the View tabs show. this
    //         : is the same rule as FLAPFlightSetFromConfig: the transient
    //         : flight if it is checked, or else the first checked flight.
    ConfigNode* ActiveFlight = NULL;

    {
        Str8 ActivePath = ConfigNodeChildFromStr(
            ConfigNodeChildFromStr(ConfigNodeRoot(), "transient"_s8),
            "flight"_s8
        )->Head->String;

        for (u64 Index = 0; Index < Flights.Count; ++Index) {
            if (!FLAPFlightIsChecked(Flights.V[Index]))
                continue;

            if (!ActiveFlight)
                ActiveFlight = Flights.V[Index];

            if (
                ActivePath.Size &&
                PathMatchNormalised(
                    Flights.V[Index]->Head->String,
                    ActivePath
                )
            ) {
                ActiveFlight = Flights.V[Index];
                break;
            }
        }
    }

    // NOTE(nc): two headers, and a list has no more folders than files
    Rows.V = ArenaPushArrayZero(
        Scratch.MemPool,
        FLAPExplorerRow,
        2 * (Flights.Count + Views.Count) + 2
    );
    FLAPExplorerPushList(
        Scratch.MemPool,
        &Rows,
        Collapsed,
        Flights,
        "file"_s8,
        "Flights"_s8,
        Filter,
        TRUE
    );

    u64 ViewsRootIndex = Rows.Count;

    FLAPExplorerPushList(
        Scratch.MemPool,
        &Rows,
        Collapsed,
        Views,
        "flv_file"_s8,
        "Views"_s8,
        Filter,
        FALSE
    );

    b32 AnyRootExpanded = (
        Rows.V[0].IsExpanded ||
        Rows.V[ViewsRootIndex].IsExpanded
    );

    State->Cursor.Y = MIN(State->Cursor.Y, (i64) Rows.Count);
    State->Mark = State->Cursor;

    // NOTE(nc): the view container that this function is called inside of
    //         : lays its children out along X. everything goes in one column
    //         : that is the size of the view.
    f32 ToolbarHeightPX = UIHeadPXHeight();
    f32 RowHeightPX = UIHeadPXHeight();
    v2f32 ListDimensionsPX = Length(Rect);

    ListDimensionsPX.Y = CLAMP_BOT(0.0f, ListDimensionsPX.Y - ToolbarHeightPX);

    UISetNextPreferredWidth(UI_PX(Length(Rect).X, 1.0f));
    UISetNextPreferredHeight(UI_PX(Length(Rect).Y, 1.0f));
    UISetNextChildLayoutAxis(AXIS_2D_Y);

    UIBox* ColumnBox = UIBuildBoxFromStr(0, "###explorer_column"_s8);

    UIParent(ColumnBox) {
        UIWidthFill() {
            // NOTE(nc): toolbar
            UISetNextPreferredHeight(UI_PX(ToolbarHeightPX, 1.0f));
            UISetNextFlags(UI_BOX_KIND_DRAW_SIDE_BOTTOM);

            UINamedRow("###explorer_toolbar"_s8) {
                UISpacer(UI_EM(0.5f, 1.0f));

                AddClicked = FLAPToolButton(
                    APP_ICON_KIND_ADD,
                    "###add_file"_s8,
                    "Add file or folder"_s8,
                    OpenCommandName
                );
                FilterClicked = FLAPToolButton(
                    APP_ICON_KIND_FIND,
                    "###filter"_s8,
                    "Filter"_s8,
                    FilterCommandName
                );
                CollapseClicked = FLAPToolButton(
                    AnyRootExpanded
                        ? APP_ICON_KIND_UP_CARET
                        : APP_ICON_KIND_DOWN_CARET,
                    "###collapse_all"_s8,
                    AnyRootExpanded ? "Collapse all"_s8 : "Expand all"_s8,
                    ""_s8
                );
            }

            if (!Flights.Count && !Views.Count) {
                // NOTE(nc): empty state
                UIPadding(UI_PERCENT(1.0f, 0.0f)) {
                    UITextAlignment(UI_TEXT_ALIGN_CENTRE) {
                        UITag("weak"_s8) {
                            UILabel("No flights or views are loaded."_s8);
                        }

                        UIRow() {
                            UIPadding(UI_PERCENT(1.0f, 0.0f)) {
                                UIPreferredWidth(UI_TEXT_DIM(20.0f, 1.0f)) {
                                    UISignal EmptyAddSig = UIButton(
                                        "Add file or folder###empty_add"_s8
                                    );

                                    if (UI_CLICKED(EmptyAddSig))
                                        AddClicked = TRUE;
                                }
                            }
                        }
                    }
                }
            } else {
                // NOTE(nc): list
                UIScrollListParameters Params = {};
                UIScrollListSignal ListSig = {};
                r1i64 Visible = {};

                Params.Kind = UI_SCROLL_LIST_KIND_ALL;
                Params.DimensionsPX = ListDimensionsPX;
                Params.RowHeightPX = RowHeightPX;
                Params.CursorRange.Max.Y = (i64) Rows.Count;
                Params.ItemRange = Rng((i64) 0, (i64) Rows.Count);
                Params.CursorMinIsEmptySelection[AXIS_2D_Y] = TRUE;

                UIFocus(UI_FOCUS_KIND_ON) {
                    UIScrollList(
                        &Params,
                        &ScrollPosition.Y,
                        &State->Cursor,
                        &State->Mark,
                        &Visible,
                        &ListSig
                    ) {
                        for (
                            i64 Index = Visible.Min;
                            Index <= Visible.Max && Index < (i64) Rows.Count;
                            ++Index
                        ) {
                            FLAPExplorerRow* Row = &Rows.V[Index];
                            b32 IsFile = (
                                Row->Kind == FLAP_EXPLORER_ROW_KIND_FILE
                            );
                            b32 IsHeader = (
                                Row->Kind == FLAP_EXPLORER_ROW_KIND_ROOT
                            );
                            b32 IsCursor = (State->Cursor.Y == Index + 1);
                            b32 IsActive = (
                                IsFile &&
                                Row->IsFlights &&
                                Row->Files[0] == ActiveFlight
                            );
                            b32 IsOpen = FALSE;
                            UIBoxKind RowFlags = (
                                UI_BOX_KIND_MOUSE_CLICKABLE |
                                UI_BOX_KIND_DISABLE_FOCUS_BORDER
                            );

                            if (IsFile && !Row->IsFlights) {
                                for (
                                    Str8Node* N = APP_STATE->TabFilePaths.Head;
                                    N && !IsOpen;
                                    N = N->Next
                                ) {
                                    IsOpen = PathMatchNormalised(
                                        N->String,
                                        Row->Files[0]->Head->String
                                    );
                                }
                            }

                            // NOTE(nc): a hairline between the two sections
                            if (IsHeader && !Row->IsFlights)
                                RowFlags |= UI_BOX_KIND_DRAW_SIDE_TOP;

                            // NOTE(nc): only the cursor row is focus hot, so
                            //         : only the cursor row gets the focus
                            //         : overlay. the same as the watch rows.
                            UISetNextFocusHot(
                                IsCursor
                                    ? UI_FOCUS_KIND_ON
                                    : UI_FOCUS_KIND_OFF
                            );
                            UISetNextChildLayoutAxis(AXIS_2D_X);

                            UIBox* RowBox = UIBuildBoxFromStrFmt(
                                RowFlags,
                                "###explorer_row_%u_%u_%p",
                                Row->Kind,
                                Row->IsFlights,
                                Row->FilesCount ? Row->Files[0] : NULL
                            );

                            // NOTE(nc): the rect is from the last frame. the
                            //         : hot key is not used for this, since
                            //         : the mouse on the remove button would
                            //         : make the row not hot, and that would
                            //         : take the remove button away.
                            b32 ShowRemove = (
                                !IsHeader &&
                                !AnyContextMenuIsOpen &&
                                InRange(RowBox->Rect, UIMouse())
                            );

                            UIParent(RowBox) {
                                // NOTE(nc): nothing inside a row is focus
                                //         : hot. the row is the selection.
                                UIFocus(UI_FOCUS_KIND_OFF) {
                                    UITextPadding(0.0f) {
                                        UISpacer(
                                            UI_EM(
                                                0.5f + 1.5f * (f32) Row->Depth,
                                                1.0f
                                            )
                                        );

                                        // NOTE(nc): expander slot. a file
                                        //         : keeps the slot so that
                                        //         : it sits under its folder.
                                        if (!IsFile) {
                                            UIPreferredWidth(
                                                UI_EM(1.5f, 1.0f)
                                            ) {
                                                UISignal ExpanderSig = UIExpander(
                                                    Row->IsExpanded,
                                                    "###expander"_s8
                                                );

                                                if (UI_PRESSED(ExpanderSig))
                                                    ToggleExpand = Row;
                                            }
                                        } else {
                                            UISpacer(UI_EM(1.5f, 1.0f));
                                        }

                                        // NOTE(nc): checkbox slot
                                        if (Row->IsFlights) {
                                            AppIconKind Icon = !Row->CheckedCount
                                                ? APP_ICON_KIND_CHECK_HOLLOW
                                                : (Row->CheckedCount == Row->FilesCount)
                                                    ? APP_ICON_KIND_CHECK_FILLED
                                                    : APP_ICON_KIND_CHECK_PARTIAL;
                                            UIBox* CheckBox = EMPTY_UI_BOX_VALUE;

                                            UIPreferredWidth(
                                                UI_EM(2.0f, 1.0f)
                                            ) {
                                                UISetNextHoverCursor(
                                                    CURSOR_KIND_HAND_POINT
                                                );
                                                UISetNextTextAlignment(
                                                    UI_TEXT_ALIGN_CENTRE
                                                );

                                                AppFont(APP_FONT_SLOT_ICONS) {
                                                    CheckBox = UIBuildBoxFromStr(
                                                        (
                                                            UI_BOX_KIND_MOUSE_CLICKABLE |
                                                            UI_BOX_KIND_DRAW_TEXT
                                                        ),
                                                        "###check"_s8
                                                    );
                                                }
                                            }

                                            UIBoxEquipDisplayStr(
                                                CheckBox,
                                                APP_ICON_KIND_TEXT_TABLE[Icon]
                                            );

                                            UISignal CheckSig = UISignalFromBox(
                                                CheckBox
                                            );

                                            if (UI_PRESSED(CheckSig))
                                                ToggleCheck = Row;

                                            UISpacer(UI_EM(0.25f, 1.0f));
                                        }

                                        // NOTE(nc): label. folders, and
                                        //         : flights that are not
                                        //         : checked, are weak.
                                        b32 IsWeak = (
                                            Row->Kind == FLAP_EXPLORER_ROW_KIND_FOLDER ||
                                            (
                                                IsFile &&
                                                Row->IsFlights &&
                                                !Row->CheckedCount
                                            )
                                        );

                                        UITag(IsWeak ? "weak"_s8 : ""_s8) {
                                            UIWidthFill() {
                                                UIBox* LabelBox = UILabel(
                                                    Row->Label
                                                ).Box;

                                                if (Filter.Size && IsFile) {
                                                    FMRangeList Matches = FuzzyFind(
                                                        Scratch.MemPool,
                                                        Filter,
                                                        Row->Label
                                                    );

                                                    UIBoxEquipFuzzyMatchRanges(
                                                        LabelBox,
                                                        &Matches
                                                    );
                                                }
                                            }
                                        }

                                        // NOTE(nc): status, on the right
                                        Str8 Status = {};

                                        if (IsHeader && Row->IsFlights) {
                                            Status = ArenaPushStrFmt(
                                                Scratch.MemPool,
                                                "%u of %u",
                                                Row->CheckedCount,
                                                Row->FilesCount
                                            );
                                        } else if (IsHeader) {
                                            Status = ArenaPushStrFmt(
                                                Scratch.MemPool,
                                                "%u",
                                                Row->FilesCount
                                            );
                                        } else if (IsActive) {
                                            Status = "active"_s8;
                                        } else if (IsOpen) {
                                            Status = "open"_s8;
                                        }

                                        if (Status.Size) {
                                            UITag("weak"_s8) {
                                                UIPreferredWidth(
                                                    UI_TEXT_DIM(0.0f, 1.0f)
                                                ) {
                                                    UILabel(Status);
                                                }
                                            }
                                        }

                                        // NOTE(nc): remove slot. every row
                                        //         : keeps the slot so that
                                        //         : the status does not move
                                        //         : when the button shows.
                                        UISpacer(UI_EM(0.25f, 1.0f));

                                        if (ShowRemove) {
                                            UIBox* RemoveBox = EMPTY_UI_BOX_VALUE;

                                            UIPreferredWidth(
                                                UI_EM(2.0f, 1.0f)
                                            ) {
                                                UISetNextHoverCursor(
                                                    CURSOR_KIND_HAND_POINT
                                                );
                                                UISetNextTextAlignment(
                                                    UI_TEXT_ALIGN_CENTRE
                                                );

                                                UITag("weak"_s8) {
                                                    AppFont(APP_FONT_SLOT_ICONS) {
                                                        RemoveBox = UIBuildBoxFromStr(
                                                            (
                                                                UI_BOX_KIND_MOUSE_CLICKABLE |
                                                                UI_BOX_KIND_DRAW_TEXT
                                                            ),
                                                            "###remove"_s8
                                                        );
                                                    }
                                                }
                                            }

                                            UIBoxEquipDisplayStr(
                                                RemoveBox,
                                                APP_ICON_KIND_TEXT_TABLE[APP_ICON_KIND_X]
                                            );

                                            UISignal RemoveSig = UISignalFromBox(
                                                RemoveBox
                                            );

                                            if (UI_CLICKED(RemoveSig))
                                                Removed = Row;
                                        } else {
                                            UISpacer(UI_EM(2.0f, 1.0f));
                                        }

                                        UISpacer(UI_EM(0.25f, 1.0f));
                                    }
                                }
                            }

                            UISignal RowSig = UISignalFromBox(RowBox);

                            // NOTE(nc): the context menu is for the cursor
                            //         : row, so a right click moves the
                            //         : cursor. it does not change the active
                            //         : flight.
                            if (UI_RIGHT_CLICKED(RowSig)) {
                                State->Cursor.Y = Index + 1;
                                State->Mark = State->Cursor;
                                UIContextMenuOpen(
                                    ContextMenuKey,
                                    RowBox->Key,
                                    Vec(
                                        UIMouse().X - RowBox->Rect.X0,
                                        UIMouse().Y - RowBox->Rect.Y0
                                    )
                                );
                            }

                            // NOTE(nc): hover. the row has no background
                            //         : unless the mouse is on it.
                            if (UI_HOVERING(RowSig) && !IsCursor) {
                                RowBox->Kind |= (
                                    UI_BOX_KIND_DRAW_BACKGROUND |
                                    UI_BOX_KIND_DRAW_HOT_EFFECTS
                                );
                            }

                            if (UI_PRESSED(RowSig)) {
                                State->Cursor.Y = Index + 1;
                                State->Mark = State->Cursor;

                                // NOTE(nc): a click on a flight makes it the
                                //         : active flight.
                                if (IsFile && Row->IsFlights)
                                    Activated = Row;
                            }

                            if (UI_DOUBLE_CLICKED(RowSig))
                                Accepted = Row;
                        }
                    }

                    if (
                        UIIsFocusActive() &&
                        State->Cursor.Y > 0 &&
                        UISlotPress(UI_EVENT_ACTION_SLOT_ACCEPT)
                    ) {
                        Accepted = &Rows.V[State->Cursor.Y - 1];
                    }
                }
            }
        }
    }

    // NOTE(nc): context menu, for the cursor row
    UIContextMenu(ContextMenuKey) {
        if (State->Cursor.Y <= 0) {
            UIContextMenuClose();
        } else {
            FLAPExplorerRow* Row = &Rows.V[State->Cursor.Y - 1];
            b32 IsFile = (Row->Kind == FLAP_EXPLORER_ROW_KIND_FILE);
            b32 IsHeader = (Row->Kind == FLAP_EXPLORER_ROW_KIND_ROOT);
            b32 Done = FALSE;

            UIPreferredWidth(UI_EM(22.0f, 1.0f)) {
                if (IsFile) {
                    UISignal AcceptSig = AppIconButton(
                        Row->IsFlights
                            ? APP_ICON_KIND_CHECK
                            : APP_ICON_KIND_FILE_OUTLINE,
                        NULL,
                        Row->IsFlights
                            ? "Make active###accept"_s8
                            : "Open###accept"_s8
                    );

                    if (UI_CLICKED(AcceptSig)) {
                        Accepted = Row;
                        Done = TRUE;
                    }
                }

                if (!IsHeader && Row->FilesCount) {
                    UISignal ShowSig = AppIconButton(
                        APP_ICON_KIND_FOLDER_OPEN_OUTLINE,
                        NULL,
                        "Show in file explorer###show"_s8
                    );

                    if (UI_CLICKED(ShowSig)) {
                        ShowPath = Row->Files[0]->Head->String;

                        if (!IsFile)
                            ShowPath = StrChopLastSlash(ShowPath);

                        Done = TRUE;
                    }
                }

                if (Row->FilesCount) {
                    UISignal RemoveSig = AppIconButton(
                        APP_ICON_KIND_X,
                        NULL,
                        IsFile
                            ? "Remove###remove"_s8
                            : IsHeader
                                ? "Remove all###remove"_s8
                                : "Remove folder###remove"_s8
                    );

                    if (UI_CLICKED(RemoveSig)) {
                        Removed = Row;
                        Done = TRUE;
                    }
                }
            }

            if (Done)
                UIContextMenuClose();
        }
    }

    // NOTE(nc): the cursor row's file is the file for the commands. moving
    //         : the cursor does not change the active flight.
    if (State->Cursor.Y > 0) {
        FLAPExplorerRow* Row = &Rows.V[State->Cursor.Y - 1];

        if (Row->Kind == FLAP_EXPLORER_ROW_KIND_FILE) {
            Registers()->FilePath = ArenaPushStrCpy(
                AppFrameMemPool(),
                Row->Files[0]->Head->String
            );
        }
    }

    if (Accepted) {
        if (Accepted->Kind != FLAP_EXPLORER_ROW_KIND_FILE) {
            ToggleExpand = Accepted;
        } else if (Accepted->IsFlights) {
            Activated = Accepted;
        } else {
            AppCmd(
                APP_COMMAND_KIND_BUILD_TAB,
                __Registers.String = "flv_view"_s8,
                __Registers.FilePath = Accepted->Files[0]->Head->String
            );
        }
    }

    if (Activated) {
        Str8 Path = Activated->Files[0]->Head->String;
        ConfigNode* Transient = ConfigNodeChildFromStr(
            ConfigNodeRoot(),
            "transient"_s8
        );
        ConfigNode* Flight = ConfigNodeChildFromStrOrAlloc(
            APP_STATE->Config,
            Transient,
            "flight"_s8
        );

        if (!StrMatch(Flight->Head->String, Path, 0))
            ConfigNodeNewReplace(APP_STATE->Config, Flight, Path);

        // NOTE(nc): a flight that is not checked is never the active flight,
        //         : so making a flight the active flight also checks it.
        if (!Activated->CheckedCount)
            FLAPFlightCheckStore(Activated->Files[0], TRUE);
    }

    if (ToggleExpand && !Filter.Size)
        FLAPExplorerCollapseToggle(View, ToggleExpand->CollapseKey);

    if (ToggleCheck) {
        b32 IsChecked = (ToggleCheck->CheckedCount != ToggleCheck->FilesCount);

        for (u32 Index = 0; Index < ToggleCheck->FilesCount; ++Index)
            FLAPFlightCheckStore(ToggleCheck->Files[Index], IsChecked);
    }

    if (ShowPath.Size) {
        AppCmd(
            APP_COMMAND_KIND_SHOW_FILE_IN_EXPLORER,
            __Registers.FilePath = ShowPath
        );
    }

    // NOTE(nc): this takes the files out of the list. it does not delete
    //         : anything on disk, and it does not close a View's tab. the
    //         : command runs after this function, so the rows stay good for
    //         : the rest of this frame.
    if (Removed) {
        for (u32 Index = 0; Index < Removed->FilesCount; ++Index) {
            AppCmd(
                APP_COMMAND_KIND_REMOVE_CONFIG,
                __Registers.Config = Removed->Files[Index]->ID
            );
        }
    }

    if (CollapseClicked && !Filter.Size) {
        FLAPExplorerCollapseStore(View, "file"_s8, AnyRootExpanded);
        FLAPExplorerCollapseStore(View, "flv_file"_s8, AnyRootExpanded);
    }

    if (AddClicked) {
        AppCmd(
            APP_COMMAND_KIND_RUN_COMMAND,
            __Registers.CommandName = OpenCommandName
        );
    }

    if (FilterClicked) {
        AppCmd(
            APP_COMMAND_KIND_RUN_COMMAND,
            __Registers.CommandName = FilterCommandName
        );
    }

    AppStoreViewScrollPosition(ScrollPosition);
    ReleaseScratch(Scratch);
}
