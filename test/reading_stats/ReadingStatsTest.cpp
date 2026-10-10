#include <gtest/gtest.h>

#include "ReadingLog.h"

namespace {

using readingstats::Activity;
using readingstats::DayTotals;
using readingstats::ReadingLog;
using readingstats::SessionTracker;
using readingstats::UNKNOWN_DAY;

Activity slice(uint32_t seconds, uint32_t pages, uint32_t chapters = 0, bool finished = false) {
  Activity activity;
  activity.seconds = seconds;
  activity.pages = pages;
  activity.chapters = chapters;
  activity.finishedBook = finished;
  return activity;
}

TEST(ReadingStatsCalendar, CountsDaysFromTheUnixEpoch) {
  EXPECT_EQ(readingstats::daysFromCivil(1970, 1, 1), 0);
  EXPECT_EQ(readingstats::daysFromCivil(1970, 1, 2), 1);
  EXPECT_EQ(readingstats::daysFromCivil(2000, 3, 1), 11017);
  EXPECT_EQ(readingstats::daysFromCivil(2026, 10, 10), 20736);
  // Leap day and the following day stay contiguous.
  EXPECT_EQ(readingstats::daysFromCivil(2024, 3, 1) - readingstats::daysFromCivil(2024, 2, 29), 1);
}

TEST(ReadingStatsLog, RecordsLifetimeDayAndBookTotals) {
  ReadingLog log;
  log.record(100, "/a.epub", slice(60, 3, 1));
  log.record(100, "/a.epub", slice(30, 2));

  EXPECT_EQ(log.lifetime().seconds, 90u);
  EXPECT_EQ(log.lifetime().pages, 5u);
  EXPECT_EQ(log.lifetime().chapters, 1u);
  const DayTotals day = log.dayTotals(100);
  EXPECT_EQ(day.seconds, 90u);
  EXPECT_EQ(day.pages, 5u);
  ASSERT_NE(log.book("/a.epub"), nullptr);
  EXPECT_EQ(log.book("/a.epub")->pages, 5u);
  EXPECT_EQ(log.book("/a.epub")->lastDay, 100);
}

TEST(ReadingStatsLog, UnknownDayFeedsTotalsButNoDay) {
  ReadingLog log;
  log.record(UNKNOWN_DAY, "/a.epub", slice(40, 2));

  EXPECT_EQ(log.lifetime().pages, 2u);
  EXPECT_TRUE(log.days().empty());
  EXPECT_EQ(log.book("/a.epub")->lastDay, UNKNOWN_DAY);
}

TEST(ReadingStatsLog, CountsAFinishedBookOnlyOnce) {
  ReadingLog log;
  log.record(100, "/a.epub", slice(10, 1, 0, true));
  log.record(101, "/a.epub", slice(10, 1, 0, true));

  EXPECT_EQ(log.lifetime().booksFinished, 1u);
  EXPECT_EQ(log.dayTotals(100).booksFinished, 1u);
  EXPECT_EQ(log.dayTotals(101).booksFinished, 0u);
}

TEST(ReadingStatsLog, KeepsMostRecentBookFirstAndCapsHistory) {
  ReadingLog log;
  for (size_t i = 0; i < ReadingLog::MAX_BOOKS + 2; ++i) {
    log.record(100, "/book" + std::to_string(i), slice(5, 1));
  }
  log.record(100, "/book5", slice(5, 1));

  ASSERT_EQ(log.books().size(), ReadingLog::MAX_BOOKS);
  EXPECT_EQ(log.books().front().path, "/book5");
  EXPECT_EQ(log.book("/book0"), nullptr);
}

TEST(ReadingStatsLog, KeepsALimitedSortedDayWindow) {
  ReadingLog log;
  for (int day = 200; day > 200 - static_cast<int>(ReadingLog::MAX_DAYS) - 5; --day) {
    log.record(day, "/a.epub", slice(10, 1));
  }

  ASSERT_EQ(log.days().size(), ReadingLog::MAX_DAYS);
  EXPECT_EQ(log.days().front().day, 200 - static_cast<int>(ReadingLog::MAX_DAYS) + 1);
  EXPECT_EQ(log.days().back().day, 200);
  // A day older than the whole window is dropped, not written over another.
  log.record(10, "/a.epub", slice(10, 1));
  EXPECT_EQ(log.days().front().day, 200 - static_cast<int>(ReadingLog::MAX_DAYS) + 1);
}

TEST(ReadingStatsLog, StreakSurvivesAnEmptyToday) {
  ReadingLog log;
  log.record(98, "/a.epub", slice(10, 1));
  log.record(99, "/a.epub", slice(10, 1));
  log.record(97, "/a.epub", slice(10, 1));
  log.record(95, "/a.epub", slice(10, 1));

  EXPECT_EQ(log.streak(100), 3u);
  log.record(100, "/a.epub", slice(10, 1));
  EXPECT_EQ(log.streak(100), 4u);
  EXPECT_EQ(log.streak(102), 0u);
  EXPECT_EQ(log.streak(UNKNOWN_DAY), 0u);
}

TEST(ReadingStatsLog, LastDaysAreOldestFirstAndZeroFilled) {
  ReadingLog log;
  log.record(98, "/a.epub", slice(60, 2));
  DayTotals week[7];
  log.lastDays(100, week, 7);

  EXPECT_EQ(week[0].day, 94);
  EXPECT_EQ(week[6].day, 100);
  EXPECT_EQ(week[4].pages, 2u);
  EXPECT_EQ(week[6].pages, 0u);
}

TEST(ReadingStatsLog, NormalizeRepairsRestoredHistory) {
  ReadingLog log;
  DayTotals later;
  later.day = 20;
  DayTotals earlier;
  earlier.day = 10;
  log.restoreDay(later);
  log.restoreDay(earlier);
  log.restoreDay(later);
  log.normalize();

  ASSERT_EQ(log.days().size(), 2u);
  EXPECT_EQ(log.days().front().day, 10);
}

TEST(ReadingStatsTracker, CreditsForwardDwellsWithinBounds) {
  SessionTracker tracker;
  tracker.onPageRendered(0, 0);
  tracker.noteTurn(true, true);
  tracker.onPageRendered(30000, 0);
  tracker.noteTurn(true, true);
  tracker.onPageRendered(31000, 0);  // too quick: page counts, time does not
  tracker.noteTurn(true, true);
  tracker.onPageRendered(31000 + 3600000, 0);  // idle hour capped

  const Activity activity = tracker.takePending();
  EXPECT_EQ(activity.pages, 3u);
  EXPECT_EQ(activity.seconds, 30u + SessionTracker::MAX_PAGE_SECONDS);
  EXPECT_FALSE(tracker.hasPending());
}

TEST(ReadingStatsTracker, IgnoresBackwardAndFailedTurns) {
  SessionTracker tracker;
  tracker.onPageRendered(0, 0);
  tracker.noteTurn(false, true);
  tracker.onPageRendered(10000, 0);
  tracker.noteTurn(true, false);
  tracker.onPageRendered(20000, 0);

  EXPECT_FALSE(tracker.hasPending());
}

TEST(ReadingStatsTracker, CountsChaptersReadIntoNotJumpedTo) {
  SessionTracker tracker;
  tracker.onPageRendered(0, 3);
  tracker.noteTurn(true, true);
  tracker.onPageRendered(5000, 4);
  tracker.onPageRendered(6000, 9);  // menu jump: re-anchors only
  tracker.noteTurn(true, true);
  tracker.onPageRendered(9000, 9);

  EXPECT_EQ(tracker.takePending().chapters, 1u);
}

TEST(ReadingStatsTracker, FlushesAfterEnoughPagesAndNotesFinishOnce) {
  SessionTracker tracker;
  tracker.onPageRendered(0, -1);
  for (uint32_t i = 1; i <= SessionTracker::FLUSH_PAGES; ++i) {
    EXPECT_FALSE(tracker.shouldFlush());
    tracker.noteTurn(true, true);
    tracker.onPageRendered(i * 3000, -1);
  }
  EXPECT_TRUE(tracker.shouldFlush());
  tracker.noteFinished();
  EXPECT_TRUE(tracker.takePending().finishedBook);
  tracker.noteFinished();
  EXPECT_FALSE(tracker.hasPending());
}

}  // namespace
