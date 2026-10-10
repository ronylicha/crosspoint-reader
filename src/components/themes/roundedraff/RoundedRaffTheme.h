#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

namespace RoundedRaffMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 15,
                                 .batteryHeight = 12,
                                 // Fit the 23px SMALL_FONT_ID line box and lift it one pixel while
                                 // keeping the 12px battery glyph at y=19, aligned with Lyra.
                                 .topPadding = 13,
                                 .batteryBarHeight = 24,
                                 .headerHeight = 84,
                                 .verticalSpacing = 10,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 20,
                                 .listRowHeight = 42,
                                 .listWithSubtitleRowHeight = 69,
                                 .listRowGap = 6,
                                 .listRowRadius = 20,
                                 .listInset = 20,
                                 .listSidePadding = 20,
                                 .listSelectionStyle = 0,  // invert fill (black card)
                                 .listScrollWidth = 4,
                                 .listScrollSide = 0,
                                 .listTitleBold = true,
                                 .headerSidePadding = 18,
                                 .headerUnderlineSize = 0,
                                 .headerTitleAlign = 0,  // left
                                 .headerBatterySide = 0,
                                 // A centered clock needs no left title reserve, so RoundedRaff
                                 // can show one after all.
                                 .headerShowsClock = true,
                                 .tabSpacing = 10,
                                 .tabBarHeight = 50,
                                 .tabPillFullSlot = true,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyHeight = 56,
                                 .keyboardKeySpacing = 10,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = 0,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 94,
                                 .popupTopOffsetRatio = 0.12f,
                                 .popupMarginX = 20,
                                 .popupMarginY = 14,
                                 .popupFrameThickness = 2,
                                 .popupCornerRadius = 18,
                                 .popupTextBold = true,
                                 .popupTextInverted = false,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 4,
                                 .popupProgressDrawOutline = true,
                                 .popupProgressClampPercent = true,
                                 .popupProgressFillInverted = false,
                                 .popupProgressOutlineInverted = false,
                                 .optionPopupItemSpacing = 6,
                                 .optionPopupInnerPadding = 24,
                                 .optionPopupSelectionVPadding = 10,
                                 .optionPopupDialogSideMargin = 20,
                                 .textFieldHorizontalPadding = 8,
                                 .textFieldNormalThickness = 2,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = -1,
                                 .controlRadius = 18,
                                 .sheetRadius = 18,
                                 .capsuleRadius = 255};
}

class RoundedRaffTheme : public BaseTheme {
 public:
  void drawTextField(const GfxRenderer& renderer, Rect rect, int textWidth, bool cursorMode = false,
                     int contentStartX = 0, int contentWidth = 0) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
};
