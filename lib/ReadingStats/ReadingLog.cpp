#include "ReadingLog.h"

#include <algorithm>
#include <utility>

namespace readingstats {

DayNumber daysFromCivil(int year, const unsigned month, const unsigned day) {
  year -= month <= 2 ? 1 : 0;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<DayNumber>(dayOfEra) - 719468;
}

DayTotals* ReadingLog::findDay(const DayNumber day) {
  for (auto& entry : dayHistory) {
    if (entry.day == day) return &entry;
  }
  return nullptr;
}

const DayTotals* ReadingLog::findDay(const DayNumber day) const {
  for (const auto& entry : dayHistory) {
    if (entry.day == day) return &entry;
  }
  return nullptr;
}

const BookTotals* ReadingLog::book(const std::string& path) const {
  for (const auto& entry : bookHistory) {
    if (entry.path == path) return &entry;
  }
  return nullptr;
}

void ReadingLog::record(const DayNumber day, const std::string& bookPath, const Activity& activity) {
  if (activity.empty()) return;

  auto bookIt = std::find_if(bookHistory.begin(), bookHistory.end(),
                             [&](const BookTotals& entry) { return entry.path == bookPath; });
  if (bookIt == bookHistory.end()) {
    BookTotals fresh;
    fresh.path = bookPath;
    bookHistory.insert(bookHistory.begin(), std::move(fresh));
    if (bookHistory.size() > MAX_BOOKS) bookHistory.pop_back();
    bookIt = bookHistory.begin();
  } else {
    std::rotate(bookHistory.begin(), bookIt, bookIt + 1);
    bookIt = bookHistory.begin();
  }
  const bool newlyFinished = activity.finishedBook && !bookIt->finished;
  bookIt->seconds += activity.seconds;
  bookIt->pages += activity.pages;
  bookIt->chapters += activity.chapters;
  bookIt->finished = bookIt->finished || activity.finishedBook;
  if (day != UNKNOWN_DAY) bookIt->lastDay = day;

  totals.seconds += activity.seconds;
  totals.pages += activity.pages;
  totals.chapters += activity.chapters;
  if (newlyFinished) ++totals.booksFinished;

  if (day == UNKNOWN_DAY) return;
  DayTotals* entry = findDay(day);
  if (!entry) {
    DayTotals fresh;
    fresh.day = day;
    dayHistory.push_back(fresh);
    normalize();
    entry = findDay(day);
    // normalize() may drop the new day when it predates the whole window.
    if (!entry) return;
  }
  entry->seconds += activity.seconds;
  entry->pages += activity.pages;
  entry->chapters += activity.chapters;
  if (newlyFinished) ++entry->booksFinished;
}

DayTotals ReadingLog::dayTotals(const DayNumber day) const {
  const DayTotals* entry = findDay(day);
  if (entry) return *entry;
  DayTotals empty;
  empty.day = day;
  return empty;
}

uint32_t ReadingLog::streak(const DayNumber today) const {
  if (today == UNKNOWN_DAY) return 0;
  const auto readOn = [this](const DayNumber day) {
    const DayTotals* entry = findDay(day);
    return entry && (entry->seconds > 0 || entry->pages > 0);
  };
  DayNumber day = readOn(today) ? today : today - 1;
  uint32_t count = 0;
  while (readOn(day)) {
    ++count;
    --day;
  }
  return count;
}

void ReadingLog::lastDays(const DayNumber today, DayTotals* out, const size_t count) const {
  for (size_t i = 0; i < count; ++i) {
    out[i] = dayTotals(today - static_cast<DayNumber>(count - 1 - i));
  }
}

uint32_t ReadingLog::secondsPerPage() const {
  return totals.pages > 0 ? static_cast<uint32_t>(totals.seconds / totals.pages) : 0;
}

void ReadingLog::clear() {
  totals = LifetimeTotals{};
  dayHistory.clear();
  bookHistory.clear();
}

void ReadingLog::restoreDay(const DayTotals& day) {
  if (day.day == UNKNOWN_DAY || findDay(day.day)) return;
  dayHistory.push_back(day);
}

void ReadingLog::restoreBook(BookTotals book) {
  if (book.path.empty() || this->book(book.path)) return;
  bookHistory.push_back(std::move(book));
}

void ReadingLog::normalize() {
  std::sort(dayHistory.begin(), dayHistory.end(), [](const DayTotals& a, const DayTotals& b) { return a.day < b.day; });
  if (dayHistory.size() > MAX_DAYS) {
    dayHistory.erase(dayHistory.begin(), dayHistory.end() - static_cast<std::ptrdiff_t>(MAX_DAYS));
  }
  if (bookHistory.size() > MAX_BOOKS) bookHistory.resize(MAX_BOOKS);
}

void SessionTracker::noteTurn(const bool forward, const bool succeeded) { pendingForward = forward && succeeded; }

void SessionTracker::onPageRendered(const uint32_t monotonicMs, const int chapterIndex) {
  if (pendingForward && hasAnchor) {
    const uint32_t elapsedSeconds = (monotonicMs - anchorMs) / 1000;
    if (elapsedSeconds >= MIN_PAGE_SECONDS) {
      pending.seconds += std::min(elapsedSeconds, MAX_PAGE_SECONDS);
    }
    ++pending.pages;
    // Reading into the next chapter counts it; menu jumps and backward
    // moves only re-anchor.
    if (chapterIndex >= 0 && lastChapter >= 0 && chapterIndex == lastChapter + 1) ++pending.chapters;
  }
  anchorMs = monotonicMs;
  lastChapter = chapterIndex;
  hasAnchor = true;
  pendingForward = false;
}

void SessionTracker::noteFinished() {
  if (finishedNoted) return;
  finishedNoted = true;
  pending.finishedBook = true;
}

Activity SessionTracker::takePending() { return std::exchange(pending, Activity{}); }

}  // namespace readingstats
