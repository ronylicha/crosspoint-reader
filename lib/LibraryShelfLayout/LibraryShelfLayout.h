#pragma once

namespace library::shelf {

inline constexpr int COLUMNS = 4;
inline constexpr int ROWS = 3;
inline constexpr int PAGE_SIZE = COLUMNS * ROWS;
inline constexpr int CELL_PADDING = 4;

constexpr int boundedSelection(const int selected, const int count) {
  return count <= 0 || selected < 0 ? 0 : (selected >= count ? count - 1 : selected);
}

constexpr int pageStart(const int selected, const int count) {
  return (boundedSelection(selected, count) / PAGE_SIZE) * PAGE_SIZE;
}

constexpr int visibleCount(const int top, const int count) {
  if (count <= 0 || top >= count) return 0;
  const int remaining = count - (top < 0 ? 0 : top);
  return remaining < PAGE_SIZE ? remaining : PAGE_SIZE;
}

// Page navigation wraps and preserves the offset, clamping on incomplete pages.
constexpr int nextPage(const int selected, const int count) {
  if (count <= 0) return 0;
  const int bounded = boundedSelection(selected, count);
  const int top = pageStart(bounded, count);
  const int nextTop = count - top <= PAGE_SIZE ? 0 : top + PAGE_SIZE;
  const int offset = bounded % PAGE_SIZE;
  return count - nextTop <= offset ? count - 1 : nextTop + offset;
}

constexpr int previousPage(const int selected, const int count) {
  if (count <= 0) return 0;
  const int bounded = boundedSelection(selected, count);
  const int top = pageStart(bounded, count);
  const int previousTop = top == 0 ? pageStart(count - 1, count) : top - PAGE_SIZE;
  const int offset = bounded % PAGE_SIZE;
  return count - previousTop <= offset ? count - 1 : previousTop + offset;
}

struct Layout {
  int cellWidth;
  int rowHeight;
  int rowGap;
  int coverWidth;
  int coverHeight;
  int labelHeight;
};

constexpr Layout makeLayout(const int width, const int height, const int labelHeight, const int gap = 8) {
  const int rowGap = gap < 0 ? 0 : gap;
  const int textHeight = labelHeight < 0 ? 0 : labelHeight;
  if (width <= 0 || height <= 0 || rowGap > height / (ROWS - 1)) return {};

  const int cellWidth = width / COLUMNS;
  const int rowHeight = (height - (ROWS - 1) * rowGap) / ROWS;
  if (cellWidth <= 2 * CELL_PADDING || rowHeight <= 2 * CELL_PADDING || textHeight >= rowHeight - 2 * CELL_PADDING) {
    return {};
  }

  const int maxCoverWidth = cellWidth - 2 * CELL_PADDING;
  const int maxCoverHeight = rowHeight - textHeight - 2 * CELL_PADDING;
  const int widthForHeight = (maxCoverHeight / 3) * 2 + ((maxCoverHeight % 3) * 2) / 3;
  const int coverWidth = maxCoverWidth < widthForHeight ? maxCoverWidth : widthForHeight;
  const int coverHeight = (coverWidth / 2) * 3 + ((coverWidth % 2) * 3) / 2;
  if (coverWidth == 0 || coverHeight == 0) return {};

  return {cellWidth, rowHeight, rowGap, coverWidth, coverHeight, textHeight};
}

}  // namespace library::shelf
