#pragma once

#include <cstdint>

namespace library::series {

struct MatchRange {
  uint16_t first = 0;
  uint16_t count = 0;
};

// Positions into the sorted match array for the half-open series row range.
constexpr MatchRange matchingRange(const uint16_t firstRow, const uint16_t endRow, const uint16_t* sortedMatches,
                                   const uint16_t matchCount) {
  if (!sortedMatches || matchCount == 0 || firstRow >= endRow) return {};
  uint16_t low = 0;
  uint16_t high = matchCount;
  while (low < high) {
    const uint16_t middle = static_cast<uint16_t>(low + (high - low) / 2);
    if (sortedMatches[middle] < firstRow)
      low = static_cast<uint16_t>(middle + 1);
    else
      high = middle;
  }
  const uint16_t first = low;
  high = matchCount;
  while (low < high) {
    const uint16_t middle = static_cast<uint16_t>(low + (high - low) / 2);
    if (sortedMatches[middle] < endRow)
      low = static_cast<uint16_t>(middle + 1);
    else
      high = middle;
  }
  return {first, static_cast<uint16_t>(low - first)};
}

constexpr uint16_t groupForEntry(const int entry, const int visibleCount, const bool descending,
                                 const uint16_t* filteredGroupIds = nullptr) {
  if (visibleCount <= 0 || visibleCount > UINT16_MAX || entry < 0 || entry >= visibleCount) return UINT16_MAX;
  const int position = descending ? visibleCount - 1 - entry : entry;
  return filteredGroupIds ? filteredGroupIds[position] : static_cast<uint16_t>(position);
}

// Preserve the strip sentinel; a surviving folder follows its identity even
// when a query or sort direction changes its displayed position.
constexpr int restoredFolderRing(const int savedRing, const uint16_t previousGroup, const int visibleCount,
                                 const bool descending, const uint16_t* filteredGroupIds = nullptr) {
  if (savedRing == 0 || visibleCount <= 0 || visibleCount > UINT16_MAX) return 0;
  if (previousGroup != UINT16_MAX) {
    for (int entry = 0; entry < visibleCount; ++entry) {
      if (groupForEntry(entry, visibleCount, descending, filteredGroupIds) == previousGroup) return entry + 1;
    }
  }
  return savedRing < 0 ? 0 : (savedRing > visibleCount ? visibleCount : savedRing);
}

}  // namespace library::series
