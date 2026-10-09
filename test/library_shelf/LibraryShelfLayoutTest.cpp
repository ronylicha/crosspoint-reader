#include <gtest/gtest.h>

#include <climits>

#include "LibraryShelfLayout/LibraryShelfLayout.h"

namespace {
using namespace library::shelf;

static_assert(COLUMNS == 4 && ROWS == 3 && PAGE_SIZE == 12);
static_assert(pageStart(24, 25) == 24);
static_assert(nextPage(11, 13) == 12);
static_assert(previousPage(0, 25) == 24);
static_assert(makeLayout(776, 300, 32).coverHeight == 54);

void expectEmpty(const Layout layout) {
  EXPECT_EQ(layout.cellWidth, 0);
  EXPECT_EQ(layout.rowHeight, 0);
  EXPECT_EQ(layout.rowGap, 0);
  EXPECT_EQ(layout.coverWidth, 0);
  EXPECT_EQ(layout.coverHeight, 0);
  EXPECT_EQ(layout.labelHeight, 0);
}

void expectFits(const int width, const int height, const int labelHeight, const int gap = 8) {
  const Layout layout = makeLayout(width, height, labelHeight, gap);
  ASSERT_GT(layout.coverWidth, 0);
  ASSERT_GT(layout.coverHeight, 0);
  EXPECT_LE(layout.cellWidth * COLUMNS, width);
  EXPECT_LE(layout.rowHeight * ROWS + layout.rowGap * (ROWS - 1), height);
  EXPECT_LE(layout.coverWidth + 2 * CELL_PADDING, layout.cellWidth);
  EXPECT_LE(layout.coverHeight + layout.labelHeight + 2 * CELL_PADDING, layout.rowHeight);
  EXPECT_EQ(layout.rowGap, gap < 0 ? 0 : gap);
  EXPECT_EQ(layout.labelHeight, labelHeight < 0 ? 0 : labelHeight);
  EXPECT_LE(layout.coverHeight * 2, layout.coverWidth * 3);
  EXPECT_LE(layout.coverWidth * 3 - layout.coverHeight * 2, 1);
}

TEST(LibraryShelfLayout, FitsBothFullScreenOrientations) {
  expectFits(480, 800, 16);
  expectFits(800, 480, 16);
  EXPECT_EQ(makeLayout(480, 800, 16).coverWidth, 112);
  EXPECT_EQ(makeLayout(480, 800, 16).coverHeight, 168);
  EXPECT_EQ(makeLayout(800, 480, 16).coverWidth, 86);
  EXPECT_EQ(makeLayout(800, 480, 16).coverHeight, 129);
}

TEST(LibraryShelfLayout, FitsUsableViewportAfterChromeAndMargins) {
  expectFits(456, 620, 16);
  expectFits(776, 300, 16);
  EXPECT_EQ(makeLayout(456, 620, 16).coverWidth, 106);
  EXPECT_EQ(makeLayout(456, 620, 16).coverHeight, 159);
  EXPECT_EQ(makeLayout(776, 300, 16).coverWidth, 46);
  EXPECT_EQ(makeLayout(776, 300, 16).coverHeight, 69);
}

TEST(LibraryShelfLayout, ReservesLargeTitleFontsBeforeSizingCovers) {
  expectFits(456, 620, 32);
  expectFits(776, 300, 32);
  EXPECT_EQ(makeLayout(776, 300, 32).coverWidth, 36);
  EXPECT_EQ(makeLayout(776, 300, 32).coverHeight, 54);
}

TEST(LibraryShelfLayout, RejectsSurfacesWithoutRoomForThreeRowsAndTitles) {
  expectEmpty(makeLayout(0, 800, 16));
  expectEmpty(makeLayout(480, 0, 16));
  expectEmpty(makeLayout(-1, 800, 16));
  expectEmpty(makeLayout(480, -1, 16));
  expectEmpty(makeLayout(1, 1, 0));
  expectEmpty(makeLayout(32, 800, 16));
  expectEmpty(makeLayout(480, 64, 16));
  expectEmpty(makeLayout(480, 800, 300));
  expectEmpty(makeLayout(480, 800, 16, 401));
}

TEST(LibraryShelfLayout, NormalizesNegativeSpacingAndTitleHeight) {
  expectFits(456, 620, -1, -8);
  expectFits(456, 620, 0, 0);
}

TEST(LibraryShelfLayout, HandlesExtremeIntegersWithoutIntermediateOverflow) {
  expectEmpty(makeLayout(INT_MIN, INT_MAX, 16));
  expectEmpty(makeLayout(INT_MAX, INT_MIN, 16));
  expectEmpty(makeLayout(INT_MAX, INT_MAX, INT_MAX));
  expectEmpty(makeLayout(INT_MAX, INT_MAX, 16, INT_MAX));
  const Layout layout = makeLayout(INT_MAX, INT_MAX, 16);
  EXPECT_GT(layout.coverWidth, 0);
  EXPECT_LE(static_cast<long long>(layout.coverHeight) * 2, static_cast<long long>(layout.coverWidth) * 3);
  EXPECT_LE(static_cast<long long>(layout.rowHeight) * ROWS + layout.rowGap * (ROWS - 1), INT_MAX);
  EXPECT_EQ(pageStart(INT_MAX, INT_MAX), ((INT_MAX - 1) / PAGE_SIZE) * PAGE_SIZE);
  EXPECT_EQ(nextPage(INT_MAX, INT_MAX), (INT_MAX - 1) % PAGE_SIZE);
  EXPECT_EQ(previousPage(INT_MIN, INT_MAX), pageStart(INT_MAX - 1, INT_MAX));
  EXPECT_EQ(visibleCount(INT_MIN, INT_MAX), PAGE_SIZE);
}

TEST(LibraryShelfPagination, EmptyOrInvalidLibrariesKeepSelectionAtZero) {
  for (const int count : {0, -1, INT_MIN}) {
    EXPECT_EQ(pageStart(11, count), 0);
    EXPECT_EQ(visibleCount(0, count), 0);
    EXPECT_EQ(nextPage(11, count), 0);
    EXPECT_EQ(previousPage(11, count), 0);
  }
}

TEST(LibraryShelfPagination, CountsOnlyAvailableSlots) {
  for (const int count : {1, 4, 11, 12, 13, 24, 25, 4096}) {
    EXPECT_EQ(visibleCount(0, count), count < PAGE_SIZE ? count : PAGE_SIZE);
    EXPECT_EQ(visibleCount(-1, count), visibleCount(0, count));
    EXPECT_EQ(visibleCount(count, count), 0);
    EXPECT_EQ(visibleCount(count + 1, count), 0);
    EXPECT_EQ(visibleCount(count - 1, count), 1);
  }
  EXPECT_EQ(visibleCount(12, 13), 1);
  EXPECT_EQ(visibleCount(12, 24), 12);
  EXPECT_EQ(visibleCount(24, 25), 1);
  EXPECT_EQ(visibleCount(4092, 4096), 4);
}

TEST(LibraryShelfPagination, PreservesOffsetAndWrapsAcrossCompletePages) {
  EXPECT_EQ(nextPage(5, 24), 17);
  EXPECT_EQ(nextPage(17, 24), 5);
  EXPECT_EQ(previousPage(17, 24), 5);
  EXPECT_EQ(previousPage(5, 24), 17);
  EXPECT_EQ(nextPage(0, 1), 0);
  EXPECT_EQ(previousPage(3, 4), 3);
  EXPECT_EQ(nextPage(10, 11), 10);
  EXPECT_EQ(previousPage(11, 12), 11);
}

TEST(LibraryShelfPagination, ClampsOffsetOnIncompleteLastPage) {
  EXPECT_EQ(nextPage(11, 13), 12);
  EXPECT_EQ(previousPage(11, 13), 12);
  EXPECT_EQ(nextPage(12, 13), 0);
  EXPECT_EQ(previousPage(12, 13), 0);
  EXPECT_EQ(nextPage(23, 25), 24);
  EXPECT_EQ(previousPage(11, 25), 24);
  EXPECT_EQ(nextPage(24, 25), 0);
  EXPECT_EQ(previousPage(24, 25), 12);
  EXPECT_EQ(nextPage(4091, 4096), 4095);
  EXPECT_EQ(nextPage(4095, 4096), 3);
  EXPECT_EQ(previousPage(11, 4096), 4095);
}

TEST(LibraryShelfPagination, NormalizesOutOfRangeSelectionBeforeNavigation) {
  EXPECT_EQ(pageStart(-1, 25), 0);
  EXPECT_EQ(pageStart(999, 25), 24);
  EXPECT_EQ(nextPage(-1, 25), 12);
  EXPECT_EQ(previousPage(-1, 25), 24);
  EXPECT_EQ(nextPage(999, 25), 0);
  EXPECT_EQ(previousPage(999, 25), 12);
}

TEST(LibraryShelfPagination, EverySelectionAndPageJumpRemainVisibleAndAligned) {
  for (const int count : {1, 4, 11, 12, 13, 24, 25, 4096}) {
    for (int selected = -2; selected <= count + 2; ++selected) {
      SCOPED_TRACE(::testing::Message() << "count=" << count << ", selected=" << selected);
      for (const int result :
           {boundedSelection(selected, count), nextPage(selected, count), previousPage(selected, count)}) {
        const int top = pageStart(result, count);
        EXPECT_GE(result, 0);
        EXPECT_LT(result, count);
        EXPECT_EQ(top % PAGE_SIZE, 0);
        EXPECT_GE(result, top);
        EXPECT_LT(result, top + visibleCount(top, count));
        EXPECT_LE(visibleCount(top, count), PAGE_SIZE);
      }
    }
  }
}
}  // namespace
