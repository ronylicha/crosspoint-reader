#include <gtest/gtest.h>

#include <vector>

#include "CoverPack.h"

using coverpack::CoverPack;
using coverpack::CoverSize;
using coverpack::Key;

namespace {
constexpr CoverSize SIZE{10, 4};  // 2 bytes per row

std::vector<uint8_t> rowsFilledWith(const uint8_t value) {
  return std::vector<uint8_t>(coverpack::coverBytes(SIZE), value);
}

class CoverPackTest : public ::testing::Test {
 protected:
  void SetUp() override { fake::reset(); }

  // Looks a cover up and reads it back; empty when absent or unreadable.
  std::vector<uint8_t> read(const CoverPack& pack, const Key& key) {
    uint32_t offset = 0;
    pack.lookup(&key, 1, &offset);
    if (offset == coverpack::NOT_FOUND) return {};
    HalFile file;
    if (!pack.openForRead(file)) return {};
    std::vector<uint8_t> rows(coverpack::MAX_COVER_BYTES);
    CoverSize size;
    if (!CoverPack::readCover(file, offset, key, size, rows.data(), rows.size())) return {};
    EXPECT_EQ(size.width, SIZE.width);
    EXPECT_EQ(size.height, SIZE.height);
    rows.resize(coverpack::coverBytes(size));
    return rows;
  }
};
}  // namespace

TEST_F(CoverPackTest, AddedCoversReadBackByKey) {
  CoverPack pack("/covers", 153);
  ASSERT_TRUE(pack.add(Key{30, 100}, SIZE, rowsFilledWith(0xAA).data()));
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x55).data()));
  ASSERT_TRUE(pack.add(Key{20, 100}, SIZE, rowsFilledWith(0x0F).data()));

  EXPECT_EQ(read(pack, Key{10, 100}), rowsFilledWith(0x55));
  EXPECT_EQ(read(pack, Key{20, 100}), rowsFilledWith(0x0F));
  EXPECT_EQ(read(pack, Key{30, 100}), rowsFilledWith(0xAA));
  EXPECT_TRUE(read(pack, Key{40, 100}).empty());
}

TEST_F(CoverPackTest, ABookReplacedUnderTheSameNameMisses) {
  CoverPack pack("/covers", 153);
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x55).data()));
  // Same path hash, different size: the new file must not get the old cover.
  EXPECT_TRUE(read(pack, Key{10, 101}).empty());
}

TEST_F(CoverPackTest, ReAddingAKeyServesTheNewCover) {
  CoverPack pack("/covers", 153);
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x55).data()));
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x33).data()));
  EXPECT_EQ(read(pack, Key{10, 100}), rowsFilledWith(0x33));
}

TEST_F(CoverPackTest, BatchLookupOpensTheIndexOnce) {
  CoverPack pack("/covers", 153);
  for (uint64_t hash = 1; hash <= 12; ++hash) ASSERT_TRUE(pack.add(Key{hash, 7}, SIZE, rowsFilledWith(0x11).data()));
  Key keys[13];
  for (uint64_t i = 0; i < 13; ++i) keys[i] = Key{i + 1, 7};
  keys[12] = Key{99, 7};
  uint32_t offsets[13];
  fake::opens = 0;
  pack.lookup(keys, 13, offsets);
  EXPECT_EQ(fake::opens, 1u);
  for (int i = 0; i < 12; ++i) EXPECT_NE(offsets[i], coverpack::NOT_FOUND) << i;
  EXPECT_EQ(offsets[12], coverpack::NOT_FOUND);
}

TEST_F(CoverPackTest, AStaleIndexEntryReadsAsAMissNotAWrongCover) {
  CoverPack pack("/covers", 153);
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x55).data()));
  ASSERT_TRUE(pack.add(Key{20, 100}, SIZE, rowsFilledWith(0x0F).data()));
  uint32_t offset = 0;
  const Key first{10, 100};
  pack.lookup(&first, 1, &offset);
  HalFile file;
  ASSERT_TRUE(pack.openForRead(file));
  std::vector<uint8_t> rows(coverpack::MAX_COVER_BYTES);
  CoverSize size;
  // The record at book 10's offset belongs to book 10, not book 20.
  EXPECT_FALSE(CoverPack::readCover(file, offset, Key{20, 100}, size, rows.data(), rows.size()));
}

TEST_F(CoverPackTest, ATruncatedPackMissesInsteadOfReadingGarbage) {
  CoverPack pack("/covers", 153);
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x55).data()));
  fake::files[pack.packPath()]->resize(fake::files[pack.packPath()]->size() - 3);
  EXPECT_TRUE(read(pack, Key{10, 100}).empty());
}

TEST_F(CoverPackTest, CompactionKeepsOnlyAcceptedCovers) {
  CoverPack pack("/covers", 153);
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x55).data()));
  ASSERT_TRUE(pack.add(Key{20, 100}, SIZE, rowsFilledWith(0x0F).data()));
  ASSERT_TRUE(pack.add(Key{30, 100}, SIZE, rowsFilledWith(0xAA).data()));
  const size_t before = fake::files[pack.packPath()]->size();

  ASSERT_TRUE(pack.compact([](const Key& key, void*) { return key.pathHash != 20; }, nullptr));

  EXPECT_LT(fake::files[pack.packPath()]->size(), before);
  EXPECT_EQ(read(pack, Key{10, 100}), rowsFilledWith(0x55));
  EXPECT_TRUE(read(pack, Key{20, 100}).empty());
  EXPECT_EQ(read(pack, Key{30, 100}), rowsFilledWith(0xAA));
  // Covers added afterwards still land after the compacted records.
  ASSERT_TRUE(pack.add(Key{40, 100}, SIZE, rowsFilledWith(0x01).data()));
  EXPECT_EQ(read(pack, Key{40, 100}), rowsFilledWith(0x01));
  EXPECT_EQ(read(pack, Key{10, 100}), rowsFilledWith(0x55));
}

TEST_F(CoverPackTest, CompactionOfEverythingRemovesBothFiles) {
  CoverPack pack("/covers", 153);
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x55).data()));
  ASSERT_TRUE(pack.compact([](const Key&, void*) { return false; }, nullptr));
  EXPECT_EQ(fake::files.count(pack.packPath()), 0u);
  EXPECT_EQ(fake::files.count(pack.indexPath()), 0u);
}

TEST_F(CoverPackTest, AFailedIndexReplacementKeepsThePreviousIndex) {
  CoverPack pack("/covers", 153);
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x55).data()));
  fake::failReplace = true;
  EXPECT_FALSE(pack.add(Key{20, 100}, SIZE, rowsFilledWith(0x0F).data()));
  fake::failReplace = false;
  EXPECT_EQ(read(pack, Key{10, 100}), rowsFilledWith(0x55));
  EXPECT_TRUE(read(pack, Key{20, 100}).empty());
}

TEST_F(CoverPackTest, RejectsOversizedCovers) {
  CoverPack pack("/covers", 153);
  const std::vector<uint8_t> rows(coverpack::MAX_COVER_BYTES + 1024);
  EXPECT_FALSE(pack.add(Key{10, 100}, CoverSize{1024, 1024}, rows.data()));
  EXPECT_FALSE(pack.add(Key{10, 100}, CoverSize{0, 10}, rows.data()));
}

namespace {
// Rewrites the index as `count` sorted entries, all pointing at `offset`.
void forgeIndex(const CoverPack& pack, const uint32_t count, const uint32_t offset) {
  std::vector<uint8_t> bytes = {'C', 'P', 'I', '1', 1, 0, 0, 0};
  for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(count >> (8 * i)));
  for (uint32_t e = 0; e < count; ++e) {
    const uint64_t hash = 1000 + e;
    for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<uint8_t>(hash >> (8 * i)));
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(100u >> (8 * i)));
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(offset >> (8 * i)));
  }
  fake::files[pack.indexPath()] = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
}
}  // namespace

TEST_F(CoverPackTest, AFullIndexRefusesNewCoversWithoutGrowingThePack) {
  CoverPack pack("/covers", 153);
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x55).data()));
  forgeIndex(pack, 8192, 8);
  const size_t before = fake::files[pack.packPath()]->size();
  EXPECT_FALSE(pack.add(Key{1, 100}, SIZE, rowsFilledWith(0x0F).data()));
  EXPECT_EQ(fake::files[pack.packPath()]->size(), before);
}

TEST_F(CoverPackTest, AnUnallocatableIndexFailsWithoutWriting) {
  CoverPack pack("/covers", 153);
  ASSERT_TRUE(pack.add(Key{10, 100}, SIZE, rowsFilledWith(0x55).data()));
  const size_t before = fake::files[pack.packPath()]->size();
  fake::failAlloc = true;
  EXPECT_FALSE(pack.add(Key{20, 100}, SIZE, rowsFilledWith(0x0F).data()));
  EXPECT_FALSE(pack.compact([](const Key&, void*) { return false; }, nullptr));
  fake::failAlloc = false;
  EXPECT_EQ(fake::files[pack.packPath()]->size(), before);
  EXPECT_EQ(read(pack, Key{10, 100}), rowsFilledWith(0x55));
}
