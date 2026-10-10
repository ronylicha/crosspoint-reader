#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>
#include <ReadingLog.h>

#include <string>

// Persists the reading statistics aggregate to /.crosspoint/reading_stats.json.
// The reader feeds it through record(); the home tile and the Statistics tab
// read log() on the main task.
class ReadingStatsStore : public PersistableStore<ReadingStatsStore> {
  readingstats::ReadingLog readingLog;

  ReadingStatsStore() = default;
  ~ReadingStatsStore() = default;

  friend class PersistableStore<ReadingStatsStore>;

 public:
  static constexpr int FORMAT_VERSION = 1;

  static const char* getFilePath() { return "/.crosspoint/reading_stats.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Adds the slice to today's totals and saves. Empty slices are skipped.
  void record(const std::string& bookPath, const readingstats::Activity& activity);

  const readingstats::ReadingLog& log() const { return readingLog; }

  // Local calendar day from the RTC or a trusted network time, or
  // readingstats::UNKNOWN_DAY while the device has no plausible date.
  static readingstats::DayNumber today();
};

#define READING_STATS ReadingStatsStore::getInstance()
