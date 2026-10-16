#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// One file of decoded cover thumbnails per thumbnail height, so a shelf page
// reads its covers from a single open file instead of walking one cache folder
// per book and re-parsing a BMP row by row.
//
//   <dir>/h<height>.pack  append-only records: 8-byte file header, then per
//                         cover a 16-byte header (pathHash u64, fileSize u32,
//                         width u16, height u16) and its rows, 1 bpp,
//                         MSB first, a set bit is ink, (width + 7) / 8 bytes
//                         per row, top row first.
//   <dir>/h<height>.idx   12-byte header (magic, version, count u32), then
//                         count entries {pathHash u64, fileSize u32, offset
//                         u32} sorted by (pathHash, fileSize), searched by
//                         bisection on the card and replaced atomically.
//
// A cover is keyed by its book's full-path hash and file size: a book replaced
// under the same name gets a new key, never a stale cover. Every read checks
// the record header against the key, so a damaged or out-of-date index only
// costs a miss, and the cover is generated again.
//
// The card allows one open file at a time: no call keeps both files open.
namespace coverpack {

struct Key {
  uint64_t pathHash = 0;
  uint32_t fileSize = 0;
};

struct CoverSize {
  uint16_t width = 0;
  uint16_t height = 0;
};

constexpr uint32_t NOT_FOUND = UINT32_MAX;
// Largest cover a record may hold: well above the Home hero (~180 x 270).
constexpr uint16_t MAX_SIDE = 1024;
constexpr size_t MAX_COVER_BYTES = 32 * 1024;

inline size_t rowBytes(const uint16_t width) { return (static_cast<size_t>(width) + 7) / 8; }
inline size_t coverBytes(const CoverSize size) { return rowBytes(size.width) * size.height; }

class CoverPack {
 public:
  CoverPack(const std::string& dir, int thumbHeight);

  // Record offsets of `count` covers, NOT_FOUND for absent ones. One open of
  // the index for the whole batch.
  void lookup(const Key* keys, size_t count, uint32_t* offsets) const;

  bool openForRead(HalFile& pack) const;
  // Reads the cover at `offset` from a pack opened with openForRead(). `rows`
  // must hold `capacity` bytes. False on a key mismatch, a damaged record or a
  // cover larger than the buffer.
  static bool readCover(HalFile& pack, uint32_t offset, const Key& key, CoverSize& size, uint8_t* rows,
                        size_t capacity);

  // Appends a cover; an existing entry for the key now points at the new one.
  bool add(const Key& key, CoverSize size, const uint8_t* rows);

  // Keeps only the covers `keep` accepts, rewriting both files.
  bool compact(bool (*keep)(const Key& key, void* context), void* context);

  const std::string& packPath() const { return packFile; }
  const std::string& indexPath() const { return indexFile; }

 private:
  struct Entry {
    uint64_t pathHash;
    uint32_t fileSize;
    uint32_t offset;
  };
  bool loadIndex(std::vector<Entry>& entries) const;
  bool writeIndex(const std::vector<Entry>& entries) const;
  bool appendRecord(const Key& key, CoverSize size, const uint8_t* rows, uint32_t& offset) const;

  std::string directory;
  std::string packFile;
  std::string indexFile;
};

}  // namespace coverpack
