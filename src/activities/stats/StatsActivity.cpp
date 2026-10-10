#include "StatsActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "activities/settings/DateTimeSettingsActivity.h"
#include "components/AppNavigation.h"
#include "components/StatsWidgets.h"
#include "components/UITheme.h"
#include "components/icons/appIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_SET_DATE = 1;

constexpr int OUTER_GAP = 10;
constexpr int TILE_PAD = 12;
constexpr int STAT_ROW_HEIGHT = 34;
constexpr int BOOK_ROW_HEIGHT = 36;
constexpr int CHART_HEIGHT = 150;
constexpr size_t MAX_BOOK_LINES = 8;

uint32_t clampToU32(const uint64_t value) { return static_cast<uint32_t>(std::min<uint64_t>(value, UINT32_MAX)); }

int trendOf(const int64_t delta) { return delta > 0 ? 1 : (delta < 0 ? -1 : 0); }
}  // namespace

const readingstats::ReadingLog& StatsActivity::log() { return READING_STATS.log(); }

void StatsActivity::onEnter() {
  Activity::onEnter();
  reloadData();
  resetUi();
  appnav::registerTabs(app, AppTab::Stats);
  app.on(ACTION_SET_DATE, &StatsActivity::onSetDateEvent, this);
  app.setScreen(&StatsActivity::statsScreen, this);
  requestUpdate();
}

void StatsActivity::reloadData() {
  today = ReadingStatsStore::today();
  if (today != readingstats::UNKNOWN_DAY) log().lastDays(today, week, 7);

  bookLines.clear();
  bookLines.reserve(std::min(log().books().size(), MAX_BOOK_LINES));
  for (const auto& book : log().books()) {
    if (bookLines.size() >= MAX_BOOK_LINES) break;
    BookLine line;
    const auto& recents = RECENT_BOOKS.getBooks();
    const auto recent =
        std::find_if(recents.begin(), recents.end(), [&](const RecentBook& entry) { return entry.path == book.path; });
    if (recent != recents.end() && !recent->title.empty()) {
      line.title = recent->title;
    } else {
      const auto slash = book.path.rfind('/');
      line.title = slash == std::string::npos ? book.path : book.path.substr(slash + 1);
    }
    line.seconds = book.seconds;
    line.pages = book.pages;
    line.finished = book.finished;
    bookLines.push_back(std::move(line));
  }
}

void StatsActivity::onExit() {
  Activity::onExit();
  bookLines.clear();
}

void StatsActivity::openDateSettings() {
  auto activity = makeUniqueNoThrow<DateTimeSettingsActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("STATS", "OOM: DateTimeSettingsActivity");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    // Handlers run unlocked; the render task reads these fields.
    {
      RenderLock lock(*this);
      reloadData();
    }
    requestUpdate();
  });
}

void StatsActivity::onSetDateEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<StatsActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->openDateSettings();
}

void StatsActivity::loop() {
  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  if (dateUnknown() && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) openDateSettings();
}

int StatsActivity::sectionTitle(const int x, const int y, const char* title) const {
  renderer.drawText(UI_10_FONT_ID, x, y, title, true, EpdFontFamily::BOLD);
  return renderer.getLineHeight(UI_10_FONT_ID) + 4;
}

void StatsActivity::drawToday(const Rect area) const {
  statswidgets::drawTile(renderer, area);
  const int titleHeight = sectionTitle(area.x + TILE_PAD, area.y + 6, tr(STR_TODAY));
  const int top = area.y + 6 + titleHeight;
  const int cellWidth = (area.width - 2 * TILE_PAD) / 2;
  const int cellHeight = (area.y + area.height - TILE_PAD / 2 - top) / 2;
  const auto todayTotals = log().dayTotals(today);
  const auto yesterday = log().dayTotals(today - 1);

  char value[24];
  char delta[24];
  // Without a date there is no "today": dashes rather than misleading zeros.
  const bool dated = today != readingstats::UNKNOWN_DAY;
  const auto cell = [&](const int index, const char* text, const int trend, const char* trendText, const char* label) {
    const int x = area.x + TILE_PAD + (index % 2) * cellWidth;
    const int y = top + (index / 2) * cellHeight + (cellHeight - statswidgets::figureHeight(renderer)) / 2;
    statswidgets::drawFigure(renderer, x, y, cellWidth - 8, dated ? text : "-", dated ? trend : 0,
                             dated ? trendText : nullptr, label);
  };

  const int64_t secondsDelta = static_cast<int64_t>(todayTotals.seconds) - yesterday.seconds;
  statswidgets::formatDuration(value, sizeof(value), todayTotals.seconds);
  statswidgets::formatDuration(delta, sizeof(delta), clampToU32(std::llabs(secondsDelta)));
  cell(0, value, trendOf(secondsDelta), secondsDelta != 0 ? delta : nullptr, tr(STR_STAT_READING_TIME));

  const int64_t pagesDelta = static_cast<int64_t>(todayTotals.pages) - yesterday.pages;
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(todayTotals.pages));
  snprintf(delta, sizeof(delta), "%u", static_cast<unsigned>(std::llabs(pagesDelta)));
  cell(1, value, trendOf(pagesDelta), pagesDelta != 0 ? delta : nullptr, tr(STR_STAT_PAGES));

  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(todayTotals.chapters));
  cell(2, value, 0, nullptr, tr(STR_STAT_CHAPTERS));

  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(log().streak(today)));
  cell(3, value, 0, nullptr, tr(STR_STAT_STREAK));
}

void StatsActivity::drawWeek(UiScreen& screen, const Rect area) const {
  statswidgets::drawTile(renderer, area);
  const int titleHeight = sectionTitle(area.x + TILE_PAD, area.y + 6, tr(STR_STATS_LAST_7_DAYS));
  const Rect chart{area.x + TILE_PAD, area.y + 8 + titleHeight, area.width - 2 * TILE_PAD,
                   area.height - 14 - titleHeight};
  if (dateUnknown()) {
    const auto& theme = screen.theme();
    fui::ButtonProps button;
    button.label = tr(STR_SET_DATE);
    button.action = ACTION_SET_DATE;
    button.inputMask = fui::InputTouch;  // Confirm is handled in loop()
    button.text = theme.bodyText;
    button.text.bold = true;
    button.text.align = fui::TextAlign::Center;
    button.radius = static_cast<uint8_t>(theme.controlRadius);
    // On button boards the highlight shows what Confirm does.
    button.state = mappedInput.hasTouch() ? fui::StateNormal : fui::StateSelected;

    const int buttonHeight = screen.target().lineHeight(button.text.font) + 2 * theme.spaceMd;
    const int labelWidth = screen.target().measureText(button.text.font, button.label, button.text).width;
    const int buttonWidth = std::min(chart.width, std::max(chart.width / 2, labelWidth + 4 * theme.spaceMd));
    const int messageHeight = std::max(0, chart.height - buttonHeight - theme.spaceSm);
    UITheme::drawCenteredWrappedText(renderer, Rect{chart.x, chart.y, chart.width, messageHeight}, UI_10_FONT_ID,
                                     tr(STR_STATS_NO_DATE), 2);
    screen.button(button, fui::Rect{static_cast<int16_t>(chart.x + (chart.width - buttonWidth) / 2),
                                    static_cast<int16_t>(chart.y + chart.height - buttonHeight),
                                    static_cast<int16_t>(buttonWidth), static_cast<int16_t>(buttonHeight)});
    return;
  }
  uint32_t weekSeconds = 0;
  for (const auto& day : week) weekSeconds += day.seconds;
  char total[24];
  statswidgets::formatDuration(total, sizeof(total), weekSeconds);
  const int totalWidth = renderer.getTextWidth(UI_10_FONT_ID, total, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, area.x + area.width - TILE_PAD - totalWidth, area.y + 6, total, true,
                    EpdFontFamily::BOLD);
  statswidgets::drawDayChart(renderer, chart, week, 7);
}

void StatsActivity::drawLifetime(const Rect area) const {
  statswidgets::drawTile(renderer, area);
  const int titleHeight = sectionTitle(area.x + TILE_PAD, area.y + 6, tr(STR_STATS_ALL_TIME));
  const auto& totals = log().lifetime();
  char value[24];
  int y = area.y + 6 + titleHeight;
  const int width = area.width - 2 * TILE_PAD;
  const auto row = [&](const freeink::Icon& icon, const char* label) {
    if (y + STAT_ROW_HEIGHT > area.y + area.height - TILE_PAD / 2) return;
    statswidgets::drawStatRow(renderer, Rect{area.x + TILE_PAD, y, width, STAT_ROW_HEIGHT}, icon, label, value);
    y += STAT_ROW_HEIGHT;
  };

  statswidgets::formatDuration(value, sizeof(value), clampToU32(totals.seconds));
  row(icon_stat_time_24, tr(STR_STAT_READING_TIME));
  snprintf(value, sizeof(value), "%llu", static_cast<unsigned long long>(totals.pages));
  row(icon_stat_pages_24, tr(STR_STAT_PAGES));
  snprintf(value, sizeof(value), "%llu", static_cast<unsigned long long>(totals.chapters));
  row(icon_stat_chapters_24, tr(STR_STAT_CHAPTERS));
  statswidgets::formatDuration(value, sizeof(value), log().secondsPerPage());
  row(icon_stat_pace_24, tr(STR_STAT_PACE));
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(totals.booksFinished));
  row(icon_stat_finished_24, tr(STR_STAT_FINISHED));
}

void StatsActivity::drawBooks(const Rect area) const {
  if (bookLines.empty() || area.height < BOOK_ROW_HEIGHT * 2) return;
  statswidgets::drawTile(renderer, area);
  const int titleHeight = sectionTitle(area.x + TILE_PAD, area.y + 6, tr(STR_STATS_BOOKS));
  int y = area.y + 6 + titleHeight;
  const int width = area.width - 2 * TILE_PAD;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  char value[48];
  char duration[24];
  for (const auto& line : bookLines) {
    if (y + BOOK_ROW_HEIGHT > area.y + area.height - TILE_PAD / 2) break;
    statswidgets::formatDuration(duration, sizeof(duration), line.seconds);
    if (line.finished) {
      snprintf(value, sizeof(value), "%s · %s", duration, tr(STR_COMPLETED));
    } else {
      snprintf(value, sizeof(value), "%s · %u %s", duration, static_cast<unsigned>(line.pages), tr(STR_STAT_PAGES));
    }
    const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, value);
    const int textY = y + (BOOK_ROW_HEIGHT - lineHeight) / 2;
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, line.title.c_str(), width - valueWidth - 12);
    renderer.drawText(UI_12_FONT_ID, area.x + TILE_PAD, textY, title.c_str(), true);
    renderer.drawText(UI_10_FONT_ID, area.x + TILE_PAD + width - valueWidth,
                      y + (BOOK_ROW_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2, value, true);
    y += BOOK_ROW_HEIGHT;
  }
}

void StatsActivity::drawEmpty(const Rect area) const {
  constexpr int ICON_SIZE = 48;
  const int iconY = area.y + area.height / 3;
  statswidgets::drawIcon(renderer, icon_app_stats_48, area.x + (area.width - ICON_SIZE) / 2, iconY);
  UITheme::drawCenteredWrappedText(renderer, Rect{area.x + 24, iconY + ICON_SIZE + 16, area.width - 48, 120},
                                   UI_12_FONT_ID, tr(STR_STATS_EMPTY), 3, true, EpdFontFamily::REGULAR,
                                   UITheme::TextVerticalAlignment::TOP);
}

void StatsActivity::statsScreen(UiScreen& screen, void* user) {
  static_cast<StatsActivity*>(user)->buildStatsScreen(screen);
}

// The tiles are painted on the renderer; only the tab bar and the date button
// are interactive FreeInkUI elements.
void StatsActivity::buildStatsScreen(UiScreen& screen) {
  appnav::buildChrome(screen, renderer, AppTab::Stats);
  const int top = appnav::contentTop(renderer) + OUTER_GAP;
  const int bottom = appnav::contentBottom(renderer) - OUTER_GAP;
  const Rect content = UITheme::getContentArea(renderer);
  const int left = content.x + OUTER_GAP;
  const int width = content.width - 2 * OUTER_GAP;

  if (log().lifetime().pages == 0 && log().lifetime().seconds == 0) {
    drawEmpty(Rect{left, top, width, bottom - top});
  } else {
    const int titleBlock = renderer.getLineHeight(UI_10_FONT_ID) + 10;
    const int todayHeight = titleBlock + 2 * (statswidgets::figureHeight(renderer) + 12);
    const int lifetimeHeight = titleBlock + 5 * STAT_ROW_HEIGHT + TILE_PAD / 2;
    if (renderer.getScreenWidth() > renderer.getScreenHeight()) {
      const int columnWidth = (width - OUTER_GAP) / 2;
      const int rightX = left + columnWidth + OUTER_GAP;
      drawToday(Rect{left, top, columnWidth, todayHeight});
      drawWeek(screen, Rect{left, top + todayHeight + OUTER_GAP, columnWidth,
                            std::min(CHART_HEIGHT, bottom - top - todayHeight - OUTER_GAP)});
      drawLifetime(Rect{rightX, top, columnWidth, lifetimeHeight});
      const int booksY = top + lifetimeHeight + OUTER_GAP;
      drawBooks(Rect{rightX, booksY, columnWidth, bottom - booksY});
    } else {
      int y = top;
      drawToday(Rect{left, y, width, todayHeight});
      y += todayHeight + OUTER_GAP;
      drawWeek(screen, Rect{left, y, width, CHART_HEIGHT});
      y += CHART_HEIGHT + OUTER_GAP;
      drawLifetime(Rect{left, y, width, lifetimeHeight});
      y += lifetimeHeight + OUTER_GAP;
      drawBooks(Rect{left, y, width, bottom - y});
    }
  }
}

void StatsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), dateUnknown() ? tr(STR_SET_DATE) : "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
