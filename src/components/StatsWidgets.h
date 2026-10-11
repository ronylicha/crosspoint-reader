#pragma once

#include <ReadingLog.h>

#include <cstddef>
#include <cstdint>

#include "components/themes/BaseTheme.h"

class GfxRenderer;
namespace freeink {
struct Icon;
}

// Drawing helpers shared by the home statistics tile and the Statistics tab.
namespace statswidgets {

// Paints a 1-bit SDK icon (freeink::Icon, MSB first, 0 = ink) at logical
// coordinates, for the hand-painted figures that sit beside FreeInkUI parts.
void drawIcon(const GfxRenderer& renderer, const freeink::Icon& icon, int x, int y, bool black = true);

// "21 s", "34 min", "1 h 05" in the UI language's units.
void formatDuration(char* out, size_t size, uint32_t seconds);

// A headline figure: big value, an up/down trend arrow with the change, and a
// label underneath. trend > 0 draws the rising arrow, < 0 the falling one,
// 0 no arrow; trendText may be null. Returns the drawn height.
int drawFigure(const GfxRenderer& renderer, int x, int y, int width, const char* value, int trend,
               const char* trendText, const char* label);
// Height drawFigure() uses, for laying out a column of figures.
int figureHeight(const GfxRenderer& renderer);

// drawFigure() led by a 32 px icon centred on the figure's height.
int drawIconFigure(const GfxRenderer& renderer, int x, int y, int width, const freeink::Icon& icon, const char* value,
                   int trend, const char* trendText, const char* label);

// Dotted hairline separating borderless sections; draws along the longer side.
void drawRule(const GfxRenderer& renderer, Rect rect);

// Slim reading-progress bar: light dotted track under a solid fill.
void drawProgressBar(const GfxRenderer& renderer, int x, int y, int width, int percent);
constexpr int PROGRESS_BAR_HEIGHT = 4;

// Bar chart of reading minutes per day, oldest left, weekday initials below;
// today's bar is solid, earlier days dithered. Bars scale to the busiest day.
void drawDayChart(const GfxRenderer& renderer, Rect rect, const readingstats::DayTotals* days, size_t count);

// Rounded tile frame used by home and statistics cards.
void drawTile(const GfxRenderer& renderer, Rect rect);

// Small icon + label + value row ("⏱ Avg / page   21 s").
void drawStatRow(const GfxRenderer& renderer, Rect rect, const freeink::Icon& icon, const char* label,
                 const char* value);

}  // namespace statswidgets
