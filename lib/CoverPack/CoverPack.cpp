#include "CoverPack.h"

#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

namespace coverpack {
namespace {

constexpr uint8_t PACK_MAGIC[4] = {'C', 'P', 'K', '1'};
constexpr uint8_t INDEX_MAGIC[4] = {'C', 'P', 'I', '1'};
constexpr uint16_t FORMAT_VERSION = 1;
constexpr size_t PACK_HEADER = 8;
constexpr size_t RECORD_HEADER = 16;
constexpr size_t INDEX_HEADER = 12;
constexpr size_t INDEX_ENTRY = 16;
// A library holds at most 4,096 books (CLIX_MAX_RECORDS); twice that leaves
// room for stale entries until the next compaction.
constexpr uint32_t MAX_ENTRIES = 8192;

// Little-endian on the card whatever the host, read through byte copies
// (RISC-V faults on unaligned wide loads).
void putU16(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void putU32(uint8_t* p, const uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
void putU64(uint8_t* p, const uint64_t v) {
  for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t getU32(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}
uint64_t getU64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}

bool keyLess(const uint64_t hashA, const uint32_t sizeA, const uint64_t hashB, const uint32_t sizeB) {
  return hashA != hashB ? hashA < hashB : sizeA < sizeB;
}

bool validSize(const CoverSize size) {
  return size.width > 0 && size.height > 0 && size.width <= MAX_SIDE && size.height <= MAX_SIDE &&
         coverBytes(size) <= MAX_COVER_BYTES;
}

bool readExact(HalFile& file, void* dst, const size_t len) { return file.read(dst, len) == static_cast<int>(len); }

bool writeExact(HalFile& file, const void* src, const size_t len) { return file.write(src, len) == len; }

void packHeader(uint8_t* header) {
  memcpy(header, PACK_MAGIC, sizeof(PACK_MAGIC));
  putU16(header + 4, FORMAT_VERSION);
  putU16(header + 6, 0);
}

bool packHeaderValid(const uint8_t* header) {
  return memcmp(header, PACK_MAGIC, sizeof(PACK_MAGIC)) == 0 && getU16(header + 4) == FORMAT_VERSION;
}

}  // namespace

CoverPack::CoverPack(const std::string& dir, const int thumbHeight)
    : directory(dir),
      packFile(dir + "/h" + std::to_string(thumbHeight) + ".pack"),
      indexFile(dir + "/h" + std::to_string(thumbHeight) + ".idx") {}

void CoverPack::lookup(const Key* keys, const size_t count, uint32_t* offsets) const {
  for (size_t i = 0; i < count; ++i) offsets[i] = NOT_FOUND;
  if (count == 0 || !Storage.exists(indexFile.c_str())) return;
  HalFile index;
  if (!Storage.openFileForRead("COVER", indexFile, index)) return;
  uint8_t header[INDEX_HEADER];
  if (!readExact(index, header, sizeof(header)) || memcmp(header, INDEX_MAGIC, sizeof(INDEX_MAGIC)) != 0 ||
      getU16(header + 4) != FORMAT_VERSION) {
    return;
  }
  const uint32_t entries = getU32(header + 8);
  if (entries > MAX_ENTRIES || index.fileSize() < INDEX_HEADER + static_cast<size_t>(entries) * INDEX_ENTRY) return;

  uint8_t entry[INDEX_ENTRY];
  for (size_t k = 0; k < count; ++k) {
    uint32_t low = 0;
    uint32_t high = entries;
    while (low < high) {
      const uint32_t middle = low + (high - low) / 2;
      if (!index.seekSet(INDEX_HEADER + static_cast<size_t>(middle) * INDEX_ENTRY) ||
          !readExact(index, entry, sizeof(entry))) {
        return;
      }
      const uint64_t hash = getU64(entry);
      const uint32_t size = getU32(entry + 8);
      if (hash == keys[k].pathHash && size == keys[k].fileSize) {
        offsets[k] = getU32(entry + 12);
        break;
      }
      if (keyLess(hash, size, keys[k].pathHash, keys[k].fileSize))
        low = middle + 1;
      else
        high = middle;
    }
  }
}

bool CoverPack::openForRead(HalFile& pack) const {
  if (!Storage.exists(packFile.c_str())) return false;
  if (!Storage.openFileForRead("COVER", packFile, pack)) return false;
  uint8_t header[PACK_HEADER];
  return readExact(pack, header, sizeof(header)) && packHeaderValid(header);
}

bool CoverPack::readCover(HalFile& pack, const uint32_t offset, const Key& key, CoverSize& size, uint8_t* rows,
                          const size_t capacity) {
  if (offset == NOT_FOUND || offset < PACK_HEADER || !pack.seekSet(offset)) return false;
  uint8_t header[RECORD_HEADER];
  if (!readExact(pack, header, sizeof(header))) return false;
  if (getU64(header) != key.pathHash || getU32(header + 8) != key.fileSize) return false;
  size.width = getU16(header + 12);
  size.height = getU16(header + 14);
  if (!validSize(size) || coverBytes(size) > capacity) return false;
  return readExact(pack, rows, coverBytes(size));
}

bool CoverPack::appendRecord(const Key& key, const CoverSize size, const uint8_t* rows, uint32_t& offset,
                             bool& fresh) const {
  Storage.mkdir(directory.c_str());
  size_t packSize = 0;
  fresh = true;
  {
    HalFile pack;
    if (openForRead(pack)) {
      packSize = pack.fileSize();
      fresh = false;
    }
  }
  if (fresh) {
    // Missing or unreadable pack: start over; the old index cannot point into it.
    Storage.remove(indexFile.c_str());
    HalFile created;
    if (!Storage.openFileForWrite("COVER", packFile, created)) return false;
    uint8_t header[PACK_HEADER];
    packHeader(header);
    if (!writeExact(created, header, sizeof(header))) return false;
    packSize = PACK_HEADER;
  }
  if (packSize > UINT32_MAX - RECORD_HEADER - coverBytes(size)) return false;

  HalFile pack = Storage.open(packFile.c_str(), O_RDWR);
  if (!pack || !pack.seekSet(packSize)) return false;
  uint8_t header[RECORD_HEADER];
  putU64(header, key.pathHash);
  putU32(header + 8, key.fileSize);
  putU16(header + 12, size.width);
  putU16(header + 14, size.height);
  if (!writeExact(pack, header, sizeof(header)) || !writeExact(pack, rows, coverBytes(size))) return false;
  offset = static_cast<uint32_t>(packSize);
  return pack.close();
}

bool CoverPack::loadIndex(EntryList& list, const uint32_t spare) const {
  list = EntryList{};
  uint32_t count = 0;
  HalFile index;
  if (Storage.exists(indexFile.c_str())) {
    if (!Storage.openFileForRead("COVER", indexFile, index)) return false;
    uint8_t header[INDEX_HEADER];
    if (readExact(index, header, sizeof(header)) && memcmp(header, INDEX_MAGIC, sizeof(INDEX_MAGIC)) == 0 &&
        getU16(header + 4) == FORMAT_VERSION && getU32(header + 8) <= MAX_ENTRIES) {
      count = getU32(header + 8);
    }
  }
  if (count + spare > 0) {
    list.items = makeUniqueNoThrow<Entry[]>(count + spare);
    if (!list.items) {
      LOG_ERR("COVER", "OOM: %u index entries", static_cast<unsigned>(count + spare));
      return false;
    }
  }
  uint8_t raw[INDEX_ENTRY];
  for (uint32_t i = 0; i < count; ++i) {
    if (!readExact(index, raw, sizeof(raw))) {
      list.count = 0;  // damaged: rebuilt from what is added next
      return true;
    }
    list.items[i] = Entry{getU64(raw), getU32(raw + 8), getU32(raw + 12)};
  }
  list.count = count;
  return true;
}

bool CoverPack::writeIndex(const Entry* entries, const uint32_t count) const {
  const std::string tmp = indexFile + ".tmp";
  {
    HalFile index;
    if (!Storage.openFileForWrite("COVER", tmp, index)) return false;
    uint8_t header[INDEX_HEADER];
    memcpy(header, INDEX_MAGIC, sizeof(INDEX_MAGIC));
    putU16(header + 4, FORMAT_VERSION);
    putU16(header + 6, 0);
    putU32(header + 8, count);
    if (!writeExact(index, header, sizeof(header))) return false;
    uint8_t raw[INDEX_ENTRY];
    for (uint32_t i = 0; i < count; ++i) {
      putU64(raw, entries[i].pathHash);
      putU32(raw + 8, entries[i].fileSize);
      putU32(raw + 12, entries[i].offset);
      if (!writeExact(index, raw, sizeof(raw))) return false;
    }
    if (!index.close()) return false;
  }
  return Storage.replaceFile(tmp.c_str(), indexFile.c_str());
}

bool CoverPack::add(const Key& key, const CoverSize size, const uint8_t* rows) {
  if (!validSize(size) || rows == nullptr) return false;
  EntryList list;
  if (!loadIndex(list, 1)) return false;
  Entry* const begin = list.items.get();
  Entry* at = std::lower_bound(begin, begin + list.count, key, [](const Entry& entry, const Key& k) {
    return keyLess(entry.pathHash, entry.fileSize, k.pathHash, k.fileSize);
  });
  bool replace = at != begin + list.count && at->pathHash == key.pathHash && at->fileSize == key.fileSize;
  // Checked before writing: a full index must not grow the pack on every visit.
  if (!replace && list.count >= MAX_ENTRIES) return false;  // compaction will make room

  uint32_t offset = 0;
  bool fresh = false;
  if (!appendRecord(key, size, rows, offset, fresh)) {
    LOG_ERR("COVER", "Cannot append to %s", packFile.c_str());
    return false;
  }
  if (fresh) {
    list.count = 0;
    at = begin;
    replace = false;
  }
  if (replace) {
    at->offset = offset;
  } else {
    memmove(at + 1, at, static_cast<size_t>(begin + list.count - at) * sizeof(Entry));
    *at = Entry{key.pathHash, key.fileSize, offset};
    ++list.count;
  }
  if (!writeIndex(begin, list.count)) {
    LOG_ERR("COVER", "Cannot write %s", indexFile.c_str());
    return false;
  }
  return true;
}

bool CoverPack::compact(bool (*keep)(const Key& key, void* context), void* context) {
  EntryList list;
  if (!loadIndex(list, 0)) return false;
  Entry* const entries = list.items.get();
  uint32_t kept = 0;
  for (uint32_t i = 0; i < list.count; ++i) {
    if (keep(Key{entries[i].pathHash, entries[i].fileSize}, context)) entries[kept++] = entries[i];
  }
  if (kept == list.count) return true;
  if (kept == 0) {
    Storage.remove(indexFile.c_str());
    Storage.remove(packFile.c_str());
    return true;
  }

  auto buffer = makeUniqueNoThrow<uint8_t[]>(MAX_COVER_BYTES);
  if (!buffer) {
    LOG_ERR("COVER", "OOM: compaction buffer");
    return false;
  }
  const std::string tmp = packFile + ".tmp";
  uint32_t rewritten = 0;
  {
    HalFile source;
    if (!openForRead(source)) return false;
    HalFile target;
    if (!Storage.openFileForWrite("COVER", tmp, target)) return false;
    uint8_t header[PACK_HEADER];
    packHeader(header);
    if (!writeExact(target, header, sizeof(header))) return false;
    size_t written = PACK_HEADER;
    for (uint32_t i = 0; i < kept; ++i) {
      const Key key{entries[i].pathHash, entries[i].fileSize};
      CoverSize size;
      // A record that no longer reads back is dropped: it is generated again.
      if (!readCover(source, entries[i].offset, key, size, buffer.get(), MAX_COVER_BYTES)) continue;
      uint8_t record[RECORD_HEADER];
      putU64(record, key.pathHash);
      putU32(record + 8, key.fileSize);
      putU16(record + 12, size.width);
      putU16(record + 14, size.height);
      if (!writeExact(target, record, sizeof(record)) || !writeExact(target, buffer.get(), coverBytes(size))) {
        return false;
      }
      // In place: slot `rewritten` never runs ahead of slot `i`.
      entries[rewritten++] = Entry{key.pathHash, key.fileSize, static_cast<uint32_t>(written)};
      written += RECORD_HEADER + coverBytes(size);
    }
    if (!target.close()) return false;
  }
  // Pack first: an index left behind points at records whose headers no
  // longer match, which reads as misses rather than wrong covers.
  if (!Storage.replaceFile(tmp.c_str(), packFile.c_str())) return false;
  return writeIndex(entries, rewritten);
}

}  // namespace coverpack
