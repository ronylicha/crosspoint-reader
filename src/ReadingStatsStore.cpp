#include "ReadingStatsStore.h"

#include <HalClock.h>
#include <Logging.h>

#include <ctime>
#include <utility>

namespace {
readingstats::DayNumber dayFromLocal(const struct tm& local) {
  if (local.tm_year + 1900 < HalClock::MIN_PLAUSIBLE_YEAR) return readingstats::UNKNOWN_DAY;
  return readingstats::daysFromCivil(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
                                     static_cast<unsigned>(local.tm_mday));
}
}  // namespace

readingstats::DayNumber ReadingStatsStore::today() {
  struct tm local{};
  if (halClock.localTime(local)) {
    const auto day = dayFromLocal(local);
    if (day != readingstats::UNKNOWN_DAY) return day;
  }
  // The system clock: SNTP, a manual set or the boot-time floor restore. Not
  // trustedtime::trustedNow(), whose floor never moves back and would keep a
  // wrong future date after the user corrects it.
  const time_t now = time(nullptr);
  if (!localtime_r(&now, &local)) return readingstats::UNKNOWN_DAY;
  return dayFromLocal(local);
}

void ReadingStatsStore::toJson(JsonDocument& doc) const {
  doc["version"] = FORMAT_VERSION;
  const auto& totals = readingLog.lifetime();
  JsonObject lifetime = doc["lifetime"].to<JsonObject>();
  lifetime["seconds"] = totals.seconds;
  lifetime["pages"] = totals.pages;
  lifetime["chapters"] = totals.chapters;
  lifetime["finished"] = totals.booksFinished;

  // Compact rows keep the file small: the day window is 30 entries.
  JsonArray days = doc["days"].to<JsonArray>();
  for (const auto& day : readingLog.days()) {
    JsonArray row = days.add<JsonArray>();
    row.add(day.day);
    row.add(day.seconds);
    row.add(day.pages);
    row.add(day.chapters);
    row.add(day.booksFinished);
  }

  JsonArray books = doc["books"].to<JsonArray>();
  for (const auto& book : readingLog.books()) {
    JsonObject obj = books.add<JsonObject>();
    obj["path"] = book.path;
    obj["seconds"] = book.seconds;
    obj["pages"] = book.pages;
    obj["chapters"] = book.chapters;
    obj["lastDay"] = book.lastDay;
    obj["finished"] = book.finished;
  }
}

bool ReadingStatsStore::fromJson(JsonVariantConst doc) {
  readingLog.clear();
  JsonObjectConst lifetime = doc["lifetime"].as<JsonObjectConst>();
  auto& totals = readingLog.mutableLifetime();
  totals.seconds = lifetime["seconds"] | static_cast<uint64_t>(0);
  totals.pages = lifetime["pages"] | static_cast<uint64_t>(0);
  totals.chapters = lifetime["chapters"] | static_cast<uint64_t>(0);
  totals.booksFinished = lifetime["finished"] | 0u;

  for (JsonArrayConst row : doc["days"].as<JsonArrayConst>()) {
    if (row.size() < 5) continue;
    readingstats::DayTotals day;
    day.day = row[0] | readingstats::UNKNOWN_DAY;
    day.seconds = row[1] | 0u;
    day.pages = row[2] | 0u;
    day.chapters = row[3] | 0u;
    day.booksFinished = row[4] | static_cast<uint16_t>(0);
    readingLog.restoreDay(day);
  }

  for (JsonObjectConst obj : doc["books"].as<JsonArrayConst>()) {
    readingstats::BookTotals book;
    book.path = obj["path"] | "";
    book.seconds = obj["seconds"] | 0u;
    book.pages = obj["pages"] | 0u;
    book.chapters = obj["chapters"] | 0u;
    book.lastDay = obj["lastDay"] | readingstats::UNKNOWN_DAY;
    book.finished = obj["finished"] | false;
    readingLog.restoreBook(std::move(book));
  }
  readingLog.normalize();

  LOG_DBG("RST", "Reading stats loaded (%u days, %u books)", static_cast<unsigned>(readingLog.days().size()),
          static_cast<unsigned>(readingLog.books().size()));
  return true;
}

void ReadingStatsStore::record(const std::string& bookPath, const readingstats::Activity& activity) {
  if (activity.empty()) return;
  {
    std::lock_guard<std::mutex> lock(storeMutex);
    readingLog.record(today(), bookPath, activity);
  }
  if (!saveToFile()) LOG_ERR("RST", "Failed to save reading stats");
}
