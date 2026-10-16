#pragma once

#include <CoverPack.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Decoded cover thumbnails for the screens that show several at once (the
// library shelves, Home). A screen loads its covers once per page from the
// cover pack (see lib/CoverPack) and then draws them from memory on every
// frame, so moving the selection never touches the SD card again.
//
// Boards with PSRAM also keep a session-wide cache of recently shown covers:
// returning to Home or to a library page draws them without reading the card.
namespace coverstore {

struct RowsDeleter {
  void operator()(uint8_t* rows) const;
};
using Rows = std::unique_ptr<uint8_t[], RowsDeleter>;

// Covers of one screen at one thumbnail height.
class CoverSet {
 public:
  static constexpr size_t MAX_COVERS = 12;

  struct Book {
    const std::string* path = nullptr;  // empty or null: no cover
    uint32_t fileSize = 0;              // 0 when unknown: the file is stat'ed
  };

  // Loads every book's cover: session cache, then the pack, then an existing
  // BMP thumbnail migrated into the pack. `missing[i]` is set for books whose
  // thumbnail still has to be generated. Returns true when nothing is missing.
  bool load(const Book* books, size_t count, int thumbHeight, bool* missing);

  // Boards with PSRAM: reads these covers from the pack into the session
  // cache, so a later load() of them reads nothing from the card. Nothing is
  // decoded from BMP or generated. No-op without PSRAM.
  static void warm(const Book* books, size_t count, int thumbHeight);

  // Slot `index` after its thumbnail was generated: migrate and keep it.
  bool refresh(size_t index);

  // Draws the cover centred in `slot`, `xOffset` px to the right, clipped to
  // the slot. False when the slot has no cover in memory.
  bool draw(const GfxRenderer& renderer, size_t index, const Rect& slot, int xOffset = 0) const;

  // Size of the cover in memory for `index`, zero when there is none.
  coverpack::CoverSize sizeOf(size_t index) const {
    return index < slotCount && slots[index].rows ? slots[index].size : coverpack::CoverSize{};
  }

  void clear();

 private:
  struct Slot {
    coverpack::Key key;
    std::string thumbPath;  // BMP thumbnail, the migration source
    coverpack::CoverSize size;
    Rows rows;
  };
  enum class BmpResult { Loaded, Absent, Unusable };
  BmpResult loadFromBmp(Slot& slot);
  // Session cache, then the pack; returns the slots it could not fill.
  size_t loadFromMemoryAndPack(const Book* books, size_t count, int thumbHeight, size_t* unfilled);

  std::array<Slot, MAX_COVERS> slots;
  size_t slotCount = 0;
  int height = 0;
};

// Drops pack entries for books no longer in the library index. Called after a
// successful index rebuild.
void compactPacks();

}  // namespace coverstore
