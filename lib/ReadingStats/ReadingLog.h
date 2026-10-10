#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Reading statistics aggregate: lifetime totals, a rolling per-day history and
// a short per-book history. Pure data and arithmetic (no I/O, no clock) so it
// runs unchanged on the device and in host tests; ReadingStatsStore persists it.
namespace readingstats {

// Days are counted since 1970-01-01 in local time. 0 means "date unknown":
// such activity still feeds the lifetime and per-book totals, never a day.
using DayNumber = int32_t;
constexpr DayNumber UNKNOWN_DAY = 0;

// Proleptic Gregorian civil date to day number (Howard Hinnant's algorithm).
DayNumber daysFromCivil(int year, unsigned month, unsigned day);

struct Activity {
  uint32_t seconds = 0;
  uint32_t pages = 0;
  uint32_t chapters = 0;
  bool finishedBook = false;

  bool empty() const { return seconds == 0 && pages == 0 && chapters == 0 && !finishedBook; }
};

struct DayTotals {
  DayNumber day = UNKNOWN_DAY;
  uint32_t seconds = 0;
  uint32_t pages = 0;
  uint32_t chapters = 0;
  uint16_t booksFinished = 0;
};

struct BookTotals {
  std::string path;
  uint32_t seconds = 0;
  uint32_t pages = 0;
  uint32_t chapters = 0;
  DayNumber lastDay = UNKNOWN_DAY;
  bool finished = false;
};

struct LifetimeTotals {
  uint64_t seconds = 0;
  uint64_t pages = 0;
  uint64_t chapters = 0;
  uint32_t booksFinished = 0;
};

class ReadingLog {
 public:
  static constexpr size_t MAX_DAYS = 30;
  static constexpr size_t MAX_BOOKS = 24;

  // Adds one flushed slice of reading. A finished book counts once per path,
  // however many times its last page is reached.
  void record(DayNumber day, const std::string& bookPath, const Activity& activity);

  // Totals of one day, zero-filled when nothing was read that day.
  DayTotals dayTotals(DayNumber day) const;
  // Consecutive reading days ending today; a streak still alive from
  // yesterday counts while today is empty.
  uint32_t streak(DayNumber today) const;
  // The `count` days ending `today`, oldest first, zero-filled.
  void lastDays(DayNumber today, DayTotals* out, size_t count) const;
  // Average seconds per page over the lifetime totals, 0 without pages.
  uint32_t secondsPerPage() const;

  const LifetimeTotals& lifetime() const { return totals; }
  // Most recently read first.
  const std::vector<BookTotals>& books() const { return bookHistory; }
  // Oldest first.
  const std::vector<DayTotals>& days() const { return dayHistory; }
  const BookTotals* book(const std::string& path) const;

  // Deserialization entry points: append in stored order, then normalize()
  // sorts and caps so a hand-edited or legacy file cannot break invariants.
  void clear();
  LifetimeTotals& mutableLifetime() { return totals; }
  void restoreDay(const DayTotals& day);
  void restoreBook(BookTotals book);
  void normalize();

 private:
  DayTotals* findDay(DayNumber day);
  const DayTotals* findDay(DayNumber day) const;

  LifetimeTotals totals;
  std::vector<DayTotals> dayHistory;
  std::vector<BookTotals> bookHistory;
};

// Turns reader events into Activity slices. Time is credited per page, when a
// forward turn ends a dwell of plausible length; the time spent on the page
// shown when the reader closes is not counted. Owned by the reader, mutated on
// the render task under its lock; takePending() hands the slice to the flush.
class SessionTracker {
 public:
  static constexpr uint32_t MIN_PAGE_SECONDS = 2;
  static constexpr uint32_t MAX_PAGE_SECONDS = 600;
  // Flush thresholds that bound the work lost to a crash or battery cut
  // without writing the SD card on every page.
  static constexpr uint32_t FLUSH_PAGES = 20;
  static constexpr uint32_t FLUSH_SECONDS = 300;

  void noteTurn(bool forward, bool succeeded);
  // chapterIndex is the reader's chapter for the rendered page, -1 if unknown.
  void onPageRendered(uint32_t monotonicMs, int chapterIndex);
  void noteFinished();
  bool shouldFlush() const { return pending.pages >= FLUSH_PAGES || pending.seconds >= FLUSH_SECONDS; }
  bool hasPending() const { return !pending.empty(); }
  Activity takePending();

 private:
  Activity pending;
  uint32_t anchorMs = 0;
  int lastChapter = -1;
  bool hasAnchor = false;
  bool pendingForward = false;
  bool finishedNoted = false;
};

}  // namespace readingstats
