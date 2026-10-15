#include "VolumeBadge.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cmath>
#include <cstdio>

namespace volumebadge {
namespace {
constexpr int INSET = 4;
constexpr int PAD_X = 6;
constexpr int PAD_Y = 2;
}  // namespace

void draw(const GfxRenderer& renderer, const Rect& cover, const std::optional<float> volume, const int fontId) {
  if (!volume || !std::isfinite(*volume) || *volume < 0.0f) return;
  char number[16];
  snprintf(number, sizeof(number), "%.9g", static_cast<double>(*volume));
  char label[24];
  snprintf(label, sizeof(label), tr(STR_VOLUME_BADGE), number);

  const int textWidth = renderer.getTextWidth(fontId, label, EpdFontFamily::BOLD);
  const int height = renderer.getLineHeight(fontId) + 2 * PAD_Y;
  const int width = textWidth + 2 * PAD_X;
  if (width + 2 * INSET > cover.width || height + 2 * INSET > cover.height) return;

  // Top-right: the left edge carries the home cover's painted spine.
  const int x = cover.x + cover.width - INSET - width;
  const int y = cover.y + INSET;
  // A white ring keeps the black pill readable on a dark cover.
  renderer.fillRoundedRect(x - 1, y - 1, width + 2, height + 2, height / 2 + 1, Color::White);
  renderer.fillRoundedRect(x, y, width, height, height / 2, Color::Black);
  renderer.drawText(fontId, x + PAD_X, y + PAD_Y, label, false, EpdFontFamily::BOLD);
}

}  // namespace volumebadge
