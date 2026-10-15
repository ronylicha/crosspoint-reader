#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "KOReaderSyncClient.h"

// Local reading positions waiting to be pushed to the KOReader sync server.
//
// One entry per document: a newer local position replaces the queued one, so
// only the latest position of each book is ever sent. Entries are kept in the
// order they were last updated, oldest first; when the queue is full the
// oldest book is dropped.
namespace koreader {

struct QueuedProgress {
  std::string document;  // KOReader document hash
  std::string bookPath;  // where the book lives on the SD card
  std::string xpath;
  float percentage = 0.0f;
  uint16_t spineIndex = 0;
  uint16_t pageNumber = 0;
  uint16_t totalPages = 1;
  std::optional<uint16_t> paragraphIndex;
  // Captured while the book was open: the upload runs after it is closed.
  std::optional<KOReaderMetadata> metadata;
  // Assigned by the queue on every update; lets an uploader drop only the
  // version it actually sent.
  uint32_t revision = 0;
};

class ProgressQueue {
 public:
  static constexpr size_t MAX_ENTRIES = 12;

  // Queues the document's latest position, replacing any older one. Returns
  // the revision assigned to it.
  uint32_t upsert(QueuedProgress entry);

  // Removes the document's entry only if it still holds `revision`: a newer
  // position queued during the upload stays for the next attempt.
  bool removeIfUnchanged(const std::string& document, uint32_t revision);

  const std::vector<QueuedProgress>& entries() const { return queued; }
  bool empty() const { return queued.empty(); }
  uint32_t nextRevision() const { return revisionCounter; }

  // Storage round trip. Entries beyond MAX_ENTRIES keep the most recent ones;
  // the counter never goes below a stored revision.
  void restore(std::vector<QueuedProgress> entries, uint32_t nextRevision);
  void clear();

 private:
  std::vector<QueuedProgress> queued;
  uint32_t revisionCounter = 1;
};

}  // namespace koreader
