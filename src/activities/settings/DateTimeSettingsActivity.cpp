#include "DateTimeSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <components/controls/button.h>
#include <components/lists/stepper-row.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_DEC = 1;
constexpr fui::ActionId ACTION_INC = 2;
constexpr fui::ActionId ACTION_ROW = 3;
constexpr fui::ActionId ACTION_SAVE = 4;

// The build year bounds the entry: a clock set earlier than the firmware it
// runs is certainly wrong.
constexpr int BUILD_YEAR =
    (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 + (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0');
// A few years of headroom only: the trusted-time floor (TrustedTime.h) never
// moves back, so on boards without an RTC a date typed far in the future
// would survive later corrections once the device sleeps.
constexpr int MAX_YEAR = BUILD_YEAR + 5;

constexpr StrId FIELD_LABELS[] = {StrId::STR_DAY, StrId::STR_MONTH, StrId::STR_YEAR, StrId::STR_HOUR,
                                  StrId::STR_MINUTE};

int daysInMonth(const int year, const int month) {
  static constexpr int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 2 && leap ? 29 : DAYS[month - 1];
}

// Wraps value into [low, high].
int wrap(const int value, const int low, const int high) {
  const int span = high - low + 1;
  return low + ((value - low) % span + span) % span;
}
}  // namespace

DateTimeSettingsActivity::DateTimeSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("DateTimeSettings", renderer, mappedInput), UiAppHost(renderer) {}

void DateTimeSettingsActivity::onEnter() {
  Activity::onEnter();
  // Start from the clock when it holds a plausible date, else from the
  // build year so the user only adjusts the rest.
  struct tm local{};
  bool known = halClock.plausibleLocalTime(local);
  if (!known) {
    const time_t now = time(nullptr);
    known = localtime_r(&now, &local) && local.tm_year + 1900 >= HalClock::MIN_PLAUSIBLE_YEAR;
  }
  if (known) {
    values[FIELD_DAY] = local.tm_mday;
    values[FIELD_MONTH] = local.tm_mon + 1;
    values[FIELD_YEAR] = std::clamp(local.tm_year + 1900, BUILD_YEAR, MAX_YEAR);
    values[FIELD_HOUR] = local.tm_hour;
    values[FIELD_MINUTE] = local.tm_min;
  } else {
    values[FIELD_DAY] = 1;
    values[FIELD_MONTH] = 1;
    values[FIELD_YEAR] = BUILD_YEAR;
    values[FIELD_HOUR] = 12;
    values[FIELD_MINUTE] = 0;
  }

  resetUi();
  app.on(ACTION_DEC, &DateTimeSettingsActivity::onStep, this);
  app.on(ACTION_INC, &DateTimeSettingsActivity::onStep, this);
  app.on(ACTION_ROW, &DateTimeSettingsActivity::onRow, this);
  app.on(ACTION_SAVE, &DateTimeSettingsActivity::onSave, this);
  app.setScreen(&DateTimeSettingsActivity::screenFn, this);
  requestUpdate();
}

void DateTimeSettingsActivity::step(const int field, const int delta) {
  switch (field) {
    case FIELD_DAY:
      values[FIELD_DAY] = wrap(values[FIELD_DAY] + delta, 1, daysInMonth(values[FIELD_YEAR], values[FIELD_MONTH]));
      break;
    case FIELD_MONTH:
      values[FIELD_MONTH] = wrap(values[FIELD_MONTH] + delta, 1, 12);
      break;
    case FIELD_YEAR:
      values[FIELD_YEAR] = std::clamp(values[FIELD_YEAR] + delta, BUILD_YEAR, MAX_YEAR);
      break;
    case FIELD_HOUR:
      values[FIELD_HOUR] = wrap(values[FIELD_HOUR] + delta, 0, 23);
      break;
    case FIELD_MINUTE:
      values[FIELD_MINUTE] = wrap(values[FIELD_MINUTE] + delta, 0, 59);
      break;
    default:
      return;
  }
  // A shorter month (or February of a common year) caps the day.
  values[FIELD_DAY] = std::min(values[FIELD_DAY], daysInMonth(values[FIELD_YEAR], values[FIELD_MONTH]));
  selected = field;
  saveFailed = false;
  requestUpdate();
}

void DateTimeSettingsActivity::save() {
  struct tm local{};
  local.tm_year = values[FIELD_YEAR] - 1900;
  local.tm_mon = values[FIELD_MONTH] - 1;
  local.tm_mday = values[FIELD_DAY];
  local.tm_hour = values[FIELD_HOUR];
  local.tm_min = values[FIELD_MINUTE];
  if (!halClock.setLocalTime(local)) {
    LOG_ERR("DATETIME", "Could not set the clock");
    saveFailed = true;
    requestUpdate();
    return;
  }
  // Deliberately not clockHasBeenSynced: that flag means "synced over the
  // network once" and gates the automatic sync on the first Wi-Fi join, which
  // should still correct a hand-set clock.
  app.clearTapFlash();  // the tap leaves this screen
  finish();
}

void DateTimeSettingsActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void DateTimeSettingsActivity::onStep(const fui::ActionEvent& event, void* user) {
  static_cast<DateTimeSettingsActivity*>(user)->step(event.value, event.action == ACTION_INC ? 1 : -1);
}

void DateTimeSettingsActivity::onRow(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<DateTimeSettingsActivity*>(user);
  self->selected = event.value;
  self->requestUpdate();
}

void DateTimeSettingsActivity::onSave(const fui::ActionEvent&, void* user) {
  static_cast<DateTimeSettingsActivity*>(user)->save();
}

void DateTimeSettingsActivity::loop() {
  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selected == SAVE_INDEX) {
      save();
    } else {
      selected = std::min(selected + 1, static_cast<int>(SAVE_INDEX));
      requestUpdate();
    }
    return;
  }
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
    selected = (selected + SAVE_INDEX) % (SAVE_INDEX + 1);
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
    selected = (selected + 1) % (SAVE_INDEX + 1);
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] {
    if (selected < SAVE_INDEX) step(selected, -1);
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] {
    if (selected < SAVE_INDEX) step(selected, +1);
  });
}

void DateTimeSettingsActivity::screenFn(UiScreen& screen, void* user) {
  static_cast<DateTimeSettingsActivity*>(user)->buildScreen(screen);
}

void DateTimeSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const bool buttonCursor = !mappedInput.hasTouch();

  // What will be saved, as a single readable line.
  char preview[32];
  snprintf(preview, sizeof(preview), "%02d/%02d/%04d  %02d:%02d", values[FIELD_DAY], values[FIELD_MONTH],
           values[FIELD_YEAR], values[FIELD_HOUR], values[FIELD_MINUTE]);
  fui::TextStyle previewText = theme.titleText;
  previewText.bold = true;
  previewText.align = fui::TextAlign::Center;
  screen.target().text(
      screen.takeTop(static_cast<int16_t>(screen.target().lineHeight(previewText.font) + 8), theme.spaceMd), preview,
      previewText);

  char texts[FIELD_COUNT][8];
  const int16_t rowHeight = static_cast<int16_t>(std::max<int>(theme.minTouchSize, theme.rowHeight));
  for (int field = 0; field < FIELD_COUNT; ++field) {
    snprintf(texts[field], sizeof(texts[field]), field == FIELD_YEAR ? "%04d" : "%02d", values[field]);
    fui::StepperRowProps props;
    props.row.label = I18N.get(FIELD_LABELS[field]);
    props.row.labelText = theme.bodyText;
    props.row.valueText = theme.bodyText;
    props.row.valueText.bold = true;
    props.row.styles = theme.listRow;
    props.row.action = ACTION_ROW;
    props.row.valueId = static_cast<int16_t>(field);
    props.row.inputMask = fui::InputTouch;
    props.row.state = buttonCursor && selected == field ? fui::StateSelected : fui::StateNormal;
    props.row.minTouchSize = theme.minTouchSize;
    props.value = texts[field];
    props.widestValue = "0000";
    props.decrement = ACTION_DEC;
    props.increment = ACTION_INC;
    props.decrementValue = static_cast<int16_t>(field);
    props.incrementValue = static_cast<int16_t>(field);
    props.buttonRadius = static_cast<uint8_t>(theme.controlRadius);
    fui::stepperRow(screen.frame(), screen.takeTop(rowHeight, theme.spaceSm), props);
  }

  if (saveFailed) {
    fui::TextStyle error = theme.smallText;
    error.align = fui::TextAlign::Center;
    screen.target().text(screen.takeTop(static_cast<int16_t>(screen.target().lineHeight(error.font) + 4)),
                         tr(STR_DATE_TIME_FAILED), error);
  }

  fui::ButtonProps saveButton;
  saveButton.label = tr(STR_SAVE);
  saveButton.action = ACTION_SAVE;
  saveButton.inputMask = fui::InputTouch;
  saveButton.text = theme.bodyText;
  saveButton.text.bold = true;
  saveButton.text.align = fui::TextAlign::Center;
  saveButton.radius = static_cast<uint8_t>(theme.controlRadius);
  saveButton.state = buttonCursor && selected == SAVE_INDEX ? fui::StateSelected : fui::StateNormal;
  saveButton.minTouchSize = theme.minTouchSize;
  const fui::Rect saveRow = screen.takeTop(static_cast<int16_t>(rowHeight + theme.spaceMd), theme.spaceMd);
  const int16_t saveWidth = static_cast<int16_t>(saveRow.width / 2);
  fui::button(screen.frame(),
              fui::Rect{static_cast<int16_t>(saveRow.x + (saveRow.width - saveWidth) / 2),
                        static_cast<int16_t>(saveRow.y + theme.spaceMd), saveWidth, rowHeight},
              saveButton);
}

void DateTimeSettingsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_DATE_TIME));
  renderUi();
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), selected == SAVE_INDEX ? tr(STR_SAVE) : tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
