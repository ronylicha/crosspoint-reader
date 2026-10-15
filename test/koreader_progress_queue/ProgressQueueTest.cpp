#include <gtest/gtest.h>

#include "ProgressQueue.h"

using koreader::ProgressQueue;
using koreader::QueuedProgress;

namespace {
QueuedProgress progressFor(const std::string& document, const float percentage) {
  QueuedProgress entry;
  entry.document = document;
  entry.bookPath = "/" + document + ".epub";
  entry.percentage = percentage;
  return entry;
}
}  // namespace

TEST(KOReaderProgressQueue, NewerPositionReplacesTheQueuedOneForTheSameBook) {
  ProgressQueue queue;
  queue.upsert(progressFor("a", 0.10f));
  queue.upsert(progressFor("b", 0.20f));
  queue.upsert(progressFor("a", 0.30f));

  ASSERT_EQ(queue.entries().size(), 2u);
  EXPECT_EQ(queue.entries()[0].document, "b");
  EXPECT_EQ(queue.entries()[1].document, "a");
  EXPECT_FLOAT_EQ(queue.entries()[1].percentage, 0.30f);
}

TEST(KOReaderProgressQueue, FullQueueDropsTheOldestBook) {
  ProgressQueue queue;
  for (size_t i = 0; i <= ProgressQueue::MAX_ENTRIES; ++i) queue.upsert(progressFor(std::to_string(i), 0.5f));

  ASSERT_EQ(queue.entries().size(), ProgressQueue::MAX_ENTRIES);
  EXPECT_EQ(queue.entries().front().document, "1");
  EXPECT_EQ(queue.entries().back().document, std::to_string(ProgressQueue::MAX_ENTRIES));
}

TEST(KOReaderProgressQueue, UploadedVersionIsRemovedButANewerOneStays) {
  ProgressQueue queue;
  const uint32_t sent = queue.upsert(progressFor("a", 0.10f));
  const uint32_t other = queue.upsert(progressFor("b", 0.20f));

  EXPECT_TRUE(queue.removeIfUnchanged("b", other));
  // "a" moved on while its older position was being uploaded.
  queue.upsert(progressFor("a", 0.40f));
  EXPECT_FALSE(queue.removeIfUnchanged("a", sent));

  ASSERT_EQ(queue.entries().size(), 1u);
  EXPECT_FLOAT_EQ(queue.entries()[0].percentage, 0.40f);
}

TEST(KOReaderProgressQueue, RestoreKeepsTheNewestEntriesAndMonotonicRevisions) {
  std::vector<QueuedProgress> stored;
  for (size_t i = 0; i < ProgressQueue::MAX_ENTRIES + 3; ++i) {
    QueuedProgress entry = progressFor(std::to_string(i), 0.1f);
    entry.revision = static_cast<uint32_t>(10 + i);
    stored.push_back(entry);
  }
  ProgressQueue queue;
  queue.restore(stored, 2);

  ASSERT_EQ(queue.entries().size(), ProgressQueue::MAX_ENTRIES);
  EXPECT_EQ(queue.entries().front().document, "3");
  // A stale counter in the file must not reissue a revision already queued.
  EXPECT_GT(queue.upsert(progressFor("new", 0.2f)), queue.entries()[queue.entries().size() - 2].revision);
}
