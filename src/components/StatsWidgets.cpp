#include "StatsWidgets.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "fontIds.h"
#include "icons/appIcons.h"

namespace statswidgets {
namespace {
#ifdef OMIT_FONTS
constexpr int VALUE_FONT_ID = UI_12_FONT_ID;
#else
constexpr int VALUE_FONT_ID = NOTOSANS_18_FONT_ID;
#endif
constexpr int LABEL_FONT_ID = UI_10_FONT_ID;
constexpr int ROW_FONT_ID = UI_12_FONT_ID;
constexpr int TREND_ICON = 24;
constexpr int TILE_RADIUS = 10;
constexpr int ROW_ICON = 24;
constexpr int FIGURE_ICON_GAP = 12;

constexpr StrId WEEKDAY_INITIALS[7] = {StrId::STR_WEEKDAY_SUN_SHORT, StrId::STR_WEEKDAY_MON_SHORT,
                                       StrId::STR_WEEKDAY_TUE_SHORT, StrId::STR_WEEKDAY_WED_SHORT,
                                       StrId::STR_WEEKDAY_THU_SHORT, StrId::STR_WEEKDAY_FRI_SHORT,
                                       StrId::STR_WEEKDAY_SAT_SHORT};

// 1970-01-01 (day 0) was a Thursday; 0 = Sunday.
int weekdayOf(const readingstats::DayNumber day) { return ((day % 7) + 7 + 4) % 7; }
}  // namespace

void drawIcon(const GfxRenderer& renderer, const freeink::Icon& icon, const int x, const int y, const bool black) {
  const int rowBytes = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; ++row) {
    const uint8_t* line = icon.bits + row * rowBytes;
    for (int col = 0; col < icon.w; ++col) {
      if ((line[col / 8] >> (7 - col % 8)) & 1) continue;  // 1 = transparent
      renderer.drawPixel(x + col, y + row, black);
    }
  }
}

void formatDuration(char* out, const size_t size, const uint32_t seconds) {
  if (seconds < 60) {
    snprintf(out, size, tr(STR_DURATION_SECONDS), static_cast<unsigned>(seconds));
  } else if (seconds < 3600) {
    snprintf(out, size, tr(STR_DURATION_MINUTES), static_cast<unsigned>(seconds / 60));
  } else {
    snprintf(out, size, tr(STR_DURATION_HOURS), static_cast<unsigned>(seconds / 3600),
             static_cast<unsigned>(seconds % 3600 / 60));
  }
}

int figureHeight(const GfxRenderer& renderer) {
  return renderer.getLineHeight(VALUE_FONT_ID) + renderer.getLineHeight(LABEL_FONT_ID) + 2;
}

int drawFigure(const GfxRenderer& renderer, const int x, const int y, const int width, const char* value,
               const int trend, const char* trendText, const char* label) {
  const int valueHeight = renderer.getLineHeight(VALUE_FONT_ID);
  renderer.drawText(VALUE_FONT_ID, x, y, value, true, EpdFontFamily::BOLD);
  int trendX = x + renderer.getTextWidth(VALUE_FONT_ID, value, EpdFontFamily::BOLD) + 6;
  if (trend != 0 && trendX + TREND_ICON <= x + width) {
    drawIcon(renderer, trend > 0 ? icon_trend_up_24 : icon_trend_down_24, trendX,
             y + (valueHeight - TREND_ICON) / 2 - 2);
    trendX += TREND_ICON + 2;
  }
  if (trendText && trendText[0] != '\0' && trendX + renderer.getTextWidth(SMALL_FONT_ID, trendText) <= x + width) {
    renderer.drawText(SMALL_FONT_ID, trendX, y + (valueHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2, trendText,
                      true, EpdFontFamily::BOLD);
  }
  renderer.drawText(LABEL_FONT_ID, x, y + valueHeight + 2, label, true);
  return figureHeight(renderer);
}

int drawIconFigure(const GfxRenderer& renderer, const int x, const int y, const int width, const freeink::Icon& icon,
                   const char* value, const int trend, const char* trendText, const char* label) {
  const int height = figureHeight(renderer);
  drawIcon(renderer, icon, x, y + (height - icon.h) / 2);
  const int textX = x + icon.w + FIGURE_ICON_GAP;
  return drawFigure(renderer, textX, y, width - (textX - x), value, trend, trendText, label);
}

void drawRule(const GfxRenderer& renderer, const Rect rect) {
  if (rect.width >= rect.height) {
    renderer.fillRectDither(rect.x, rect.y, rect.width, 1, Color::DarkGray);
  } else {
    renderer.fillRectDither(rect.x, rect.y, 1, rect.height, Color::DarkGray);
  }
}

void drawProgressBar(const GfxRenderer& renderer, const int x, const int y, const int width, const int percent) {
  if (width <= 0) return;
  renderer.fillRectDither(x, y, width, PROGRESS_BAR_HEIGHT, Color::LightGray);
  const int fill = width * std::clamp(percent, 0, 100) / 100;
  if (fill > 0) renderer.fillRect(x, y, fill, PROGRESS_BAR_HEIGHT);
}

void drawDayChart(const GfxRenderer& renderer, const Rect rect, const readingstats::DayTotals* days,
                  const size_t count) {
  if (count == 0 || rect.width <= 0) return;
  const int labelHeight = renderer.getLineHeight(LABEL_FONT_ID);
  const int barsBottom = rect.y + rect.height - labelHeight - 4;
  const int barsHeight = barsBottom - rect.y;
  if (barsHeight <= 4) return;

  uint32_t busiest = 0;
  for (size_t i = 0; i < count; ++i) busiest = std::max(busiest, days[i].seconds);

  const int slot = rect.width / static_cast<int>(count);
  const int barWidth = std::max(4, slot * 3 / 5);
  for (size_t i = 0; i < count; ++i) {
    const int slotX = rect.x + static_cast<int>(i) * slot;
    const int barX = slotX + (slot - barWidth) / 2;
    const auto& day = days[i];
    if (busiest > 0 && day.seconds > 0) {
      const int height = std::max(3, static_cast<int>(static_cast<uint64_t>(barsHeight) * day.seconds / busiest));
      const bool isToday = i + 1 == count;
      if (isToday) {
        renderer.fillRect(barX, barsBottom - height, barWidth, height);
      } else {
        renderer.fillRectDither(barX, barsBottom - height, barWidth, height, Color::DarkGray);
        renderer.drawRect(barX, barsBottom - height, barWidth, height);
      }
    }
    if (day.day != readingstats::UNKNOWN_DAY) {
      const char* initial = I18N.get(WEEKDAY_INITIALS[weekdayOf(day.day)]);
      const int labelWidth = renderer.getTextWidth(LABEL_FONT_ID, initial);
      renderer.drawText(LABEL_FONT_ID, slotX + (slot - labelWidth) / 2, barsBottom + 4, initial, true,
                        i + 1 == count ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
  }
  renderer.fillRect(rect.x, barsBottom, rect.width, 1);
}

void drawTile(const GfxRenderer& renderer, const Rect rect) {
  renderer.drawRoundedRect(rect.x, rect.y, rect.width, rect.height, 1, TILE_RADIUS, true);
}

void drawStatRow(const GfxRenderer& renderer, const Rect rect, const freeink::Icon& icon, const char* label,
                 const char* value) {
  const int lineHeight = renderer.getLineHeight(ROW_FONT_ID);
  drawIcon(renderer, icon, rect.x, rect.y + (rect.height - ROW_ICON) / 2);
  const int textY = rect.y + (rect.height - lineHeight) / 2;
  renderer.drawText(ROW_FONT_ID, rect.x + ROW_ICON + 12, textY, label, true);
  const int valueWidth = renderer.getTextWidth(ROW_FONT_ID, value, EpdFontFamily::BOLD);
  renderer.drawText(ROW_FONT_ID, rect.x + rect.width - valueWidth, textY, value, true, EpdFontFamily::BOLD);
}

}  // namespace statswidgets
