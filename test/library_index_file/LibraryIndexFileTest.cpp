#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "LibraryIndexFile.h"

namespace library {

std::string joinLibraryPath(const std::string_view folder, const std::string_view name) {
  return std::string(folder) + "/" + std::string(name);
}

}  // namespace library

namespace {

std::vector<uint8_t> makeBlob(const uint64_t pathHash, const std::initializer_list<uint8_t> fields,
                              const uint8_t version = library::CLIX_FORMAT_VERSION) {
  std::vector<uint8_t> blob(sizeof(pathHash) + fields.size());
  std::memcpy(blob.data(), &pathHash, sizeof(pathHash));
  std::copy(fields.begin(), fields.end(), blob.begin() + sizeof(pathHash));
  if (version == library::CLIX_FORMAT_VERSION) blob.insert(blob.end(), {0, 0});
  return blob;
}

std::vector<uint8_t> seriesBlob(const std::string& series, const std::optional<float> number = std::nullopt) {
  auto blob = makeBlob(42, {'x', 1, 'a', 1, 't', 1, 'a'});
  blob.resize(blob.size() - 2);
  blob.push_back(static_cast<uint8_t>(series.size()));
  blob.insert(blob.end(), series.begin(), series.end());
  blob.push_back(number.has_value() ? 1 : 0);
  if (number) {
    uint8_t bytes[sizeof(float)];
    std::memcpy(bytes, &*number, sizeof(bytes));
    blob.insert(blob.end(), bytes, bytes + sizeof(bytes));
  }
  return blob;
}

struct IndexFixture {
  library::ClixHeader header{};
  std::vector<uint8_t> bytes;
  std::vector<library::ClixRecord> records;
};

IndexFixture fixture(const uint8_t version, const std::vector<std::vector<uint8_t>>& blobs,
                     const std::vector<uint16_t>& groups = {0}) {
  IndexFixture result;
  auto& header = result.header;
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = version;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = static_cast<uint16_t>(blobs.size());
  header.folderCount = 1;
  header.seriesGroupCount =
      version == library::CLIX_FORMAT_VERSION && !blobs.empty() ? static_cast<uint16_t>(groups.size()) : 0;
  size_t nameBytes = 0;
  for (const auto& blob : blobs) nameBytes += blob.size();
  const uint8_t folder[] = {6, '/', 'b', 'o', 'o', 'k', 's'};
  library::layoutSections(header, sizeof(folder), static_cast<uint32_t>(nameBytes));
  result.bytes.resize(header.selfSize);
  std::memcpy(result.bytes.data(), &header, sizeof(header));
  std::memcpy(result.bytes.data() + header.folderStart, folder, sizeof(folder));
  uint32_t offset = 0;
  result.records.reserve(blobs.size());
  for (uint16_t ordinal = 0; ordinal < header.bookCount; ++ordinal) {
    library::ClixRecord record{};
    record.nameLen = 1;
    record.nameOff = offset;
    record.fileSize = 100 + ordinal;
    result.records.push_back(record);
    std::memcpy(result.bytes.data() + library::recordOffset(header, ordinal), &record, sizeof(record));
    std::memcpy(result.bytes.data() + header.nameStart + offset, blobs[ordinal].data(), blobs[ordinal].size());
    std::memcpy(result.bytes.data() + library::authorOrderOffset(header, ordinal), &ordinal, sizeof(ordinal));
    std::memcpy(result.bytes.data() + library::arrivalOrderOffset(header, ordinal), &ordinal, sizeof(ordinal));
    if (version == library::CLIX_FORMAT_VERSION)
      std::memcpy(result.bytes.data() + library::seriesOrderOffset(header, ordinal), &ordinal, sizeof(ordinal));
    offset += static_cast<uint32_t>(blobs[ordinal].size());
  }
  if (version == library::CLIX_FORMAT_VERSION && !groups.empty())
    std::memcpy(result.bytes.data() + header.seriesGroupsStart, groups.data(), groups.size() * sizeof(uint16_t));
  return result;
}

}  // namespace

TEST(LibraryIndexFile, MissingIndexDoesNotCloseAnUninitializedHandle) {
  Storage.clearFile();
  HalFile::resetInvalidCloseCount();

  {
    library::LibraryIndexFile index;
    EXPECT_FALSE(index.open("/missing.clx"));
  }

  EXPECT_EQ(HalFile::invalidCloseCount(), 0);
}

TEST(LibraryIndexFile, ReadsEveryStoredOrderInBothDirections) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 3;
  header.seriesGroupCount = 1;
  library::layoutSections(header, 0, 0);

  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t authorOrder[] = {2, 0, 1};
  const uint16_t arrivalOrder[] = {1, 2, 0};
  const uint16_t seriesOrder[] = {2, 1, 0};
  std::memcpy(bytes.data() + library::authorOrderOffset(header, 0), authorOrder, sizeof(authorOrder));
  std::memcpy(bytes.data() + library::arrivalOrderOffset(header, 0), arrivalOrder, sizeof(arrivalOrder));
  std::memcpy(bytes.data() + library::seriesOrderOffset(header, 0), seriesOrder, sizeof(seriesOrder));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));

  const auto expectOrder = [&](const library::SortOrder order, const uint16_t a, const uint16_t b, const uint16_t c) {
    EXPECT_EQ(index.ordinalForRow(order, 0), a);
    EXPECT_EQ(index.ordinalForRow(order, 1), b);
    EXPECT_EQ(index.ordinalForRow(order, 2), c);
    EXPECT_EQ(index.ordinalForRow(order, 3), 0xFFFF);
  };
  expectOrder(library::SortOrder::RecentAsc, 1, 2, 0);
  expectOrder(library::SortOrder::RecentDesc, 0, 2, 1);
  expectOrder(library::SortOrder::TitleAsc, 0, 1, 2);
  expectOrder(library::SortOrder::TitleDesc, 2, 1, 0);
  expectOrder(library::SortOrder::AuthorAsc, 2, 0, 1);
  expectOrder(library::SortOrder::AuthorDesc, 1, 0, 2);
  expectOrder(library::SortOrder::SeriesAsc, 2, 1, 0);
}

TEST(LibraryIndexFile, ResolvesRecentRowsByIdentity) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 3;
  header.seriesGroupCount = 1;
  // Each v3 blob has a hash, name, legacy fields, and an empty series payload.
  constexpr size_t BLOB_SIZE = sizeof(uint64_t) + 1 + 3 + 2;
  library::layoutSections(header, 0, 3 * BLOB_SIZE);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));

  // Ordinals 0 and 2 share a size, so only the hash can tell them apart.
  constexpr uint64_t HASHES[] = {11, 22, 33};
  constexpr uint32_t SIZES[] = {100, 200, 100};
  for (uint16_t ordinal = 0; ordinal < 3; ordinal++) {
    library::ClixRecord record{};
    record.fileSize = SIZES[ordinal];
    record.nameOff = ordinal * BLOB_SIZE;
    record.nameLen = 1;
    std::memcpy(bytes.data() + library::recordOffset(header, ordinal), &record, sizeof(record));
    const auto blob = makeBlob(HASHES[ordinal], {'x', 0, 0, 0});
    std::memcpy(bytes.data() + header.nameStart + record.nameOff, blob.data(), blob.size());
  }
  const uint16_t arrivalOrder[] = {1, 2, 0};
  std::memcpy(bytes.data() + library::arrivalOrderOffset(header, 0), arrivalOrder, sizeof(arrivalOrder));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  const library::BookIdentity books[] = {
      {HASHES[0], SIZES[0]},  // ordinal 0 -> ascending row 2
      {HASHES[2], SIZES[2]},  // same size as ordinal 0, hash picks ordinal 2 -> row 1
      {99, SIZES[0]},         // size matches, hash does not: absent
      {HASHES[1], 999},       // hash matches, size does not: absent
      {HASHES[1], 0},         // size unknown: the hash alone matches -> row 0
  };
  uint16_t rows[5] = {};
  ASSERT_TRUE(index.recentRowsFor(books, 5, rows));
  EXPECT_EQ(rows[0], 2);
  EXPECT_EQ(rows[1], 1);
  EXPECT_EQ(rows[2], 0xFFFF);
  EXPECT_EQ(rows[3], 0xFFFF);
  EXPECT_EQ(rows[4], 0);
}

TEST(LibraryIndexFile, RejectsInvalidPermutationOrdinal) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  header.seriesGroupCount = 1;
  library::layoutSections(header, 0, 0);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t invalid = 1;
  std::memcpy(bytes.data() + library::authorOrderOffset(header, 0), &invalid, sizeof(invalid));
  std::memcpy(bytes.data() + library::seriesOrderOffset(header, 0), &invalid, sizeof(invalid));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0xFFFF);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::SeriesAsc, 0), 0xFFFF);
}

TEST(LibraryIndexFile, ReadsPathHashAndEveryPublicBlobField) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  header.seriesGroupCount = 1;
  const uint8_t folder[] = {6, '/', 'b', 'o', 'o', 'k', 's'};
  constexpr uint64_t PATH_HASH = 0x0123456789ABCDEFULL;
  const auto blob = makeBlob(PATH_HASH, {'x', 1, 'a', 1, 't', 8, 'O', 'r', 'i', 'g', 'i', 'n', 'a', 'l'});
  header.folderCount = 1;
  library::layoutSections(header, sizeof(folder), blob.size());
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.folderStart, folder, sizeof(folder));
  std::memcpy(bytes.data() + header.nameStart, blob.data(), blob.size());
  library::ClixRecord record{};
  record.nameLen = 1;
  std::memcpy(bytes.data() + library::recordOffset(header, 0), &record, sizeof(record));
  Storage.setFile("/library.clx", bytes);

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  uint64_t pathHash = 0;
  ASSERT_TRUE(index.readPathHash(record, pathHash));
  EXPECT_EQ(pathHash, PATH_HASH);
  std::string name;
  ASSERT_TRUE(index.readName(record, name));
  EXPECT_EQ(name, "x");
  std::string author;
  ASSERT_TRUE(index.readAuthor(record, author));
  EXPECT_EQ(author, "a");
  std::string title;
  ASSERT_TRUE(index.readTitle(record, title));
  EXPECT_EQ(title, "t");
  ASSERT_TRUE(index.readSourceAuthor(record, author));
  EXPECT_EQ(author, "Original");
  std::string path;
  ASSERT_TRUE(index.readPath(record, path));
  EXPECT_EQ(path, "/books/x");
  index.close();

  bytes[header.nameStart + sizeof(PATH_HASH) + 5] = 255;
  Storage.setFile("/library.clx", std::move(bytes));
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_FALSE(index.readSourceAuthor(record, author));
}

TEST(LibraryIndexFile, RejectsTruncatedAndOverflowingPathHashes) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  header.seriesGroupCount = 1;
  library::layoutSections(header, 0, sizeof(uint64_t) - 1);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  uint64_t hash = 1;
  EXPECT_FALSE(index.readPathHash(record, hash));
  EXPECT_EQ(hash, 0u);
  EXPECT_TRUE(index.ioFailed());

  record.nameOff = UINT32_MAX;
  EXPECT_FALSE(index.readPathHash(record, hash));
}

TEST(LibraryIndexFile, RejectsFolderRecordBeyondFolderBlob) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  header.seriesGroupCount = 1;
  const uint8_t folder[] = {5, '/'};
  const auto blob = makeBlob(1, {'x', 0, 0, 0});
  library::layoutSections(header, sizeof(folder), blob.size());
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.folderStart, folder, sizeof(folder));
  std::memcpy(bytes.data() + header.nameStart, blob.data(), blob.size());
  library::ClixRecord record{};
  record.nameLen = 1;
  std::memcpy(bytes.data() + library::recordOffset(header, 0), &record, sizeof(record));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  std::string path;
  EXPECT_FALSE(index.readPath(record, path));
}

TEST(LibraryIndexFile, SeriesPermutationRemainsReadableWhenRanksAreDegraded) {
  auto data = fixture(library::CLIX_FORMAT_VERSION, {seriesBlob("B"), seriesBlob("A"), seriesBlob("")}, {0, 1, 2});
  data.header.flags = library::CLIX_FLAG_RANKS_DEGRADED;
  std::memcpy(data.bytes.data(), &data.header, sizeof(data.header));
  const uint16_t order[] = {1, 0, 2};
  std::memcpy(data.bytes.data() + library::seriesOrderOffset(data.header, 0), order, sizeof(order));
  Storage.setFile("/library.clx", std::move(data.bytes));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_TRUE(index.ranksDegraded());
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::SeriesAsc, 0), 1);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::SeriesAsc, 1), 0);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::SeriesAsc, 2), 2);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::SeriesAsc, 3), 0xFFFF);
}

TEST(LibraryIndexFile, ReadsSeriesDirectoryAndItsBookCountSentinel) {
  auto data = fixture(library::CLIX_FORMAT_VERSION, {seriesBlob("A"), seriesBlob("A"), seriesBlob("B"), seriesBlob("")},
                      {0, 2, 3});
  Storage.setFile("/library.clx", std::move(data.bytes));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_EQ(index.seriesGroupCount(), 3);
  EXPECT_EQ(index.seriesGroupStart(0), 0);
  EXPECT_EQ(index.seriesGroupStart(1), 2);
  EXPECT_EQ(index.seriesGroupStart(2), 3);
  EXPECT_EQ(index.seriesGroupStart(3), 4);
  EXPECT_EQ(index.seriesGroupStart(4), 0xFFFF);
  index.close();
  EXPECT_EQ(index.seriesGroupCount(), 0);
  EXPECT_EQ(index.seriesGroupStart(0), 0xFFFF);
}

TEST(LibraryIndexFile, RejectsCorruptSeriesDirectoryNeighbors) {
  for (const std::vector<uint16_t> groups :
       {std::vector<uint16_t>{1, 2, 3}, {0, 0, 3}, {0, 2, 2}, {0, 2, 4}, {0, 3, 2}}) {
    auto data = fixture(library::CLIX_FORMAT_VERSION,
                        {seriesBlob("A"), seriesBlob("A"), seriesBlob("B"), seriesBlob("")}, groups);
    Storage.setFile("/library.clx", std::move(data.bytes));
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    if (groups.front() != 0)
      EXPECT_EQ(index.seriesGroupStart(0), 0xFFFF);
    else
      EXPECT_EQ(index.seriesGroupStart(1), 0xFFFF);
  }
}

TEST(LibraryIndexFile, RejectsInvalidSeriesGroupCountBeforeReadingDirectory) {
  for (const uint16_t groups : {0, 2}) {
    auto data = fixture(library::CLIX_FORMAT_VERSION, {seriesBlob("A")});
    data.header.seriesGroupCount = groups;
    std::memcpy(data.bytes.data(), &data.header, sizeof(data.header));
    Storage.setFile("/library.clx", std::move(data.bytes));
    library::LibraryIndexFile index;
    EXPECT_FALSE(index.open("/library.clx"));
    EXPECT_EQ(index.validity(), library::ClixValidity::CountOutOfRange);
  }
}

TEST(LibraryIndexFile, ReadsMissingZeroAndFractionalSeriesNumbers) {
  auto data = fixture(library::CLIX_FORMAT_VERSION,
                      {seriesBlob(""), seriesBlob("Saga", 0.0f), seriesBlob("Saga", 3.5f), seriesBlob("Saga")});
  Storage.setFile("/library.clx", std::move(data.bytes));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  for (uint16_t row = 0; row < 4; ++row) {
    library::ClixRecord record{};
    ASSERT_TRUE(index.readRecord(row, record));
    std::string series = "stale";
    std::optional<float> number = 99.0f;
    ASSERT_TRUE(index.readSeries(record, series));
    ASSERT_TRUE(index.readSeriesIndex(record, number));
    EXPECT_EQ(series, row == 0 ? "" : "Saga");
    if (row == 1) {
      ASSERT_TRUE(number);
      EXPECT_FLOAT_EQ(*number, 0.0f);
    } else if (row == 2) {
      ASSERT_TRUE(number);
      EXPECT_FLOAT_EQ(*number, 3.5f);
    } else
      EXPECT_FALSE(number);
  }
}

TEST(LibraryIndexFile, IgnoresSeriesNumberWithoutASeriesName) {
  auto data = fixture(library::CLIX_FORMAT_VERSION, {seriesBlob("", 2.0f)});
  Storage.setFile("/library.clx", std::move(data.bytes));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  ASSERT_TRUE(index.readRecord(0, record));
  std::string series;
  std::optional<float> number;
  ASSERT_TRUE(index.readSeries(record, series));
  EXPECT_TRUE(series.empty());
  ASSERT_TRUE(index.readSeriesIndex(record, number));
  EXPECT_FALSE(number);
}

TEST(LibraryIndexFile, RejectsNonFiniteSeriesNumbers) {
  for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()}) {
    auto data = fixture(library::CLIX_FORMAT_VERSION, {seriesBlob("Saga", invalid)});
    Storage.setFile("/library.clx", std::move(data.bytes));
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    library::ClixRecord record{};
    ASSERT_TRUE(index.readRecord(0, record));
    std::string series = "stale";
    std::optional<float> number = 99.0f;
    EXPECT_FALSE(index.readSeries(record, series));
    EXPECT_TRUE(series.empty());
    EXPECT_FALSE(index.readSeriesIndex(record, number));
    EXPECT_FALSE(number);
  }
}

TEST(LibraryIndexFile, RejectsUnknownSeriesIndexFlag) {
  auto blob = seriesBlob("Saga");
  blob.back() = 2;
  auto data = fixture(library::CLIX_FORMAT_VERSION, {blob});
  Storage.setFile("/library.clx", std::move(data.bytes));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  ASSERT_TRUE(index.readRecord(0, record));
  std::string series;
  std::optional<float> number;
  EXPECT_FALSE(index.readSeries(record, series));
  EXPECT_FALSE(index.readSeriesIndex(record, number));
}

TEST(LibraryIndexFile, TruncatedExtensionCannotReadIntoTheNextBooksBlob) {
  for (const int removed : {1, 2, 3, 4, 5, 6, 7, 8, 9}) {
    auto first = seriesBlob("Saga", 3.5f);
    first.resize(first.size() - removed);
    auto data = fixture(library::CLIX_FORMAT_VERSION, {first, seriesBlob("Other", 7.0f)});
    Storage.setFile("/library.clx", std::move(data.bytes));
    library::LibraryIndexFile index;
    ASSERT_TRUE(index.open("/library.clx"));
    library::ClixRecord record{};
    ASSERT_TRUE(index.readRecord(0, record));
    std::string series;
    std::optional<float> number;
    EXPECT_FALSE(index.readSeries(record, series)) << "removed=" << removed;
    EXPECT_FALSE(index.readSeriesIndex(record, number)) << "removed=" << removed;
    ASSERT_TRUE(index.readRecord(1, record));
    ASSERT_TRUE(index.readSeries(record, series));
    EXPECT_EQ(series, "Other");
    ASSERT_TRUE(index.readSeriesIndex(record, number));
    ASSERT_TRUE(number);
    EXPECT_FLOAT_EQ(*number, 7.0f);
  }
}

TEST(LibraryIndexFile, LegacyV2IsOnlyAcceptedForReconciliation) {
  constexpr uint64_t HASH = 12345;
  auto legacy = fixture(library::CLIX_LEGACY_FORMAT_VERSION,
                        {makeBlob(HASH, {'x', 1, 'a', 1, 't', 1, 'a'}, library::CLIX_LEGACY_FORMAT_VERSION)});
  // v2 reserved bytes are not a v3 directory and must not affect migration.
  legacy.header.seriesGroupsStart = UINT32_MAX;
  legacy.header.seriesGroupCount = UINT16_MAX;
  legacy.header.foldVersion = 0;
  std::memcpy(legacy.bytes.data(), &legacy.header, sizeof(legacy.header));
  Storage.setFile("/library.clx", std::move(legacy.bytes));
  library::LibraryIndexFile index;
  EXPECT_FALSE(index.open("/library.clx"));
  EXPECT_EQ(index.validity(), library::ClixValidity::UnknownFormatVersion);
  ASSERT_TRUE(index.openForReconciliation("/library.clx"));
  library::ClixRecord record{};
  ASSERT_TRUE(index.readRecord(0, record));
  std::string value;
  ASSERT_TRUE(index.readTitle(record, value));
  EXPECT_EQ(value, "t");
  ASSERT_TRUE(index.readSourceAuthor(record, value));
  EXPECT_EQ(value, "a");
  uint64_t hash = 0;
  ASSERT_TRUE(index.readPathHash(record, hash));
  EXPECT_EQ(hash, HASH);
  ASSERT_TRUE(index.readPath(record, value));
  EXPECT_EQ(value, "/books/x");
  ASSERT_TRUE(index.readSeries(record, value));
  EXPECT_TRUE(value.empty());
  std::optional<float> number = 99.0f;
  ASSERT_TRUE(index.readSeriesIndex(record, number));
  EXPECT_FALSE(number);
  EXPECT_EQ(index.seriesGroupCount(), 0);
  EXPECT_EQ(index.seriesGroupStart(0), 0xFFFF);
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::SeriesAsc, 0), 0xFFFF);
}
