#include "nc_application.h"
#include "nc_application_views.h"
#include "nc_profile.h"
#include "nc_ui.h"
#include "nc_file.h"
#include "nc_ui_components.h"


APP_VIEW_UI_FUNC_DEF(Text)
{
    TempArena Scratch = GetScratch(NULL, 0);
    Access* Acc = AccessOpen();
    f32 MainFontSize = UITailFontSize();
    r2f32 TextAreaRect = Rect;

    PROFILE(ProcessTextFileCommandsTimer) {
        for (AppCommand* Cmd = NULL; AppNextViewCommand(&Cmd); ) {
            AppCommandKind Kind = AppCommandKindFromStr(Cmd->Name);

            switch (Kind) {
                default: {} break;

                case APP_COMMAND_KIND_PICK_FILE: {
                    Str8 Src = Registers()->FilePath;
                    Str8 Dst = Cmd->Regs->FilePath;

                    if (Src.Size && Dst.Size) {
                        AppStoreViewExprStr(
                            AppEvalStrFromFilePath(Scratch.MemPool, Dst)
                        );
                    }
                } break;
            }
        }

        Registers()->FilePath = AppFilePathFromEval(
            AppFrameMemPool(), 
            Eval
        );
        Registers()->Cursor.Line = AppViewSettingValueFromName("cursor_line"_s8).I64;
        Registers()->Cursor.Column = AppViewSettingValueFromName("cursor_column"_s8).I64;
        Registers()->Mark.Line = AppViewSettingValueFromName("mark_line"_s8).I64;
        Registers()->Mark.Column = AppViewSettingValueFromName("mark_column"_s8).I64;

        if (!Registers()->Cursor.Line)
            Registers()->Cursor.Line = 1;

        if (!Registers()->Cursor.Column)
            Registers()->Cursor.Column = 1;

        if (!Registers()->Mark.Line)
            Registers()->Mark.Line = 1;

        if (!Registers()->Mark.Column)
            Registers()->Mark.Column = 1;

        Str8List Overrides = AppPossibleOverridesFromFilePath(
            Scratch.MemPool,
            Registers()->FilePath
        );
        r1u64 Range = AppSpaceRangeFromEval(Eval);

        Registers()->TextKey = AppKeyFromEvalSpaceRange(
            Eval.Space, 
            Range, 
            TRUE
        );

        u128 TextHash = BigHash(Str8Struct(&Registers()->TextKey));
        Str8 Data = CDataFromHash(Acc, TextHash);
        b32 FileIsMissing = (
            Registers()->FilePath.Size &&
            !SystemGetFileProperties(Registers()->FilePath).Modified
        );
        b32 TextHashHasData = TextHash != u128{};

        if (FileIsMissing) {
            UIWidthFill() {
                UIHeightFill() {
                    UIColumn() {
                        UIPadding(UI_PERCENT(1, 0)) {
                            TempArena Scratch = GetScratch(NULL, 0);

                            UIPreferredWidth(UI_SUM_OF_CHILDREN(1)) {
                                UIPreferredHeight(UI_EM(3, 1)) {
                                    UIRow() {
                                        UIPadding(UI_PERCENT(1, 0)) {
                                            UIPreferredWidth(UI_TEXT_DIM(1, 1)) {
                                                UITag("weak"_s8) {
                                                    AppFont(APP_FONT_SLOT_ICONS) {
                                                        UILabel(APP_ICON_KIND_TEXT_TABLE[APP_ICON_KIND_WARNING_BIG]);
                                                        UILabel(
                                                            "Could not find \"%S\".",
                                                            PRINT_STR(Registers()->FilePath)
                                                        );
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            UIPreferredHeight(UI_EM(3, 1)) {
                                UIRow() {
                                    UIPadding(UI_PERCENT(1, 0)) {
                                        UIPreferredWidth(UI_TEXT_DIM(10, 1)) {
                                            UICornerRadius(UIHeadFontSize() / 3) {
                                                UIPreferredWidth(UI_TEXT_DIM(10, 1)) {
                                                    UIFocus(UI_FOCUS_KIND_ON) {
                                                        UITextAlignment(UI_TEXT_ALIGN_CENTRE) {
                                                            UITag("pop"_s8) {
                                                                if (UI_CLICKED(UIButton("Find alternative..."_s8))) {
                                                                    AppCmd(
                                                                        APP_COMMAND_KIND_RUN_COMMAND,
                                                                        __Registers.CommandName = APP_COMMAND_KIND_INFO_TABLE[APP_COMMAND_KIND_PICK_FILE].String
                                                                    );
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            ReleaseScratch(Scratch);
                        }
                    }

                    if (
                        !FileIsMissing && 
                        Eval.Msgs.MaxKind == EVAL_MSG_KIND_NULL
                    ) {
                        AppStoreViewLoadingInfo(
                            1,
                            0,
                            Data.Size
                        );
                    }
                }
            }
        }
    }

    AppStoreViewParameterI64("cursor_line"_s8, Registers()->Cursor.Line);
    AppStoreViewParameterI64("cursor_column"_s8, Registers()->Cursor.Column);
    AppStoreViewParameterI64("mark_line"_s8, Registers()->Mark.Line);
    AppStoreViewParameterI64("mark_column"_s8, Registers()->Mark.Column);
    AccessClose(Acc);
    ReleaseScratch(Scratch);
}