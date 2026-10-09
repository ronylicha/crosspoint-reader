#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "Epub.h"
#include "LibraryBuilder.h"
#include "LibraryIndexFile.h"
#include "LibraryText.h"

using namespace library;

namespace {

constexpr char INDEX[] = "/.crosspoint/library.idx";

std::string numbered(const char* prefix, const unsigned value) {
  char text[32];
  std::snprintf(text, sizeof(text), "%s%04u", prefix, value);
  return text;
}

std::string pathAt(LibraryIndexFile& index, const SortOrder order, const uint16_t row) {
  const uint16_t ordinal = index.ordinalForRow(order, row);
  if (ordinal == 0xFFFF) return {};
  ClixRecord record{};
  if (!index.readRecord(ordinal, record)) return {};
  std::string path;
  return index.readPath(record, path) ? path : std::string();
}

// A legacy fixture has only its original permutations and three blob fields.
std::vector<uint8_t> asLegacyV2(const std::vector<uint8_t>& current) {
  ClixHeader header{};
  std::memcpy(&header, current.data(), sizeof(header));
  const uint32_t oldNameStart = header.nameStart;
  std::vector<ClixRecord> records(header.bookCount);
  std::vector<uint8_t> blob;
  blob.reserve(header.nameLen);
  for (uint16_t ordinal = 0; ordinal < header.bookCount; ++ordinal) {
    auto& record = records[ordinal];
    std::memcpy(&record, current.data() + recordOffset(header, ordinal), sizeof(record));
    const uint32_t begin = oldNameStart + record.nameOff;
    uint32_t end = begin + sizeof(uint64_t) + record.nameLen;
    for (int field = 0; field < 3; ++field) end += 1u + current[end];
    record.nameOff = static_cast<uint32_t>(blob.size());
    blob.insert(blob.end(), current.begin() + begin, current.begin() + end);
  }
  header.formatVersion = CLIX_LEGACY_FORMAT_VERSION;
  header.seriesGroupsStart = 0;
  header.seriesGroupCount = 0;
  header.nameStart = alignUp(header.permStart + static_cast<uint32_t>(header.bookCount) * 4);
  header.nameLen = static_cast<uint32_t>(blob.size());
  header.selfSize = header.nameStart + header.nameLen;
  std::vector<uint8_t> result(header.selfSize);
  std::memcpy(result.data(), &header, sizeof(header));
  std::copy_n(current.data() + header.folderStart, header.folderLen, result.data() + header.folderStart);
  for (uint16_t ordinal = 0; ordinal < header.bookCount; ++ordinal) {
    std::memcpy(result.data() + recordOffset(header, ordinal), &records[ordinal], sizeof(ClixRecord));
  }
  std::copy_n(current.data() + header.permStart, static_cast<size_t>(header.bookCount) * 4,
              result.data() + header.permStart);
  std::copy(blob.begin(), blob.end(), result.begin() + header.nameStart);
  return result;
}

void expectSeriesDirectory(LibraryIndexFile& index, const uint16_t expectedGroups) {
  ASSERT_EQ(index.seriesGroupCount(), expectedGroups);
  if (index.bookCount() == 0) return;
  ASSERT_GT(expectedGroups, 0);
  ASSERT_EQ(index.seriesGroupStart(0), 0);
  for (uint16_t group = 0; group < expectedGroups; ++group) {
    const uint16_t start = index.seriesGroupStart(group);
    const uint16_t end = group + 1 < expectedGroups ? index.seriesGroupStart(group + 1) : index.bookCount();
    ASSERT_LT(start, end);
    ASSERT_LE(end, index.bookCount());
    std::string groupKey;
    for (uint16_t row = start; row < end; ++row) {
      ClixRecord record{};
      ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::SeriesAsc, row), record));
      std::string series;
      ASSERT_TRUE(index.readSeries(record, series));
      const std::string key = fold(series);
      if (row == start)
        groupKey = key;
      else
        EXPECT_EQ(key, groupKey) << group << ':' << row;
    }
  }
  EXPECT_EQ(index.seriesGroupStart(expectedGroups), index.bookCount());
  EXPECT_EQ(index.seriesGroupStart(static_cast<uint16_t>(expectedGroups + 1)), 0xFFFF);
}

void addSeriesBook(const std::string& path, const std::string& title, const std::string& series,
                   const std::optional<float> number = std::nullopt) {
  fake::add(path);
  auto& metadata = bookMetadata[path];
  metadata.title = title;
  metadata.series = series;
  metadata.seriesIndex = number;
}

class LibraryBuilderTest : public ::testing::Test {
 protected:
  BuildStats stats;

  void SetUp() override {
    fake::reset();
    bookMetadata.clear();
    fake::add("/a.epub");
    fake::add("/b.epub");
  }

  void initial() { ASSERT_TRUE(buildLibraryIndex("/", stats, true)); }
};

}  // namespace

TEST_F(LibraryBuilderTest, UnchangedRebuildReusesMetadataAndDoesNotReplaceIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 0u);
  EXPECT_EQ(stats.parsed, 0);
  EXPECT_EQ(stats.metadataReused, 2);
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}

TEST_F(LibraryBuilderTest, FolderHeavyUnchangedReconciliationIoScalesLinearly) {
  const auto measure = [this](const unsigned count) {
    fake::reset();
    bookMetadata.clear();
    for (unsigned i = 0; i < count; i++) {
      fake::add("/folder" + numbered("", i) + "/book.txt");
    }
    if (!buildLibraryIndex("/", stats, false)) {
      ADD_FAILURE() << "initial build failed for " << count << " books";
      return 0u;
    }
    fake::resetIoCounters();
    if (!buildLibraryIndex("/", stats, false)) {
      ADD_FAILURE() << "unchanged build failed for " << count << " books";
      return 0u;
    }
    EXPECT_EQ(stats.metadataReused, count);
    EXPECT_FALSE(stats.indexReplaced);
    return fake::reads + fake::seeks;
  };

  const unsigned smallIo = measure(128);
  const unsigned largeIo = measure(256);
  EXPECT_LT(largeIo, smallIo * 3u);
}

TEST_F(LibraryBuilderTest, DirectoryEntriesAreEnumeratedOnce) {
  fake::add("/folder/c.txt");

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));

  EXPECT_EQ(fake::directoryEntriesByPath["/a.epub"], 1u);
  EXPECT_EQ(fake::directoryEntriesByPath["/b.epub"], 1u);
  EXPECT_EQ(fake::directoryEntriesByPath["/folder"], 1u);
  EXPECT_EQ(fake::directoryEntriesByPath["/folder/c.txt"], 1u);
}

TEST_F(LibraryBuilderTest, DirectoryResumeFailureRetainsPreviousIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;
  fake::add("/aa-folder/c.txt");
  fake::failDirectorySeek = true;

  EXPECT_FALSE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}

TEST_F(LibraryBuilderTest, StagingAndIndexWritesAreBatched) {
  fake::reset();
  for (unsigned i = 0; i < 128; i++) fake::add("/book" + numbered("", i) + ".txt");

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));

  EXPECT_LT(fake::writesByPath["/.crosspoint/library.stage"], 64u);
  EXPECT_LT(fake::writesByPath["/.crosspoint/library.new"], 32u);
}

TEST_F(LibraryBuilderTest, ParentDuplicateTrackingSurvivesDirectoryRecursion) {
  fake::add("/folder/c.txt");
  fake::duplicateDirectoryEntry("/a.epub");

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));

  EXPECT_EQ(stats.books, 3);
  EXPECT_EQ(stats.duplicatesDropped, 1);
}

TEST_F(LibraryBuilderTest, TimestampAndSizeChangesParseOnlyTheChangedBook) {
  initial();
  fake::files["/a.epub"]->time++;
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.metadataReused, 1);

  fake::files["/b.epub"]->bytes.push_back('x');
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.metadataReused, 1);
}

TEST_F(LibraryBuilderTest, ZeroTimestampAndFailedExtractionAreNeverFresh) {
  fake::files["/a.epub"]->time = 0;
  bookMetadata["/b.epub"].success = false;
  initial();
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 2u);
  EXPECT_EQ(stats.metadataReused, 0);
  EXPECT_TRUE(stats.indexReplaced);
}

TEST_F(LibraryBuilderTest, MetadataModeChangesInvalidateCachedMetadata) {
  initial();
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(fake::parses, 0u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().metadataEnabled, 0);
  index.close();

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 2u);
}

TEST_F(LibraryBuilderTest, RebuildVotesFromSourceAuthorInsteadOfPriorCanonicalAuthor) {
  fake::add("/c.epub");
  bookMetadata["/a.epub"].author = "Victor Hugo";
  bookMetadata["/b.epub"].author = "Hugo Victor";
  bookMetadata["/c.epub"].author = "Hugo Victor";
  initial();
  ASSERT_TRUE(Storage.remove("/b.epub"));
  ASSERT_TRUE(Storage.remove("/c.epub"));
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 0u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ClixRecord record{};
  std::string author;
  ASSERT_TRUE(index.readRecord(0, record));
  ASSERT_TRUE(index.readAuthor(record, author));
  EXPECT_EQ(author, "Victor Hugo");
}

TEST_F(LibraryBuilderTest, EqualBasenamesInDifferentFoldersReconcileIndependently) {
  fake::add("/one/same.epub");
  fake::add("/two/same.epub");
  bookMetadata["/one/same.epub"].title = "One";
  bookMetadata["/two/same.epub"].title = "Two";
  initial();
  fake::files["/two/same.epub"]->time++;
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.metadataReused, 3);
}

TEST_F(LibraryBuilderTest, ArrivalOrderFollowsModificationTimeOverDiscoveryOrder) {
  // a and b exist with the default time; c lands with an older timestamp and d
  // with the newest, so file times, not walk or firstSeen order, decide.
  fake::add("/c.epub", "book c", /*time=*/0);
  fake::add("/d.epub", "book d", /*time=*/9);
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/c.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 1), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 2), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 3), "/d.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentDesc, 0), "/d.epub");
}

TEST_F(LibraryBuilderTest, AddedRemovedMovedAndRenamedBooksKeepArrivalOrder) {
  initial();
  fake::add("/c.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 1), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 2), "/c.epub");
  index.close();

  ASSERT_TRUE(Storage.remove("/b.epub"));
  ASSERT_TRUE(Storage.rename("/a.epub", "/moved.epub"));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.removed, 1);
  EXPECT_EQ(stats.renamed, 1);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/moved.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 1), "/c.epub");
  index.close();

  ASSERT_TRUE(Storage.rename("/moved.epub", "/renamed.epub"));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.renamed, 1);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/renamed.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 1), "/c.epub");
}

TEST_F(LibraryBuilderTest, WholeFolderRenameWithUniqueSizePreservesArrivalOrder) {
  fake::add("/old/unique.epub", "a uniquely sized book");
  initial();
  ASSERT_TRUE(Storage.mkdir("/new"));
  ASSERT_TRUE(Storage.rename("/old/unique.epub", "/new/unique.epub"));
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(stats.renamed, 1);
  EXPECT_EQ(stats.removed, 0);
  EXPECT_EQ(fake::parses, 1u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 2), "/new/unique.epub");
}

TEST_F(LibraryBuilderTest, DuplicateDetectionRemainsBoundedAndFindsTrackedKeysAfterTheCap) {
  fake::duplicateDirectoryEntry("/a.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.books, 2);
  EXPECT_EQ(stats.duplicatesDropped, 1);
  EXPECT_FALSE(stats.dedupDegraded);

  fake::reset();
  bookMetadata.clear();
  for (unsigned i = 0; i <= LIBRARY_MAX_DEDUP_KEYS; i++) {
    fake::add("/book" + numbered("", i) + ".txt");
  }
  fake::duplicateDirectoryEntry("/book0000.txt");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(stats.books, LIBRARY_MAX_DEDUP_KEYS + 1);
  EXPECT_EQ(stats.duplicatesDropped, 1);
  EXPECT_TRUE(stats.dedupDegraded);
  EXPECT_LT(fake::delays, 2000u);
}

TEST_F(LibraryBuilderTest, ReadWriteCloseAndAllocationFailuresRetainPreviousIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;

  fake::failRead = 0;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  fake::failRead = -1;

  fake::files["/a.epub"]->time++;
  fake::failWrite = 0;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  fake::failWrite = -1;

  fake::failWritePath = "/.crosspoint/library.new";
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);

  fake::failClosePath = "/.crosspoint/library.new";
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);

  fake::failAlloc = 3;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  fake::failAlloc = -1;

  fake::failRename = 1;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}

TEST_F(LibraryBuilderTest, TruncatedPersistedPathHashAbortsAndRetainsTheLiveIndex) {
  initial();
  auto& bytes = fake::files[INDEX]->bytes;
  ClixHeader header{};
  std::memcpy(&header, bytes.data(), sizeof(header));
  ClixRecord record{};
  std::memcpy(&record, bytes.data() + recordOffset(header, 0), sizeof(record));
  record.nameOff = header.nameLen - 4;
  std::memcpy(bytes.data() + recordOffset(header, 0), &record, sizeof(record));
  const auto corrupted = bytes;

  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, corrupted);
  EXPECT_FALSE(Storage.exists("/.crosspoint/library.stage"));
  EXPECT_FALSE(Storage.exists("/.crosspoint/library.stage.f"));
}

TEST_F(LibraryBuilderTest, LibrariesPastOldGateAndAtFormatCeilingKeepAllOrders) {
  for (const unsigned count : {513u, static_cast<unsigned>(CLIX_MAX_RECORDS)}) {
    fake::reset();
    bookMetadata.clear();
    std::vector<unsigned> authorOrder(count);
    std::vector<unsigned> seriesOrder(count);
    std::iota(authorOrder.begin(), authorOrder.end(), 0u);
    std::iota(seriesOrder.begin(), seriesOrder.end(), 0u);
    for (unsigned i = 0; i < count; i++) {
      const std::string path = "/book" + numbered("", i) + ".epub";
      fake::add(path);
      bookMetadata[path].title = numbered("Title ", count - 1 - i);
      bookMetadata[path].author = numbered("Writer ", (i * (count == 513 ? 257u : 2053u)) % count);
      bookMetadata[path].series = i % 17 == 16 ? "" : numbered("Series ", i % 17);
      if (i % 5 != 0) bookMetadata[path].seriesIndex = static_cast<float>(count - i) / 2;
    }

    ASSERT_TRUE(buildLibraryIndex("/", stats, true)) << count;
    ASSERT_EQ(stats.books, count);
    EXPECT_FALSE(stats.ranksDegraded);

    if (count == CLIX_MAX_RECORDS) {
      const auto old = fake::files[INDEX]->bytes;
      fake::parses = 0;
      fake::resetIoCounters();
      ASSERT_TRUE(buildLibraryIndex("/", stats, true));
      EXPECT_EQ(fake::parses, 0u);
      EXPECT_EQ(stats.metadataReused, CLIX_MAX_RECORDS);
      EXPECT_FALSE(stats.indexReplaced);
      EXPECT_EQ(fake::files[INDEX]->bytes, old);
      EXPECT_LT(fake::delays, 10000u);
    }

    std::sort(authorOrder.begin(), authorOrder.end(), [count](const unsigned a, const unsigned b) {
      return (a * (count == 513 ? 257u : 2053u)) % count < (b * (count == 513 ? 257u : 2053u)) % count;
    });
    std::sort(seriesOrder.begin(), seriesOrder.end(), [](const unsigned a, const unsigned b) {
      const auto& left = bookMetadata["/book" + numbered("", a) + ".epub"];
      const auto& right = bookMetadata["/book" + numbered("", b) + ".epub"];
      if (left.series.empty() != right.series.empty()) return !left.series.empty();
      if (left.series != right.series) return left.series < right.series;
      if (!left.series.empty()) {
        if (left.seriesIndex.has_value() != right.seriesIndex.has_value()) return left.seriesIndex.has_value();
        if (left.seriesIndex && *left.seriesIndex != *right.seriesIndex) return *left.seriesIndex < *right.seriesIndex;
      }
      return left.title < right.title;
    });
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(INDEX));
    std::vector<bool> seriesSeen(count);
    for (uint16_t row = 0; row < count; row++) {
      EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, row), "/book" + numbered("", row) + ".epub") << count << ':' << row;
      EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, row), "/book" + numbered("", count - 1 - row) + ".epub")
          << count << ':' << row;
      EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, row), "/book" + numbered("", authorOrder[row]) + ".epub")
          << count << ':' << row;
      EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, row), "/book" + numbered("", seriesOrder[row]) + ".epub")
          << count << ':' << row;
      const uint16_t ordinal = index.ordinalForRow(SortOrder::SeriesAsc, row);
      ASSERT_LT(ordinal, count);
      EXPECT_FALSE(seriesSeen[ordinal]);
      seriesSeen[ordinal] = true;
    }
    EXPECT_TRUE(std::all_of(seriesSeen.begin(), seriesSeen.end(), [](const bool seen) { return seen; }));
    expectSeriesDirectory(index, 17);
  }
}

TEST_F(LibraryBuilderTest, SortAllocationFailureProducesValidDegradedIndex) {
  fake::reset();
  bookMetadata.clear();
  for (unsigned i = 0; i < CLIX_MAX_RECORDS; i++) {
    const std::string path = "/book" + numbered("", i) + ".epub";
    fake::add(path);
    bookMetadata[path].title = numbered("Title ", CLIX_MAX_RECORDS - i);
    bookMetadata[path].series = i % 8 == 7 ? "" : numbered("Series ", i % 8);
    bookMetadata[path].seriesIndex = static_cast<float>(CLIX_MAX_RECORDS - i) / 2;
  }
  fake::failAlloc = 6 + 2 * CLIX_MAX_RECORDS;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_TRUE(fake::failureTriggered);
  EXPECT_TRUE(stats.ranksDegraded);
  EXPECT_TRUE(stats.indexReplaced);

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.bookCount(), CLIX_MAX_RECORDS);
  expectSeriesDirectory(index, 8);
  std::vector<bool> seen(CLIX_MAX_RECORDS);
  for (uint16_t group = 0; group < index.seriesGroupCount(); ++group) {
    const uint16_t start = index.seriesGroupStart(group);
    const uint16_t end = group + 1 < index.seriesGroupCount() ? index.seriesGroupStart(group + 1) : index.bookCount();
    float lastNumber = -std::numeric_limits<float>::infinity();
    std::string lastTitle;
    for (uint16_t row = start; row < end; ++row) {
      const uint16_t ordinal = index.ordinalForRow(SortOrder::SeriesAsc, row);
      ASSERT_LT(ordinal, seen.size());
      EXPECT_FALSE(seen[ordinal]);
      seen[ordinal] = true;
      ClixRecord record{};
      ASSERT_TRUE(index.readRecord(ordinal, record));
      std::string series;
      std::string title;
      std::optional<float> number;
      ASSERT_TRUE(index.readSeries(record, series));
      ASSERT_TRUE(index.readSeriesIndex(record, number));
      ASSERT_TRUE(index.readTitle(record, title));
      if (series.empty()) {
        EXPECT_FALSE(number.has_value());
        EXPECT_LE(lastTitle, title);
        lastTitle = title;
      } else {
        ASSERT_TRUE(number.has_value());
        EXPECT_LE(lastNumber, *number);
        lastNumber = *number;
      }
    }
  }
  EXPECT_TRUE(std::all_of(seen.begin(), seen.end(), [](const bool present) { return present; }));
}

TEST_F(LibraryBuilderTest, SeriesFoldersUseCompleteFoldedNamesAndNumericVolumeOrder) {
  fake::reset();
  bookMetadata.clear();
  const std::string firstSeries = "Common Prefix 12 Alpha";
  const std::string secondSeries = "Common Prefix 12 Beta";
  addSeriesBook("/a0.epub", "Zero", firstSeries, 0);
  addSeriesBook("/a1.epub", "One", firstSeries, 1);
  addSeriesBook("/a2z.epub", "Zeta", firstSeries, 2);
  addSeriesBook("/a2a.epub", "Alpha", firstSeries, 2);
  addSeriesBook("/a35.epub", "Fraction", firstSeries, 3.5f);
  addSeriesBook("/a10.epub", "Ten", firstSeries, 10);
  addSeriesBook("/a-none-z.epub", "Zulu", firstSeries);
  addSeriesBook("/a-none-a.epub", "Alpha", firstSeries);
  addSeriesBook("/b1.epub", "Other series", secondSeries, 1);
  addSeriesBook("/unicode-z.epub", "Beta", "Épopée", 1);
  addSeriesBook("/unicode-a.epub", "Alpha", "E\u0301pope\u0301e", 1);
  addSeriesBook("/none-z.epub", "Zulu", "", 99);
  addSeriesBook("/none-a.epub", "Alpha", "");

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 13u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  expectSeriesDirectory(index, 4);
  EXPECT_EQ(index.seriesGroupStart(1), 8);
  EXPECT_EQ(index.seriesGroupStart(2), 9);
  EXPECT_EQ(index.seriesGroupStart(3), 11);
  const std::vector<std::string> expected = {"/a0.epub",    "/a1.epub",        "/a2a.epub",       "/a2z.epub",
                                             "/a35.epub",   "/a10.epub",       "/a-none-a.epub",  "/a-none-z.epub",
                                             "/b1.epub",    "/unicode-a.epub", "/unicode-z.epub", "/none-a.epub",
                                             "/none-z.epub"};
  for (uint16_t row = 0; row < expected.size(); ++row) {
    EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, row), expected[row]);
  }
  const std::vector<std::optional<float>> expectedNumbers = {
      0, 1, 2, 2, 3.5f, 10, std::nullopt, std::nullopt, 1, 1, 1, std::nullopt, std::nullopt};
  for (uint16_t row = 0; row < expectedNumbers.size(); ++row) {
    ClixRecord record{};
    ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::SeriesAsc, row), record));
    std::optional<float> number;
    ASSERT_TRUE(index.readSeriesIndex(record, number));
    EXPECT_EQ(number, expectedNumbers[row]);
  }
  ClixRecord unicode{};
  ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::SeriesAsc, 9), unicode));
  std::string name;
  ASSERT_TRUE(index.readSeries(unicode, name));
  EXPECT_EQ(name, "Épopée");
}

TEST_F(LibraryBuilderTest, SeriesUnchangedRebuildReusesAllMetadataAndReplacesNothing) {
  bookMetadata["/a.epub"].series = "Cycle";
  bookMetadata["/a.epub"].seriesIndex = 3.5f;
  bookMetadata["/b.epub"].series = "Cycle";
  initial();
  const auto old = fake::files[INDEX]->bytes;
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 0u);
  EXPECT_EQ(stats.metadataReused, 2);
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  expectSeriesDirectory(index, 1);
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 0), "/a.epub");
}

TEST_F(LibraryBuilderTest, MetadataDisabledPutsEveryBookInUnclassifiedFolderWithoutParsing) {
  bookMetadata["/a.epub"].series = "Cycle";
  bookMetadata["/a.epub"].seriesIndex = 2;
  bookMetadata["/b.epub"].series = "Other";
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(fake::parses, 0u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  expectSeriesDirectory(index, 1);
  for (uint16_t ordinal = 0; ordinal < index.bookCount(); ++ordinal) {
    ClixRecord record{};
    ASSERT_TRUE(index.readRecord(ordinal, record));
    std::string series;
    std::optional<float> number;
    ASSERT_TRUE(index.readSeries(record, series));
    ASSERT_TRUE(index.readSeriesIndex(record, number));
    EXPECT_TRUE(series.empty());
    EXPECT_FALSE(number.has_value());
  }
}

TEST_F(LibraryBuilderTest, SeriesNamesAreUtf8SafeAndNonFiniteNumbersAreAbsent) {
  fake::reset();
  bookMetadata.clear();
  addSeriesBook("/truncated.epub", "A", std::string(254, 'a') + "é ending", 1);
  addSeriesBook("/exact.epub", "B", std::string(253, 'b') + "é ending", 2);
  addSeriesBook("/nan.epub", "C", "Cycle", std::numeric_limits<float>::quiet_NaN());
  addSeriesBook("/positive-inf.epub", "D", "Cycle", std::numeric_limits<float>::infinity());
  addSeriesBook("/negative-inf.epub", "E", "Cycle", -std::numeric_limits<float>::infinity());
  addSeriesBook("/punctuation.epub", "F", " -- ", 3);
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  expectSeriesDirectory(index, 4);
  for (uint16_t ordinal = 0; ordinal < index.bookCount(); ++ordinal) {
    ClixRecord record{};
    ASSERT_TRUE(index.readRecord(ordinal, record));
    std::string path;
    std::string series;
    std::optional<float> number;
    ASSERT_TRUE(index.readPath(record, path));
    ASSERT_TRUE(index.readSeries(record, series));
    ASSERT_TRUE(index.readSeriesIndex(record, number));
    if (path == "/truncated.epub") {
      EXPECT_EQ(series, std::string(254, 'a'));
      EXPECT_EQ(number, 1);
    } else if (path == "/exact.epub") {
      EXPECT_EQ(series, std::string(253, 'b') + "é");
      EXPECT_EQ(series.size(), 255u);
      EXPECT_EQ(number, 2);
    } else {
      EXPECT_FALSE(number.has_value());
      EXPECT_EQ(series, path == "/punctuation.epub" ? "" : "Cycle");
    }
  }
}

TEST_F(LibraryBuilderTest, EmptyLibraryHasNoSeriesDirectory) {
  fake::reset();
  bookMetadata.clear();
  fake::add("/");
  fake::files["/"]->directory = true;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.bookCount(), 0);
  EXPECT_EQ(index.seriesGroupCount(), 0);
  EXPECT_EQ(index.seriesGroupStart(0), 0);
  EXPECT_EQ(index.seriesGroupStart(1), 0xFFFF);
}

TEST_F(LibraryBuilderTest, LegacyV2MigrationPreservesArrivalHistoryAndReparsesUnchangedBooksOnce) {
  bookMetadata["/a.epub"].title = "Alpha";
  bookMetadata["/b.epub"].title = "Beta";
  initial();
  auto& current = fake::files[INDEX]->bytes;
  ClixHeader header{};
  std::memcpy(&header, current.data(), sizeof(header));
  header.nextFirstSeen = 99;
  std::memcpy(current.data(), &header, sizeof(header));
  for (uint16_t ordinal = 0; ordinal < header.bookCount; ++ordinal) {
    ClixRecord record{};
    std::memcpy(&record, current.data() + recordOffset(header, ordinal), sizeof(record));
    record.firstSeen = ordinal == 0 ? 24 : 58;
    std::memcpy(current.data() + recordOffset(header, ordinal), &record, sizeof(record));
  }
  current = asLegacyV2(current);
  LibraryIndexFile index;
  EXPECT_FALSE(index.open(INDEX));
  ASSERT_TRUE(index.openForReconciliation(INDEX));
  EXPECT_EQ(index.header().formatVersion, 2);
  EXPECT_EQ(index.header().nameStart, alignUp(index.header().permStart + index.bookCount() * 4u));
  EXPECT_EQ(index.seriesGroupCount(), 0);
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/a.epub");
  index.close();
  bookMetadata["/a.epub"].series = "Fresh from OPF";
  bookMetadata["/a.epub"].seriesIndex = 2;
  bookMetadata["/b.epub"].series = "Fresh from OPF";
  bookMetadata["/b.epub"].seriesIndex = 1;
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 2u);
  EXPECT_EQ(stats.metadataReused, 0);
  EXPECT_TRUE(stats.indexReplaced);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().formatVersion, 3);
  EXPECT_EQ(index.header().nextFirstSeen, 99);
  ClixRecord first{};
  ClixRecord second{};
  ASSERT_TRUE(index.readRecord(0, first));
  ASSERT_TRUE(index.readRecord(1, second));
  EXPECT_EQ(first.firstSeen, 24);
  EXPECT_EQ(second.firstSeen, 58);
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 0), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/a.epub");
  expectSeriesDirectory(index, 1);
  index.close();
  const auto migrated = fake::files[INDEX]->bytes;
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 0u);
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, migrated);
}

TEST_F(LibraryBuilderTest, InterruptedInstallRestoresLegacyBackupBeforeMigration) {
  initial();
  const auto legacy = asLegacyV2(fake::files[INDEX]->bytes);
  ASSERT_TRUE(Storage.rename(INDEX, "/.crosspoint/library.bak"));
  fake::files["/.crosspoint/library.bak"]->bytes = legacy;
  fake::add(INDEX, "interrupted v3 data");
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 2u);
  EXPECT_FALSE(Storage.exists("/.crosspoint/library.bak"));
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().formatVersion, 3);
  EXPECT_EQ(index.bookCount(), 2);
}

TEST_F(LibraryBuilderTest, LegacyMigrationRenameFailureKeepsOriginalIndexForRetry) {
  initial();
  const auto legacy = asLegacyV2(fake::files[INDEX]->bytes);
  for (const int failAt : {0, 1}) {
    fake::files[INDEX]->bytes = legacy;
    fake::failRename = failAt;
    EXPECT_FALSE(buildLibraryIndex("/", stats, true)) << failAt;
    EXPECT_TRUE(fake::failureTriggered);
    EXPECT_EQ(fake::files[INDEX]->bytes, legacy);
    EXPECT_FALSE(Storage.exists("/.crosspoint/library.bak"));
    EXPECT_FALSE(Storage.exists("/.crosspoint/library.new"));
  }
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().formatVersion, 3);
}

TEST_F(LibraryBuilderTest, AllocationFailuresDuringSeriesRebuildNeverPublishIncompleteIndex) {
  bookMetadata["/a.epub"].series = "Cycle";
  bookMetadata["/a.epub"].seriesIndex = 1;
  bookMetadata["/b.epub"].series = "Other";
  initial();
  const auto old = fake::files[INDEX]->bytes;
  // Sweep all existing allocation failure hooks across both data phases.
  for (int failAt = 0; failAt < 20; ++failAt) {
    fake::files[INDEX]->bytes = old;
    fake::files["/a.epub"]->time = 2;
    fake::failAlloc = failAt;
    fake::failureTriggered = false;
    const bool success = buildLibraryIndex("/", stats, true);
    fake::failAlloc = -1;
    if (!success) {
      EXPECT_EQ(fake::files[INDEX]->bytes, old) << failAt;
    } else {
      LibraryIndexFile index;
      ASSERT_TRUE(index.open(INDEX)) << failAt;
      expectSeriesDirectory(index, 2);
      EXPECT_EQ(index.bookCount(), 2);
    }
  }
}

TEST_F(LibraryBuilderTest, EqualSeriesAndVolumesKeepStableFileOrderAcrossRebuilds) {
  bookMetadata["/a.epub"].series = "Saga";
  bookMetadata["/a.epub"].seriesIndex = 2;
  bookMetadata["/a.epub"].title = "Same title";
  bookMetadata["/b.epub"].series = "saga";
  bookMetadata["/b.epub"].seriesIndex = 2;
  bookMetadata["/b.epub"].title = "Same title";
  initial();
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  expectSeriesDirectory(index, 1);
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 0), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 1), "/b.epub");
  index.close();
  fake::files["/a.epub"]->time = 3;
  fake::files["/b.epub"]->time = 2;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 0), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 1), "/b.epub");
}

TEST_F(LibraryBuilderTest, MissingLiveIndexKeepsLegacyBackupWhenRecoveryRenameFails) {
  initial();
  const auto legacy = asLegacyV2(fake::files[INDEX]->bytes);
  ASSERT_TRUE(Storage.rename(INDEX, "/.crosspoint/library.bak"));
  fake::files["/.crosspoint/library.bak"]->bytes = legacy;
  fake::failRename = 0;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_TRUE(fake::failureTriggered);
  EXPECT_FALSE(Storage.exists(INDEX));
  ASSERT_TRUE(Storage.exists("/.crosspoint/library.bak"));
  EXPECT_EQ(fake::files["/.crosspoint/library.bak"]->bytes, legacy);
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().formatVersion, 3);
  EXPECT_FALSE(Storage.exists("/.crosspoint/library.bak"));
}

TEST_F(LibraryBuilderTest, ReadFailuresAcrossSeriesSortAndEmitKeepThePreviousIndex) {
  bookMetadata["/a.epub"].series = "Cycle";
  bookMetadata["/a.epub"].seriesIndex = 2;
  bookMetadata["/b.epub"].series = "Cycle";
  bookMetadata["/b.epub"].seriesIndex = 1;
  initial();
  const auto old = fake::files[INDEX]->bytes;
  unsigned rejected = 0;
  for (int failAt = 0; failAt < 160; ++failAt) {
    fake::files[INDEX]->bytes = old;
    fake::files["/a.epub"]->time = 2;
    fake::failRead = failAt;
    fake::failureTriggered = false;
    const bool success = buildLibraryIndex("/", stats, true);
    fake::failRead = -1;
    if (!success) {
      ++rejected;
      EXPECT_TRUE(fake::failureTriggered) << failAt;
      EXPECT_EQ(fake::files[INDEX]->bytes, old) << failAt;
    } else {
      LibraryIndexFile index;
      ASSERT_TRUE(index.open(INDEX)) << failAt;
      expectSeriesDirectory(index, 1);
      EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, 0), "/b.epub");
    }
  }
  EXPECT_GT(rejected, 0u);
  EXPECT_LT(rejected, 160u);
}

TEST_F(LibraryBuilderTest, EmptyLegacyIndexMigratesOnceAndThenRemainsUnchanged) {
  fake::reset();
  bookMetadata.clear();
  fake::add("/");
  fake::files["/"]->directory = true;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  fake::files[INDEX]->bytes = asLegacyV2(fake::files[INDEX]->bytes);
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_TRUE(stats.indexReplaced);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().formatVersion, 3);
  EXPECT_EQ(index.seriesGroupCount(), 0);
  index.close();
  const auto migrated = fake::files[INDEX]->bytes;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, migrated);
}

TEST_F(LibraryBuilderTest, EmptyIndexMetadataModeChangeUpdatesItsHeader) {
  fake::reset();
  bookMetadata.clear();
  fake::add("/");
  fake::files["/"]->directory = true;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  for (const bool enabled : {true, false, true}) {
    ASSERT_TRUE(buildLibraryIndex("/", stats, enabled));
    EXPECT_TRUE(stats.indexReplaced);
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(INDEX));
    EXPECT_EQ(index.header().metadataEnabled, enabled);
    EXPECT_EQ(index.bookCount(), 0);
    EXPECT_EQ(fake::parses, 0u);
  }
}

TEST_F(LibraryBuilderTest, EmptyStaleFoldIndexRebuildsToCurrentFoldVersion) {
  fake::reset();
  bookMetadata.clear();
  fake::add("/");
  fake::files["/"]->directory = true;
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  auto& bytes = fake::files[INDEX]->bytes;
  ClixHeader header{};
  std::memcpy(&header, bytes.data(), sizeof(header));
  header.foldVersion = CLIX_FOLD_VERSION - 1;
  std::memcpy(bytes.data(), &header, sizeof(header));
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_TRUE(stats.indexReplaced);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().foldVersion, CLIX_FOLD_VERSION);
}
