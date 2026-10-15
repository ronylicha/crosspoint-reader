#include "ProgressQueue.h"

#include <algorithm>
#include <utility>

namespace koreader {

uint32_t ProgressQueue::upsert(QueuedProgress entry) {
  const auto existing = std::find_if(queued.begin(), queued.end(),
                                     [&entry](const QueuedProgress& item) { return item.document == entry.document; });
  if (existing != queued.end()) queued.erase(existing);
  if (queued.size() >= MAX_ENTRIES) queued.erase(queued.begin());

  entry.revision = revisionCounter++;
  if (revisionCounter == 0) revisionCounter = 1;  // 0 never names a queued version
  const uint32_t revision = entry.revision;
  queued.push_back(std::move(entry));
  return revision;
}

bool ProgressQueue::removeIfUnchanged(const std::string& document, const uint32_t revision) {
  const auto existing = std::find_if(queued.begin(), queued.end(), [&](const QueuedProgress& item) {
    return item.document == document && item.revision == revision;
  });
  if (existing == queued.end()) return false;
  queued.erase(existing);
  return true;
}

void ProgressQueue::restore(std::vector<QueuedProgress> entries, const uint32_t nextRevision) {
  queued = std::move(entries);
  if (queued.size() > MAX_ENTRIES) queued.erase(queued.begin(), queued.end() - MAX_ENTRIES);
  revisionCounter = std::max<uint32_t>(nextRevision, 1);
  for (const auto& item : queued) revisionCounter = std::max(revisionCounter, item.revision + 1);
}

void ProgressQueue::clear() {
  queued.clear();
  revisionCounter = 1;
}

}  // namespace koreader
