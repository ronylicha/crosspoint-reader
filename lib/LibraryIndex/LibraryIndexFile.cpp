#include "LibraryIndexFile.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "LibraryText.h"

namespace library {

LibraryIndexFile::~LibraryIndexFile() { close(); }

bool LibraryIndexFile::open(const char* path) { return openImpl(path, false, false); }

bool LibraryIndexFile::openForReconciliation(const char* path) { return openImpl(path, true, true); }

bool LibraryIndexFile::openImpl(const char* path, const bool acceptStaleFold, const bool acceptLegacy) {
  close();
  readFailed = false;
  if (!Storage.openFileForRead("LIBIDX", path, file)) {
    readFailed = true;
    return false;
  }

  // A truncated cache is rebuildable; only a read failure against an
  // announced complete header indicates unavailable storage.
  const uint64_t fileSize = file.fileSize64();
  if (fileSize < sizeof(head)) {
    lastValidity = ClixValidity::SizeMismatch;
    file.close();
    return false;
  }

  if (file.read(&head, sizeof(head)) != static_cast<int>(sizeof(head))) {
    readFailed = true;
    lastValidity = ClixValidity::SizeMismatch;
    file.close();
    return false;
  }

  if (acceptLegacy) {
    lastValidity = validateHeaderStructureForReconciliation(head, fileSize);
  } else {
    lastValidity = acceptStaleFold ? validateHeaderStructure(head, fileSize) : validateHeader(head, fileSize);
  }
  if (lastValidity != ClixValidity::Ok) {
    LOG_INF("LIBIDX", "index rejected: %s", clixValidityName(lastValidity));
    file.close();
    return false;
  }
  opened = true;
  return true;
}

void LibraryIndexFile::close() {
  if (file.isOpen()) file.close();
  opened = false;
}

bool LibraryIndexFile::readAt(const uint32_t offset, void* dst, const size_t len) {
  if (!opened) return false;
  // Every offset handed to this function comes from the header, and the header
  // was validated against the real file size, so a short read means the card
  // changed under us rather than a bad computation.
  if (!file.seekSet(offset) || file.read(dst, len) != static_cast<int>(len)) {
    readFailed = true;
    return false;
  }
  return true;
}

uint16_t LibraryIndexFile::ordinalForRow(const SortOrder order, const uint16_t row) {
  constexpr uint16_t NONE = 0xFFFF;
  if (!opened || row >= head.bookCount) return NONE;

  switch (order) {
    case SortOrder::TitleAsc:
      // The record section IS in title order, so this costs no storage and no
      // read at all.
      return row;
    case SortOrder::TitleDesc:
      return static_cast<uint16_t>(head.bookCount - 1 - row);
    case SortOrder::AuthorAsc:
    case SortOrder::AuthorDesc: {
      const uint16_t k = order == SortOrder::AuthorAsc ? row : static_cast<uint16_t>(head.bookCount - 1 - row);
      uint16_t ordinal = NONE;
      return readAt(authorOrderOffset(head, k), &ordinal, sizeof(ordinal)) && ordinal < head.bookCount ? ordinal : NONE;
    }
    case SortOrder::SeriesAsc: {
      if (head.formatVersion != CLIX_FORMAT_VERSION) return NONE;
      uint16_t ordinal = NONE;
      return readAt(seriesOrderOffset(head, row), &ordinal, sizeof(ordinal)) && ordinal < head.bookCount ? ordinal
                                                                                                         : NONE;
    }
    case SortOrder::RecentAsc:
    case SortOrder::RecentDesc: {
      // arrivalOrder runs oldest first, so both directions share one on-disk
      // permutation.
      const uint16_t k = order == SortOrder::RecentAsc ? row : static_cast<uint16_t>(head.bookCount - 1 - row);
      uint16_t ordinal = NONE;
      return readAt(arrivalOrderOffset(head, k), &ordinal, sizeof(ordinal)) && ordinal < head.bookCount ? ordinal
                                                                                                        : NONE;
    }
  }
  return NONE;
}

uint16_t LibraryIndexFile::seriesGroupStart(const uint16_t group) {
  constexpr uint16_t NONE = 0xFFFF;
  if (!opened || head.formatVersion != CLIX_FORMAT_VERSION || group > head.seriesGroupCount) return NONE;
  if (group == head.seriesGroupCount) return head.bookCount;
  uint16_t start = NONE;
  if (!readAt(head.seriesGroupsStart + static_cast<uint32_t>(group) * sizeof(start), &start, sizeof(start)) ||
      start >= head.bookCount || (group == 0 && start != 0))
    return NONE;
  if (group > 0) {
    uint16_t previous = NONE;
    if (!readAt(head.seriesGroupsStart + static_cast<uint32_t>(group - 1) * sizeof(previous), &previous,
                sizeof(previous)) ||
        previous >= start)
      return NONE;
  }
  if (group + 1 < head.seriesGroupCount) {
    uint16_t next = NONE;
    if (!readAt(head.seriesGroupsStart + static_cast<uint32_t>(group + 1) * sizeof(next), &next, sizeof(next)) ||
        next <= start || next >= head.bookCount)
      return NONE;
  }
  return start;
}

bool LibraryIndexFile::recentRowsFor(const BookIdentity* books, const size_t count, uint16_t* outRows) {
  constexpr uint16_t NONE = 0xFFFF;
  for (size_t i = 0; i < count; i++) outRows[i] = NONE;
  if (!opened || count == 0 || count > MAX_IDENTITY_LOOKUPS || head.bookCount == 0) return opened;

  constexpr size_t CHUNK_RECORDS = 32;  // 4096 bytes, the aligned-tile size
  auto chunk = makeUniqueNoThrow<uint8_t[]>(CHUNK_RECORDS * sizeof(ClixRecord));
  if (!chunk) {
    LOG_ERR("LIBIDX", "OOM: %u-byte lookup chunk", static_cast<unsigned>(CHUNK_RECORDS * sizeof(ClixRecord)));
    return false;
  }

  // Pass 1: record section, matching sizes in the chunk and confirming the few
  // size hits against the stored path hash.
  uint16_t ordinals[MAX_IDENTITY_LOOKUPS];
  for (size_t i = 0; i < count; i++) ordinals[i] = NONE;
  size_t unresolved = count;
  for (uint16_t base = 0; base < head.bookCount && unresolved > 0; base += CHUNK_RECORDS) {
    const uint16_t batch = std::min<uint16_t>(CHUNK_RECORDS, head.bookCount - base);
    if (!readAt(recordOffset(head, base), chunk.get(), batch * sizeof(ClixRecord))) return false;
    for (uint16_t r = 0; r < batch && unresolved > 0; r++) {
      // memcpy, not a cast: the chunk buffer has no alignment guarantee for the
      // record's 32-bit fields.
      ClixRecord record;
      memcpy(&record, chunk.get() + r * sizeof(ClixRecord), sizeof(ClixRecord));
      uint64_t hash = 0;
      bool hashRead = false;
      for (size_t i = 0; i < count; i++) {
        if (ordinals[i] != NONE) continue;
        // Size 0 means the caller could not stat the file (the index handle
        // may be the only reader the card allows); the hash alone decides.
        if (books[i].fileSize != 0 && books[i].fileSize != record.fileSize) continue;
        if (!hashRead) {
          if (record.nameOff > head.nameLen) break;  // unvalidated record; skip it
          if (!readPathHash(record, hash)) return false;
          hashRead = true;
        }
        if (books[i].pathHash == hash) {
          ordinals[i] = base + r;
          unresolved--;
        }
      }
    }
  }

  // Pass 2: arrival permutation, translating matched ordinals to ascending
  // rows.
  for (uint16_t base = 0; base < head.bookCount && unresolved < count; base += CHUNK_RECORDS * 2) {
    const uint16_t batch = std::min<uint16_t>(CHUNK_RECORDS * 2, head.bookCount - base);
    if (!readAt(arrivalOrderOffset(head, base), chunk.get(), batch * sizeof(uint16_t))) return false;
    for (uint16_t k = 0; k < batch; k++) {
      uint16_t ordinal;
      memcpy(&ordinal, chunk.get() + k * sizeof(uint16_t), sizeof(uint16_t));
      for (size_t i = 0; i < count; i++) {
        if (ordinals[i] != NONE && ordinals[i] == ordinal) outRows[i] = base + k;
      }
    }
  }
  return true;
}

bool LibraryIndexFile::readRecord(const uint16_t ordinal, ClixRecord& out) {
  if (!opened || ordinal >= head.bookCount) return false;
  if (!readAt(recordOffset(head, ordinal), &out, sizeof(out))) return false;

  // Clamp here, at the single point every record enters the program. These
  // lengths come off an SD card that the user can write to and that can rot: a
  // foldLen of 255 against a 96-byte field sends a string_view 159 bytes past the
  // end of the record, and callers build views from them without looking. Fixing
  // it at each call site would mean fixing it again at the next one.
  out.foldLen = static_cast<uint8_t>(std::min<size_t>(out.foldLen, CLIX_FOLD_BYTES));
  out.authorKeyLen = static_cast<uint8_t>(std::min<size_t>(out.authorKeyLen, CLIX_AUTHOR_KEY_BYTES));
  if (out.metadataStatus > CLIX_METADATA_FAILED) return false;
  // nameOff is u32 and every reader adds a length to it before comparing against
  // the section size. A forged value near the top of the range wraps that sum and
  // passes the bounds check it was supposed to fail, so it is rejected here
  // instead — the one place that can, before any arithmetic touches it.
  if (out.nameOff > head.nameLen) {
    out.nameLen = 0;
    out.nameOff = 0;
  }
  return true;
}

bool LibraryIndexFile::readName(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened || record.nameLen == 0) return false;
  if (record.nameOff > head.nameLen || sizeof(uint64_t) > head.nameLen - record.nameOff ||
      record.nameLen > head.nameLen - record.nameOff - sizeof(uint64_t))
    return false;
  out.resize(record.nameLen);
  return readAt(head.nameStart + record.nameOff + sizeof(uint64_t), out.data(), record.nameLen);
}

bool LibraryIndexFile::readPathHash(const ClixRecord& record, uint64_t& out) {
  out = 0;
  if (!opened) return false;
  if (record.nameOff > head.nameLen || sizeof(out) > head.nameLen - record.nameOff) {
    readFailed = true;
    return false;
  }
  return readAt(head.nameStart + record.nameOff, &out, sizeof(out));
}

bool LibraryIndexFile::recordBlobBounds(const ClixRecord& record, uint32_t& start, uint32_t& end) {
  start = 0;
  end = 0;
  if (!opened || head.formatVersion != CLIX_FORMAT_VERSION || record.nameOff >= head.nameLen) return false;
  // v3 writers emit names in record order; binary search needs no resident
  // offsets and reads at most log2(bookCount) fixed-width fields.
  uint16_t low = 0;
  uint16_t high = head.bookCount;
  while (low < high) {
    const uint16_t middle = static_cast<uint16_t>(low + (high - low) / 2);
    uint32_t offset = 0;
    if (!readAt(recordOffset(head, middle), &offset, sizeof(offset)) || offset >= head.nameLen) return false;
    if (offset < record.nameOff)
      low = static_cast<uint16_t>(middle + 1);
    else
      high = middle;
  }
  if (low >= head.bookCount) return false;
  uint32_t matched = 0;
  if (!readAt(recordOffset(head, low), &matched, sizeof(matched)) || matched != record.nameOff) return false;
  uint32_t limit = head.nameLen;
  if (low + 1 < head.bookCount && !readAt(recordOffset(head, static_cast<uint16_t>(low + 1)), &limit, sizeof(limit)))
    return false;
  if (limit <= matched || limit > head.nameLen) return false;
  if (low > 0) {
    uint32_t previous = 0;
    if (!readAt(recordOffset(head, static_cast<uint16_t>(low - 1)), &previous, sizeof(previous)) || previous >= matched)
      return false;
  }
  start = matched;
  end = limit;
  return true;
}

bool LibraryIndexFile::readBlobField(const ClixRecord& record, const uint8_t field, std::string& out) {
  out.clear();
  if (!opened || record.nameLen == 0) return false;
  uint32_t start = record.nameOff;
  uint32_t end = head.nameLen;
  if (head.formatVersion == CLIX_FORMAT_VERSION && !recordBlobBounds(record, start, end)) return false;
  if (start > end || sizeof(uint64_t) > end - start || record.nameLen > end - start - sizeof(uint64_t)) return false;

  uint32_t at = start + sizeof(uint64_t) + record.nameLen;
  for (uint8_t i = 0; i <= field; i++) {
    if (at >= end) return false;
    uint8_t len = 0;
    if (!readAt(head.nameStart + at, &len, sizeof(len))) return false;
    ++at;
    if (len > end - at) return false;
    if (i == field) {
      out.resize(len);
      if (len == 0 || readAt(head.nameStart + at, out.data(), len)) return true;
      out.clear();
      return false;
    }
    at += len;
  }
  return false;
}

bool LibraryIndexFile::readSeriesExtension(const ClixRecord& record, uint32_t& cursor, uint32_t& end) {
  cursor = 0;
  if (record.nameLen == 0 || !recordBlobBounds(record, cursor, end)) return false;
  if (sizeof(uint64_t) > end - cursor || record.nameLen > end - cursor - sizeof(uint64_t)) return false;
  cursor += sizeof(uint64_t) + record.nameLen;
  // Three length-prefixed legacy fields precede the series payload.
  for (int field = 0; field < 3; ++field) {
    uint8_t len = 0;
    if (cursor >= end || !readAt(head.nameStart + cursor, &len, sizeof(len))) return false;
    ++cursor;
    if (len > end - cursor) return false;
    cursor += len;
  }
  uint8_t seriesLen = 0;
  if (cursor >= end || !readAt(head.nameStart + cursor, &seriesLen, sizeof(seriesLen))) return false;
  uint32_t at = cursor + 1;
  if (seriesLen > end - at) return false;
  at += seriesLen;
  uint8_t hasIndex = 0;
  if (at >= end || !readAt(head.nameStart + at, &hasIndex, sizeof(hasIndex)) || hasIndex > 1) return false;
  ++at;
  if (hasIndex == 0) return at == end;
  if (sizeof(float) != end - at) return false;
  uint8_t raw[sizeof(float)];
  float value = 0;
  if (!readAt(head.nameStart + at, raw, sizeof(raw))) return false;
  memcpy(&value, raw, sizeof(value));
  return std::isfinite(value);
}

bool LibraryIndexFile::readSeries(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened) return false;
  if (head.formatVersion == CLIX_LEGACY_FORMAT_VERSION) return true;
  uint32_t cursor = 0;
  uint32_t end = 0;
  if (!readSeriesExtension(record, cursor, end)) return false;
  uint8_t len = 0;
  if (!readAt(head.nameStart + cursor, &len, sizeof(len))) return false;
  out.resize(len);
  if (len == 0 || readAt(head.nameStart + cursor + 1, out.data(), len)) return true;
  out.clear();
  return false;
}

bool LibraryIndexFile::readSeriesIndex(const ClixRecord& record, std::optional<float>& out) {
  out.reset();
  if (!opened) return false;
  if (head.formatVersion == CLIX_LEGACY_FORMAT_VERSION) return true;
  uint32_t cursor = 0;
  uint32_t end = 0;
  if (!readSeriesExtension(record, cursor, end)) return false;
  uint8_t seriesLen = 0;
  if (!readAt(head.nameStart + cursor, &seriesLen, sizeof(seriesLen))) return false;
  cursor += 1 + seriesLen;
  uint8_t hasIndex = 0;
  if (!readAt(head.nameStart + cursor, &hasIndex, sizeof(hasIndex))) return false;
  if (hasIndex == 0 || seriesLen == 0) return true;
  uint8_t raw[sizeof(float)];
  float value = 0;
  if (!readAt(head.nameStart + cursor + 1, raw, sizeof(raw))) return false;
  memcpy(&value, raw, sizeof(value));
  out = value;
  return true;
}

bool LibraryIndexFile::readAuthor(const ClixRecord& record, std::string& out) {
  return readBlobField(record, 0, out) && !out.empty();
}

// The book's own title, after the name and the author. Absent (length 0) for a
// book that never told us one, in which case the caller shows the filename.
bool LibraryIndexFile::readTitle(const ClixRecord& record, std::string& out) {
  return readBlobField(record, 1, out) && !out.empty();
}

bool LibraryIndexFile::readSourceAuthor(const ClixRecord& record, std::string& out) {
  return readBlobField(record, 2, out);
}

bool LibraryIndexFile::readPath(const ClixRecord& record, std::string& out) {
  out.clear();
  if (!opened || record.folderId >= head.folderCount) return false;

  // Folder records are variable length, so reaching folder n means walking the
  // n preceding length bytes. At one seek per folder this is only done when a
  // book is opened or its details are shown, never while paging.
  uint32_t offset = head.folderStart;
  const uint32_t folderEnd = head.folderStart + head.folderLen;
  for (uint16_t i = 0; i <= record.folderId; i++) {
    if (offset >= folderEnd) return false;
    uint8_t pathLen = 0;
    if (!readAt(offset, &pathLen, sizeof(pathLen)) || pathLen == 0) return false;
    if (pathLen > folderEnd - offset - 1u) return false;
    if (i == record.folderId) {
      std::string dir(pathLen, '\0');
      if (!readAt(offset + 1, dir.data(), pathLen)) return false;
      std::string name;
      if (!readName(record, name)) return false;
      out = joinLibraryPath(dir, name);
      return true;
    }
    offset += 1u + pathLen;
    if (offset >= folderEnd) return false;
  }
  return false;
}

}  // namespace library
