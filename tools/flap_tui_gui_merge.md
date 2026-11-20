# FLAP TUI / GUI merge reference

This document lists the differences between the TUI code and the GUI code that the FLAP layer touches, the decisions made, and the work order. It is a reference for Phase 2 (the TUI/GUI merge).

Sources compared: the TUI copies in the project, and the GUI copies of `flap_gui.cpp`, `nc_draw`, `nc_ui`, `nc_ui_components`, `nc_application.h`, `nc_render.h`, the CAD render code and the draw part of `AppWindowFrame`. The TUI `nc_ui.cpp` and `nc_ui_components.cpp` were compared by header only.

## 1. Decisions

- The two backends have the same names. The TUI gets the GUI names. The FLAP code has no `#if`.
- Text: the TUI gets the GUI font names with empty or constant values (section 3).
- GUI graph: Y is auto-fit only. Zoom is the same as the TUI. The cursor does not snap to a sample.
- The reduce uses a time grid in the two backends (section 7).

## 2. Units

`1.0f` is one cell in the TUI and one pixel in the GUI. The rule for FLAP code:

- A height of N rows is `N * UIHeadPXHeight()`.
- A width of N cells is `UI_EM(N, strictness)`.

With a TUI font size of 1, the TUI layout does not change. An em is a font height, so GUI widths are about two characters for each em. Examine the GUI widths one time.

## 3. Names the TUI must get

| Name | TUI behaviour |
|---|---|
| `UIHeadFontSize()` | Gives 1. |
| `UIHeadPXHeight()` | Gives 1. |
| `UI_EM(X, Y)` | Same macro as the GUI. |
| `UIFont`, `UIFontSize`, `UIHeadFont()`, `UIIconFont()` | No effect. |
| `AppFont(Slot)`, `AppFontSlot` | No effect. FLAP uses `AppFont(APP_FONT_SLOT_ICONS)` around `APP_ICON_KIND_TEXT_TABLE` text. |
| `FontTag`, `FontRasterKind` | Empty types. |
| `Box->Font`, `Box->FontSize`, `Box->TabSize`, `Box->TextRasterKind` | `FontSize` is 1. |
| `FontMetricsFromTagSize` | Ascent and descent are 0. |
| `FontDimensionsFromTagSizeString`, `FontCharPositionFromTagSizeString` | Results in columns. |
| `NC_DrawText` | Ignores the font arguments. |
| `DrawRect` with 5 arguments | Ignores corner radius and edge softness. |
| `DTopClip` | New name for `DHeadClip`. |
| `FancyStrParams` | One struct with the GUI fields and the TUI `Flags`. Each backend ignores the fields it cannot draw. |
| View function signature | `(EvalEval Eval, r2f32 Rect)`. The path comes from `AppFilePathFromEval`. |

Scroll: the FLAP code does not set `ScrollPoint->Offset` to 0. A TUI scroll rate of 1 makes the same code snap.

## 4. FLAP edits

### flap_widgets.cpp

| Function | Change |
|---|---|
| `FLAPFlaggedRowDraw` | `DrawRect` to 5 arguments. |
| `FLAPTable`, `FLAPFlagList`, `FLAPErrorList` | `RowHeightPX = UIHeadPXHeight()`. |
| `FLAPTable` | Header: `Length(Rect).Y - UIHeadPXHeight()`. |
| `FLAPGraphText` | Takes the box. Draws with `NC_DrawText` and the box font. Y is `Floor(row centre + Ascent / 2 - Descent / 2)`. `NC_DrawText` has no `MaxX`: the clip stops a long label. |
| `FLAPGraphDraw` | `DrawRect` to 5 arguments. Legend gaps from the font size. Label X from the measured width, not `String.Size`. The braille and marker sections go to the backend draw. |
| `FLAPGraph` | Gutter, plot offsets, columns for each unit, and cursor snap go into the params. |

### flap_views.cpp

| Function | Change |
|---|---|
| `FLAPAnalysisUI` | Title height and `Rect.Y0` offset use `UIHeadPXHeight()`. |
| `FLAPLayoutUI` | One value for the boundary thickness (now 1.0 in three places). Minimum sizes 3.0 and 6.0 use the unit rule. |
| `FLVView` | New signature. Path from the eval. Remove `State->ErrorScroll.Offset = 0.0f`. |
| `Explorer` | New signature. Widths (26, 2, 4) and the indent use `UI_EM`. `Rect.Y0` offset and `RowHeightPX` use `UIHeadPXHeight()`. `UIDivider` size in em. `AppFont(APP_FONT_SLOT_ICONS)` around the icon labels and the checkbox. Remove `ScrollPosition.Y.Offset = 0.0f`. |

### flap.cpp / flap_gui.cpp

- Search row: `Rect.Y0` offset uses `UIHeadPXHeight()`.
- In `flap.cpp` but not in `flap_gui.cpp`: `FLVCompilerInit()`, `AsyncTickEnd()` in the lane loop, the 100 ms wait with the repeat check inside the lock, and the `FLIEntryPoint` branch for `NC_FLI`.

## 5. Application layer

| Item | TUI | GUI |
|---|---|---|
| Watch view | `AppWatchViewUI` in nc_application | Inline in `AppViewUI`, with `pending` and `getting_started` |
| Search row | `UILineEdit`, `UI_COMMITTED` | `AppCell` |
| Quit flag | `ShouldQuit` | `Quit` |
| Cell params | `IsExpanded`, `IsToggled` | `Expanded`, `Toggled`, plus `Revert`, `LineEditKey`, note strings |
| Watch state | `IsInitialised`, `__TextEditState` | `Initialised`, `DummyTextEditState` |
| Auto-complete | `ReplacedRanged` | `ReplacedRange` |
| Command | `Cmd->Registers` | `Cmd->Regs` |
| `AppInfoFromWatchRowCell` | 6 arguments | Also font and font size |
| `AppRegistersFillSlotFromCellCtx` | `(Row)` | `(RootCtxExpr, Row)` |
| `AppSetHoverEval` | `v2i32` | `v2f32` |
| `AppViewFilePath()` | Exists | Not there |
| `APP_STATE->TabFilePaths` | Exists (explorer open-tab dot) | Not there |
| `APP_ICON_KIND_CHECK_PARTIAL` | Exists | Not there (enum entry and icon font glyph) |
| `AppViewUIRuleFromStr` | Declared | Declared as `AppViewUIRuleFomStr` |
| State | One UI state in `AppState` | `AppWindowState` for each window |
| `NC_DATA_DIR` | `flap` | `flap_gui` |
| Theme colours `curve_0` to `curve_7` | In the themes | Not confirmed. A colour that is not in the theme shows as magenta. |

## 6. Draw and render

| Item | TUI | GUI |
|---|---|---|
| `DrawRect` | 3 arguments | 5 arguments, gives `Rect2DInstr*` |
| Text | `DrawFancyStrList`, `DDimensionsFromStr` (cells) | Font runs, `NC_DrawText`, `FontDimensionsFromTagSizeString` (pixels) |
| Text position | Top-left of the cell | From ascent and descent |
| Clip | `DPushClip` | `DPushClip` does not intersect. An empty clip means no clip. |
| Frame | `DBeginFrame(pool, fg, bg)`, `DEndFrame` | Buckets, `DBeginFrame(IconFont)`, `DSubmitBucket` |
| `nc_render` | Cell grid (`RendPutCell`) | Passes and batches. One file for each backend, the same as `nc_console`. |
| Custom draw call | `UIDrawRoot` in nc_ui | Draw loop in `AppWindowFrame` |

GUI custom draw facts (`AppWindowFrame`):

- The custom draw runs after the box pushes its own clip. A box without `UI_BOX_KIND_CLIP` gets the clip of its nearest clip ancestor.
- The box transparency and squish go on the draw stacks. The CAD pass does not read them.

## 7. GUI graph widget

### Shared (TUI and GUI)

- State, key, cache, reduce, fit and input do not change.
- Time grid: `Step` is a whole number of microseconds from the shared time range and the column count of the graph. Column `k` covers `[k * Step, (k + 1) * Step)`. The cache key holds `Step`. A pan reduces only the new columns. A zoom does a full reduce.
- Pan moves in whole columns in the two backends.
- The marker rule needs a sample count for each column, or a different rule.
- The min/max pyramid stays open until the measurement.

### GUI draw

- Curves use the CAD pass with `DrawCADLine`. `XForm` is identity and the CPU calculates points in window pixels.
- For each column with data: one vertical line from Min to Max if they are different, and one join line from the previous column if the join bit is set and the join is not hidden.
- A line of zero length draws nothing.
- Markers use `DrawCADDisc`. Arcs draw before lines in the pass.
- Draw order: bands and cursor line (rects), then curves and markers (CAD), then legend and labels (text). This order gives each graph its own CAD pass.
- Clip: the graph draw pushes `Intersect(DTopClip(), plot rect)` for the curves. `DrawCADBegin` sets `Params->Clip = DTopClip()`.
- `REND_PASS_KIND_BATCHES[PASS_KIND_CAD]` must be 1.
- Cost: about 96 KB for each curve for each frame at a 2000 pixel plot. If the measurement shows a problem, add a smaller curve instruction to the CAD pass.

### Open

- Pane resize: keep the time range (full reduce on each resize frame) or keep the scale (reduce only new columns).

## 8. Work order

1. libnc: add the TUI names in section 3. The TUI must build and look the same after this step.
2. FLAP: apply the edits in section 4. The TUI must look the same after this step.
3. Application layer: resolve section 5.
4. Graph: move the cell-unit numbers into the params, then the time grid.
5. Render: add the CAD pass to the GUI renderer.
6. Graph: the GUI draw function, then the measurement.
