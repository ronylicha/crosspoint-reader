#include "CoverStore.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <LibraryFormat.h>
#include <LibraryIndexFile.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "RecentBooksStore.h"

namespace coverstore {
namespace {

constexpr char PACK_DIR[] = "/.crosspoint/covers";
constexpr char BOOK_CACHE_DIR[] = "/.crosspoint";
// Thumbnail heights in use at once (shelf, Home hero, Home rows, orientations).
constexpr size_t MAX_PACK_HEIGHTS = 16;

bool keyLess(const coverpack::Key& a, const coverpack::Key& b) {
  return a.pathHash != b.pathHash ? a.pathHash < b.pathHash : a.fileSize < b.fileSize;
}

// Internal RAM is shared with the whole firmware: on boards without PSRAM a
// cover keeps its placeholder rather than pushing the heap below this floor.
constexpr size_t INTERNAL_HEAP_FLOOR = 48 * 1024;

Rows allocateRows(const size_t bytes) {
#ifdef BOARD_HAS_PSRAM
  if (auto* rows = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM))) return Rows(rows);
#endif
  const auto heap = HalMemory::getInternalHeap();
  if (heap.freeBytes < bytes + INTERNAL_HEAP_FLOOR || heap.largestBlockBytes < bytes) {
    LOG_DBG("COVER", "Skipping %u-byte cover: %u bytes free", static_cast<unsigned>(bytes),
            static_cast<unsigned>(heap.freeBytes));
    return Rows();
  }
  return Rows(static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

std::string thumbPathFor(const std::string& bookPath, const int height) {
  if (FsHelpers::hasReflowableBookExtension(bookPath)) return Epub::thumbBmpPathFor(bookPath, BOOK_CACHE_DIR, height);
  if (FsHelpers::hasXtcExtension(bookPath)) return Xtc::thumbBmpPathFor(bookPath, BOOK_CACHE_DIR, height);
  return {};
}

// Thumbnails fill a portrait slot, never wider than tall.
size_t scratchCapacity(const int height) {
  return std::min(coverpack::rowBytes(static_cast<uint16_t>(height)) * static_cast<size_t>(height),
                  coverpack::MAX_COVER_BYTES);
}

uint32_t statSize(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("COVER", path, file)) return 0;
  return static_cast<uint32_t>(file.fileSize());
}

#ifdef BOARD_HAS_PSRAM
// Recently shown covers, any height, least recently used evicted first.
class SessionCache {
 public:
  static constexpr size_t BUDGET_BYTES = 512 * 1024;
  static constexpr size_t MAX_ENTRIES = 256;

  SessionCache() { entries.reserve(MAX_ENTRIES); }

  bool copyOut(const coverpack::Key& key, const int height, coverpack::CoverSize& size, Rows& out) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& entry : entries) {
      if (entry.height != height || entry.key.pathHash != key.pathHash || entry.key.fileSize != key.fileSize) continue;
      out = allocateRows(coverpack::coverBytes(entry.size));
      if (!out) return false;
      memcpy(out.get(), entry.rows.get(), coverpack::coverBytes(entry.size));
      size = entry.size;
      entry.lastUse = ++tick;
      return true;
    }
    return false;
  }

  void put(const coverpack::Key& key, const int height, const coverpack::CoverSize size, const uint8_t* rows) {
    const size_t bytes = coverpack::coverBytes(size);
    if (bytes > BUDGET_BYTES) return;
    std::lock_guard<std::mutex> lock(mutex);
    while (!entries.empty() && (usedBytes + bytes > BUDGET_BYTES || entries.size() >= MAX_ENTRIES)) evictOldest();
    auto* copy = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    if (!copy) return;
    memcpy(copy, rows, bytes);
    entries.push_back(Entry{key, height, size, Rows(copy), ++tick});
    usedBytes += bytes;
  }

 private:
  struct Entry {
    coverpack::Key key;
    int height;
    coverpack::CoverSize size;
    Rows rows;
    uint32_t lastUse;
  };

  void evictOldest() {
    auto oldest = std::min_element(entries.begin(), entries.end(),
                                   [](const Entry& a, const Entry& b) { return a.lastUse < b.lastUse; });
    usedBytes -= coverpack::coverBytes(oldest->size);
    entries.erase(oldest);
  }

  std::mutex mutex;
  std::vector<Entry> entries;
  size_t usedBytes = 0;
  uint32_t tick = 0;
};

SessionCache& session() {
  static SessionCache cache;
  return cache;
}
#endif

bool sessionCopy(const coverpack::Key& key, const int height, coverpack::CoverSize& size, Rows& out) {
#ifdef BOARD_HAS_PSRAM
  return session().copyOut(key, height, size, out);
#else
  (void)key;
  (void)height;
  (void)size;
  (void)out;
  return false;
#endif
}

void sessionPut(const coverpack::Key& key, const int height, const coverpack::CoverSize size, const uint8_t* rows) {
#ifdef BOARD_HAS_PSRAM
  session().put(key, height, size, rows);
#else
  (void)key;
  (void)height;
  (void)size;
  (void)rows;
#endif
}

}  // namespace

void RowsDeleter::operator()(uint8_t* rows) const { heap_caps_free(rows); }

void CoverSet::clear() {
  for (auto& slot : slots) {
    slot.key = {};
    slot.thumbPath.clear();
    slot.size = {};
    slot.rows.reset();
  }
  slotCount = 0;
}

size_t CoverSet::loadFromMemoryAndPack(const Book* books, const size_t count, const int thumbHeight, size_t* unfilled) {
  clear();
  height = thumbHeight;
  slotCount = std::min(count, MAX_COVERS);

  coverpack::Key keys[MAX_COVERS];
  size_t pendingCount = 0;
  for (size_t i = 0; i < slotCount; ++i) {
    Slot& slot = slots[i];
    const std::string* path = books[i].path;
    if (!path || path->empty()) continue;
    slot.thumbPath = thumbPathFor(*path, height);
    if (slot.thumbPath.empty()) continue;  // no cover for this format
    const uint32_t fileSize = books[i].fileSize != 0 ? books[i].fileSize : statSize(*path);
    slot.key = coverpack::Key{library::clixPathHash(path->data(), path->size()), fileSize};
    if (sessionCopy(slot.key, height, slot.size, slot.rows)) continue;
    keys[pendingCount] = slot.key;
    unfilled[pendingCount++] = i;
  }
  if (pendingCount == 0) return 0;

  // One pass: a batch lookup in the index, then one open pack read in order.
  const coverpack::CoverPack pack(PACK_DIR, height);
  uint32_t offsets[MAX_COVERS];
  pack.lookup(keys, pendingCount, offsets);
  const size_t capacity = scratchCapacity(height);
  Rows scratch = allocateRows(capacity);
  HalFile file;
  if (scratch && pack.openForRead(file)) {
    for (size_t p = 0; p < pendingCount; ++p) {
      Slot& slot = slots[unfilled[p]];
      coverpack::CoverSize size;
      if (!coverpack::CoverPack::readCover(file, offsets[p], slot.key, size, scratch.get(), capacity)) continue;
      slot.rows = allocateRows(coverpack::coverBytes(size));
      if (!slot.rows) continue;
      memcpy(slot.rows.get(), scratch.get(), coverpack::coverBytes(size));
      slot.size = size;
      sessionPut(slot.key, height, size, slot.rows.get());
    }
  }
  size_t left = 0;
  for (size_t p = 0; p < pendingCount; ++p) {
    if (!slots[unfilled[p]].rows) unfilled[left++] = unfilled[p];
  }
  return left;
}

bool CoverSet::load(const Book* books, const size_t count, const int thumbHeight, bool* missing) {
  for (size_t i = 0; i < std::min(count, MAX_COVERS); ++i) missing[i] = false;
  size_t unfilled[MAX_COVERS];
  const size_t left = loadFromMemoryAndPack(books, count, thumbHeight, unfilled);
  bool complete = true;
  for (size_t u = 0; u < left; ++u) {
    // Not in the pack yet: a thumbnail from before the pack moves in once.
    if (loadFromBmp(slots[unfilled[u]]) == BmpResult::Absent) {
      missing[unfilled[u]] = true;
      complete = false;
    }
  }
  return complete;
}

void CoverSet::warm(const Book* books, const size_t count, const int thumbHeight) {
#ifdef BOARD_HAS_PSRAM
  // The set itself is dropped: what matters is what reached the session cache.
  auto set = makeUniqueNoThrow<CoverSet>();
  if (!set) return;
  size_t unfilled[MAX_COVERS];
  set->loadFromMemoryAndPack(books, count, thumbHeight, unfilled);
#else
  (void)books;
  (void)count;
  (void)thumbHeight;
#endif
}

bool CoverSet::refresh(const size_t index) {
  if (index >= slotCount || slots[index].thumbPath.empty()) return false;
  return loadFromBmp(slots[index]) == BmpResult::Loaded;
}

CoverSet::BmpResult CoverSet::loadFromBmp(Slot& slot) {
  if (!Storage.exists(slot.thumbPath.c_str())) return BmpResult::Absent;
  coverpack::CoverSize size;
  {
    HalFile file;
    if (!Storage.openFileForRead("COVER", slot.thumbPath, file)) return BmpResult::Unusable;
    Bitmap bitmap(file);
    // A format the generator could not decode leaves an empty BMP so it is not
    // retried: the slot keeps its placeholder.
    if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0 ||
        bitmap.getWidth() > coverpack::MAX_SIDE || bitmap.getHeight() > coverpack::MAX_SIDE) {
      return BmpResult::Unusable;
    }
    size = coverpack::CoverSize{static_cast<uint16_t>(bitmap.getWidth()), static_cast<uint16_t>(bitmap.getHeight())};
    if (coverpack::coverBytes(size) > coverpack::MAX_COVER_BYTES) return BmpResult::Unusable;

    Rows rows = allocateRows(coverpack::coverBytes(size));
    // readNextRow emits 2 bpp; the BMP's own row stride is its scratch.
    auto twoBit = makeUniqueNoThrow<uint8_t[]>((size.width + 3) / 4 + static_cast<size_t>(bitmap.getRowBytes()));
    if (!rows || !twoBit) {
      LOG_ERR("COVER", "OOM: decoding %s", slot.thumbPath.c_str());
      return BmpResult::Unusable;
    }
    const size_t stride = coverpack::rowBytes(size.width);
    uint8_t* const twoBitRow = twoBit.get();
    uint8_t* const rawRow = twoBit.get() + (size.width + 3) / 4;
    memset(rows.get(), 0, coverpack::coverBytes(size));
    for (int r = 0; r < size.height; ++r) {
      if (bitmap.readNextRow(twoBitRow, rawRow) != BmpReaderError::Ok) return BmpResult::Unusable;
      uint8_t* out = rows.get() + static_cast<size_t>(bitmap.isTopDown() ? r : size.height - 1 - r) * stride;
      for (int x = 0; x < size.width; ++x) {
        // Levels 0-2 are ink, 3 is paper (same rule as drawBitmap1Bit).
        if (((twoBitRow[x / 4] >> (6 - (x % 4) * 2)) & 0x3) < 3) out[x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
      }
    }
    slot.rows = std::move(rows);
  }
  slot.size = size;
  coverpack::CoverPack(PACK_DIR, height).add(slot.key, size, slot.rows.get());
  sessionPut(slot.key, height, size, slot.rows.get());
  return BmpResult::Loaded;
}

bool CoverSet::draw(const GfxRenderer& renderer, const size_t index, const Rect& slot, const int xOffset) const {
  if (index >= slotCount || !slots[index].rows) return false;
  const coverpack::CoverSize size = slots[index].size;
  const uint8_t* rows = slots[index].rows.get();
  const size_t stride = coverpack::rowBytes(size.width);
  const int left = slot.x + (slot.width - size.width) / 2 + xOffset;
  const int top = slot.y + (slot.height - size.height) / 2;
  const int clipRight = slot.x + slot.width;
  const int clipBottom = slot.y + slot.height;
  for (int r = 0; r < size.height; ++r) {
    const int y = top + r;
    if (y < slot.y || y >= clipBottom) continue;
    const uint8_t* row = rows + static_cast<size_t>(r) * stride;
    for (size_t b = 0; b < stride; ++b) {
      const uint8_t bits = row[b];
      if (bits == 0) continue;  // eight paper pixels
      for (int bit = 0; bit < 8; ++bit) {
        if (!(bits & (0x80 >> bit))) continue;
        const int x = left + static_cast<int>(b) * 8 + bit;
        if (x < slot.x || x >= clipRight) continue;
        renderer.drawPixel(x, y, true);
      }
    }
  }
  return true;
}

void compactPacks() {
  // Keys of every indexed book plus the recent books (Home shows them even when
  // the index skipped them), sorted for lookup. Fallible: a huge library on a
  // board short of memory just keeps its packs until the next rebuild.
  size_t liveCount = 0;
  size_t liveCapacity = 0;
  std::unique_ptr<coverpack::Key[]> live;
  {
    library::LibraryIndexFile index;
    if (!index.open(library::libraryIndexPath())) return;
    liveCapacity = index.bookCount() + static_cast<size_t>(RecentBooksStore::MAX_RECENT_BOOKS);
    live = makeUniqueNoThrow<coverpack::Key[]>(liveCapacity);
    if (!live) {
      LOG_ERR("COVER", "OOM: pack compaction skipped");
      return;
    }
    for (uint16_t ordinal = 0; ordinal < index.bookCount(); ++ordinal) {
      library::ClixRecord record{};
      uint64_t hash = 0;
      if (!index.readRecord(ordinal, record) || !index.readPathHash(record, hash)) return;  // keep packs on doubt
      live[liveCount++] = coverpack::Key{hash, record.fileSize};
    }
  }
  for (const auto& recent : RECENT_BOOKS.getBooks()) {
    if (liveCount >= liveCapacity) break;
    const uint32_t fileSize = statSize(recent.path);
    if (fileSize != 0)
      live[liveCount++] = coverpack::Key{library::clixPathHash(recent.path.data(), recent.path.size()), fileSize};
  }
  std::sort(live.get(), live.get() + liveCount, keyLess);

  // Pack heights present on the card, from their index file names.
  int heights[MAX_PACK_HEIGHTS];
  size_t heightCount = 0;
  {
    HalFile dir = Storage.open(PACK_DIR);
    if (!dir || !dir.isDirectory()) return;
    char name[32];
    for (HalFile entry = dir.openNextFile(); entry && heightCount < MAX_PACK_HEIGHTS; entry = dir.openNextFile()) {
      name[0] = '\0';
      entry.getName(name, sizeof(name));
      int height = 0;
      char tail[8] = {};
      if (sscanf(name, "h%d.%7s", &height, tail) == 2 && strcmp(tail, "idx") == 0 && height > 0) {
        heights[heightCount++] = height;
      }
    }
  }
  struct LiveKeys {
    const coverpack::Key* begin;
    const coverpack::Key* end;
  } keys{live.get(), live.get() + liveCount};
  for (size_t i = 0; i < heightCount; ++i) {
    coverpack::CoverPack(PACK_DIR, heights[i])
        .compact(
            [](const coverpack::Key& key, void* raw) {
              const auto* set = static_cast<const LiveKeys*>(raw);
              return std::binary_search(set->begin, set->end, key, keyLess);
            },
            &keys);
  }
}

}  // namespace coverstore
