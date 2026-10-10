#pragma once

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Manual date and time entry. Writes the system clock and, where the board
// has one, the RTC (HalClock::setLocalTime). Reached from Settings → System
// (Clock on RTC boards) and from the statistics when the date is unknown.
//
// Touch: -/+ on each row, Save. Buttons: side Up/Down pick a row, front
// Left/Right change it, Confirm saves, Back cancels.
class DateTimeSettingsActivity final : public Activity, private UiAppHost {
 public:
  DateTimeSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum Field { FIELD_DAY, FIELD_MONTH, FIELD_YEAR, FIELD_HOUR, FIELD_MINUTE, FIELD_COUNT };
  // Selection index of the Save button, after the fields.
  static constexpr int SAVE_INDEX = FIELD_COUNT;

  static void screenFn(UiScreen& screen, void* user);
  static void onStep(const freeink::ui::ActionEvent& event, void* user);
  static void onRow(const freeink::ui::ActionEvent& event, void* user);
  static void onSave(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);

  void step(int field, int delta);
  void save();
  void cancel();

  int values[FIELD_COUNT]{};
  int selected = 0;
  bool saveFailed = false;
  ButtonNavigator buttonNavigator;
};
