#pragma once

#include <optional>

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Series volume pill ("T3") painted over the top-right corner of a cover.
namespace volumebadge {

// Paints nothing without a volume or when the cover is too small to hold it.
void draw(const GfxRenderer& renderer, const Rect& cover, std::optional<float> volume, int fontId);

}  // namespace volumebadge
