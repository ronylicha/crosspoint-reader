#pragma once

#include <ReadingLog.h>

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/themes/BaseTheme.h"

// Statistics tab: today's figures, the last seven days, lifetime totals and
// the books read most recently. Back returns Home. While the date is unknown
// the week tile offers a "Set the date" button (Confirm on button boards).
class StatsActivity final : public Activity, private UiAppHost {
 public:
  StatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Stats", renderer, mappedInput), UiAppHost(renderer) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct BookLine {
    std::string title;
    uint32_t seconds = 0;
    uint32_t pages = 0;
    bool finished = false;
  };

  static void statsScreen(UiScreen& screen, void* user);
  static void onSetDateEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildStatsScreen(UiScreen& screen);

  // Recomputes today, the week and the book lines from the store.
  void reloadData();
  void openDateSettings();
  bool dateUnknown() const { return today == readingstats::UNKNOWN_DAY; }

  void drawToday(Rect area) const;
  void drawWeek(UiScreen& screen, Rect area) const;
  void drawLifetime(Rect area) const;
  void drawBooks(Rect area) const;
  void drawEmpty(Rect area) const;
  int sectionTitle(int x, int y, const char* title) const;

  // Read straight from the store: only the reader writes it, and no reader
  // runs while this screen is up, so no copy (or its allocations) is needed.
  static const readingstats::ReadingLog& log();
  readingstats::DayNumber today = readingstats::UNKNOWN_DAY;
  readingstats::DayTotals week[7]{};
  std::vector<BookLine> bookLines;
};
