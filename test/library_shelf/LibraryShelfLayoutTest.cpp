#include <LibrarySeriesView/LibrarySeriesView.h>
#include <gtest/gtest.h>

#include <array>
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

TEST(LibrarySeriesView, SelectsHalfOpenRangesAtExactFilteredPositions) {
  constexpr uint16_t matches[] = {1, 3, 4, 5, 7, 8, 10, 12};
  const auto first = library::series::matchingRange(0, 4, matches, 8);
  EXPECT_EQ(first.first, 0);
  EXPECT_EQ(first.count, 2);
  const auto middle = library::series::matchingRange(4, 9, matches, 8);
  EXPECT_EQ(middle.first, 2);
  EXPECT_EQ(middle.count, 4);
  EXPECT_EQ(matches[middle.first], 4);
  EXPECT_EQ(matches[middle.first + middle.count - 1], 8);
  const auto last = library::series::matchingRange(9, 13, matches, 8);
  EXPECT_EQ(last.first, 6);
  EXPECT_EQ(last.count, 2);
  EXPECT_EQ(matches[last.first + last.count - 1], 12);
}

TEST(LibrarySeriesView, KeepsSparseThirteenBookSlicesInGlobalMatchSpace) {
  constexpr uint16_t matches[] = {1, 3, 4, 5, 7, 8, 10, 12};
  const auto range = library::series::matchingRange(6, 13, matches, 8);
  EXPECT_EQ(range.first, 4);
  EXPECT_EQ(range.count, 4);
  EXPECT_EQ(matches[range.first], 7);
  EXPECT_EQ(matches[range.first + 1], 8);
  EXPECT_EQ(matches[range.first + 2], 10);
  EXPECT_EQ(matches[range.first + 3], 12);
  EXPECT_EQ(library::shelf::visibleCount(0, range.count), 4);
}

TEST(LibrarySeriesView, ReturnsEmptyRangesForMissingOrInvalidInputs) {
  constexpr uint16_t matches[] = {1, 3, 8};
  for (const auto range :
       {library::series::matchingRange(0, 9, nullptr, 3), library::series::matchingRange(0, 9, matches, 0),
        library::series::matchingRange(8, 8, matches, 3), library::series::matchingRange(9, 8, matches, 3)}) {
    EXPECT_EQ(range.first, 0);
    EXPECT_EQ(range.count, 0);
  }
  const auto gap = library::series::matchingRange(4, 8, matches, 3);
  EXPECT_EQ(gap.first, 2);
  EXPECT_EQ(gap.count, 0);
  const auto pastEnd = library::series::matchingRange(9, 12, matches, 3);
  EXPECT_EQ(pastEnd.first, 3);
  EXPECT_EQ(pastEnd.count, 0);
}

TEST(LibrarySeriesView, IncludesWholeLibraryAndIncompleteLastBucketAtFormatCapacity) {
  std::array<uint16_t, 4096> matches{};
  for (uint16_t row = 0; row < matches.size(); ++row) matches[row] = row;
  const auto all = library::series::matchingRange(0, 4096, matches.data(), matches.size());
  EXPECT_EQ(all.first, 0);
  EXPECT_EQ(all.count, 4096);
  const auto last = library::series::matchingRange(4092, 4096, matches.data(), matches.size());
  EXPECT_EQ(last.first, 4092);
  EXPECT_EQ(last.count, 4);
  EXPECT_EQ(matches[last.first + last.count - 1], 4095);
}

TEST(LibrarySeriesView, MapsAscendingAndDescendingEntriesToGlobalGroups) {
  EXPECT_EQ(library::series::groupForEntry(0, 4096, false), 0);
  EXPECT_EQ(library::series::groupForEntry(4095, 4096, false), 4095);
  EXPECT_EQ(library::series::groupForEntry(0, 4096, true), 4095);
  EXPECT_EQ(library::series::groupForEntry(4095, 4096, true), 0);
  constexpr uint16_t groups[] = {2, 8, 17};
  EXPECT_EQ(library::series::groupForEntry(0, 3, false, groups), 2);
  EXPECT_EQ(library::series::groupForEntry(1, 3, false, groups), 8);
  EXPECT_EQ(library::series::groupForEntry(2, 3, false, groups), 17);
  EXPECT_EQ(library::series::groupForEntry(0, 3, true, groups), 17);
  EXPECT_EQ(library::series::groupForEntry(1, 3, true, groups), 8);
  EXPECT_EQ(library::series::groupForEntry(2, 3, true, groups), 2);
}

TEST(LibrarySeriesView, RejectsInvalidIntegerInputsBeforeAccessingFilteredGroups) {
  constexpr uint16_t groups[] = {2, 8, 17};
  for (const int entry : {INT_MIN, -1, 3, INT_MAX}) {
    EXPECT_EQ(library::series::groupForEntry(entry, 3, false, groups), UINT16_MAX);
    EXPECT_EQ(library::series::groupForEntry(entry, 3, true, groups), UINT16_MAX);
  }
  for (const int count : {INT_MIN, -1, 0, INT_MAX}) {
    EXPECT_EQ(library::series::groupForEntry(0, count, false, groups), UINT16_MAX);
    EXPECT_EQ(library::series::groupForEntry(0, count, true, groups), UINT16_MAX);
  }
}

TEST(LibrarySeriesView, PreservesStripFocusEvenWhenFolderIdentitySurvives) {
  constexpr uint16_t groups[] = {2, 8, 17};
  EXPECT_EQ(library::series::restoredFolderRing(0, 17, 3, false, groups), 0);
  EXPECT_EQ(library::series::restoredFolderRing(0, 17, 3, true, groups), 0);
  EXPECT_EQ(library::series::restoredFolderRing(0, 4095, 4096, false), 0);
}

TEST(LibrarySeriesView, RestoresFolderIdentityAfterFilteringAndSortDirectionChange) {
  constexpr uint16_t groups[] = {2, 8, 17};
  EXPECT_EQ(library::series::restoredFolderRing(18, 17, 3, false, groups), 3);
  EXPECT_EQ(library::series::restoredFolderRing(18, 17, 3, true, groups), 1);
  EXPECT_EQ(library::series::restoredFolderRing(3, 2, 3, false, groups), 1);
  EXPECT_EQ(library::series::restoredFolderRing(1, 2, 3, true, groups), 3);
  EXPECT_EQ(library::series::restoredFolderRing(2, 8, 3, false, groups), 2);
  EXPECT_EQ(library::series::restoredFolderRing(2, 8, 3, true, groups), 2);
  EXPECT_EQ(library::series::restoredFolderRing(4096, 4095, 4096, true), 1);
}

TEST(LibrarySeriesView, ClampsMissingFoldersAndEmptyLibrariesWithoutIntegerOverflow) {
  constexpr uint16_t groups[] = {2, 8, 17};
  EXPECT_EQ(library::series::restoredFolderRing(2, 9, 3, false, groups), 2);
  EXPECT_EQ(library::series::restoredFolderRing(10, 9, 3, true, groups), 3);
  EXPECT_EQ(library::series::restoredFolderRing(INT_MAX, 9, 3, false, groups), 3);
  EXPECT_EQ(library::series::restoredFolderRing(INT_MIN, 9, 3, true, groups), 0);
  EXPECT_EQ(library::series::restoredFolderRing(10, UINT16_MAX, 3, false, groups), 3);
  for (const int count : {INT_MIN, -1, 0, INT_MAX})
    EXPECT_EQ(library::series::restoredFolderRing(INT_MAX, 17, count, false, groups), 0);
}

constexpr uint16_t STATIC_MATCHES[] = {2, 4, 7};
static_assert(library::series::matchingRange(3, 7, STATIC_MATCHES, 3).first == 1);
static_assert(library::series::matchingRange(3, 7, STATIC_MATCHES, 3).count == 1);
static_assert(library::series::groupForEntry(0, 3, true, STATIC_MATCHES) == 7);
static_assert(library::series::restoredFolderRing(2, 7, 3, true, STATIC_MATCHES) == 1);
}  // namespace
