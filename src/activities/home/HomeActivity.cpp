#include "HomeActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Xtc.h>
#include <components/controls/button.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "activities/settings/DateTimeSettingsActivity.h"
#include "components/StatsWidgets.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "fontIds.h"
#include "util/BookProgress.h"

namespace fui = freeink::ui;

namespace {
constexpr int OUTER_GAP = 10;
constexpr int TILE_PAD = 12;
constexpr int CHART_HEIGHT = 120;
constexpr int ROW_HEIGHT = 96;
constexpr int ROW_COVER_HEIGHT = ROW_HEIGHT - TILE_PAD;
constexpr int PROGRESS_HEIGHT = 6;
constexpr int FOCUS_GAP = 3;
constexpr int FOCUS_WIDTH = 3;
constexpr int TILE_RADIUS = 10;
constexpr int EMPTY_ICON_SIZE = 32;

constexpr int SET_DATE_HEIGHT = 40;

constexpr fui::ActionId ACTION_OPEN_BOOK = 1;
constexpr fui::ActionId ACTION_OPEN_STATS = 2;
constexpr fui::ActionId ACTION_SET_DATE = 3;

fui::Rect toUi(const Rect& rect) {
  return fui::Rect{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y), static_cast<int16_t>(rect.width),
                   static_cast<int16_t>(rect.height)};
}

void drawProgress(const GfxRenderer& renderer, const int x, const int y, const int width, const int percent) {
  renderer.drawRect(x, y, width, PROGRESS_HEIGHT);
  const int fill = (width - 2) * std::clamp(percent, 0, 100) / 100;
  if (fill > 0) renderer.fillRect(x + 1, y + 1, fill, PROGRESS_HEIGHT - 2);
}

int coverWidthFor(const int height) { return height * 2 / 3; }
}  // namespace

void HomeActivity::onEnter() {
  Activity::onEnter();
  loadBooks();
  loadFigures();

  coverCache = makeUniqueNoThrow<HomeCoverCache>(renderer);
  if (!coverCache) {
    LOG_ERR("HOME", "OOM: cover cache");
  } else if (!books.empty()) {
    coverCache->begin();
  }

  // The selection is flat: books first, then the visible tabs by bar index.
  const Layout layout = computeLayout();
  const auto tabIndex = [](const AppTab tab) {
    const auto visible = appnav::visibleTabs();
    for (int i = 0; i < visible.count; ++i) {
      if (visible.tabs[i] == tab) return i;
    }
    return 0;
  };
  if (focusTab != AppTab::Home) {
    selection = bookSlots(layout) + tabIndex(focusTab);
  } else if (bookSlots(layout) == 0) {
    selection = tabIndex(AppTab::Library);  // nothing to open: start on the library tab
  } else {
    selection = 0;
  }

  resetUi();
  appnav::registerTabs(app, AppTab::Home);
  app.on(ACTION_OPEN_BOOK, &HomeActivity::onOpenBook, this);
  app.on(ACTION_OPEN_STATS, &HomeActivity::onOpenStats, this);
  app.on(ACTION_SET_DATE, &HomeActivity::onSetDate, this);
  app.setScreen(&HomeActivity::screenFn, this);
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();
  coverCache.reset();
}

void HomeActivity::loadBooks() {
  books.clear();
  books.reserve(MAX_BOOKS);
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (static_cast<int>(books.size()) >= MAX_BOOKS) break;
    if (RecentBooksStore::isMissing(book)) continue;
    books.push_back(book);
  }
  for (size_t i = 0; i < books.size(); ++i) {
    progress[i] = loadBookProgress(books[i].path);
    const readingstats::BookTotals* stats = READING_STATS.log().book(books[i].path);
    finished[i] = progress[i] >= 100 || (stats && stats->finished);
  }
}

void HomeActivity::loadFigures() {
  const auto& log = READING_STATS.log();
  const readingstats::DayNumber today = ReadingStatsStore::today();
  figures.dated = today != readingstats::UNKNOWN_DAY;
  figures.finished = log.lifetime().booksFinished;
  if (!figures.dated) return;
  const auto todayTotals = log.dayTotals(today);
  const auto yesterday = log.dayTotals(today - 1);
  figures.seconds = todayTotals.seconds;
  figures.secondsDelta = static_cast<int32_t>(todayTotals.seconds) - static_cast<int32_t>(yesterday.seconds);
  figures.pages = todayTotals.pages;
  figures.pagesDelta = static_cast<int32_t>(todayTotals.pages) - static_cast<int32_t>(yesterday.pages);
  figures.streak = log.streak(today);
  figures.finishedToday = todayTotals.booksFinished;
  log.lastDays(today, figures.week, 7);
}

HomeActivity::Layout HomeActivity::computeLayout() const {
  Layout layout;
  const Rect content = UITheme::getContentArea(renderer);
  const int left = content.x + OUTER_GAP;
  const int width = content.width - 2 * OUTER_GAP;
  const int top = appnav::contentTop(renderer) + OUTER_GAP;
  const int bottom = appnav::contentBottom(renderer) - OUTER_GAP;
  const int captionHeight = renderer.getLineHeight(UI_12_FONT_ID) + PROGRESS_HEIGHT + 10;
  const bool landscape = renderer.getScreenWidth() > renderer.getScreenHeight();

  // The hero tile hosts the cover (2:3) and, beside it, four figures.
  const auto layoutHero = [&](const Rect hero) {
    layout.hero = hero;
    const int maxCoverHeight = hero.height - 2 * TILE_PAD - captionHeight;
    const int coverWidth = std::min(hero.width * 2 / 5, coverWidthFor(maxCoverHeight));
    const int coverHeight = coverWidth * 3 / 2;
    layout.heroCover = Rect{hero.x + TILE_PAD, hero.y + TILE_PAD, coverWidth, coverHeight};
    layout.heroCaption = Rect{hero.x + TILE_PAD, layout.heroCover.y + coverHeight + 6, coverWidth, captionHeight - 6};
    const int figuresX = layout.heroCover.x + coverWidth + TILE_PAD + 4;
    layout.heroFigures =
        Rect{figuresX, hero.y + TILE_PAD, hero.x + hero.width - TILE_PAD - figuresX, hero.height - 2 * TILE_PAD};
  };
  const auto layoutRows = [&](const int x, int y, const int rowWidth, const int limit) {
    const int available = std::max(0, static_cast<int>(books.size()) - 1);
    while (layout.rowCount < std::min(available, MAX_ROWS) && y + ROW_HEIGHT <= limit) {
      layout.rows[layout.rowCount++] = Rect{x, y, rowWidth, ROW_HEIGHT};
      y += ROW_HEIGHT + OUTER_GAP;
    }
  };

  if (landscape) {
    const int heroWidth = width * 11 / 20;
    layoutHero(Rect{left, top, heroWidth, bottom - top});
    const int sideX = left + heroWidth + OUTER_GAP;
    const int sideWidth = width - heroWidth - OUTER_GAP;
    layout.chart = Rect{sideX, top, sideWidth, CHART_HEIGHT};
    layoutRows(sideX, top + CHART_HEIGHT + OUTER_GAP, sideWidth, bottom);
  } else {
    // Hero gets what the chart and one row leave, capped so the cover stays
    // a sensible size on tall panels.
    const int heroHeight = std::min(bottom - top - CHART_HEIGHT - ROW_HEIGHT - 2 * OUTER_GAP,
                                    2 * TILE_PAD + captionHeight + (width * 2 / 5) * 3 / 2);
    layoutHero(Rect{left, top, width, std::max(heroHeight, 0)});
    layout.chart = Rect{left, layout.hero.y + layout.hero.height + OUTER_GAP, width, CHART_HEIGHT};
    layoutRows(left, layout.chart.y + CHART_HEIGHT + OUTER_GAP, width, bottom);
  }
  return layout;
}

int HomeActivity::bookSlots(const Layout& layout) const { return books.empty() ? 0 : 1 + layout.rowCount; }

bool HomeActivity::ensureThumb(RecentBook& book, const int height, bool& showingLoading, Rect& popupRect) {
  const auto notifyLoading = [&] {
    if (showingLoading) return;
    showingLoading = true;
    popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    GUI.fillPopupProgress(renderer, popupRect, 0);
  };
  // One parser at a time, off the stack: EPUB/XTC objects exceed its budget.
  if (FsHelpers::hasReflowableBookExtension(book.path)) {
    auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("HOME", "OOM: cover EPUB");
      return false;
    }
    book.coverBmpPath = epub->getThumbBmpPath();
    if (Storage.exists(epub->getThumbBmpPath(height).c_str())) return true;
    notifyLoading();
    return epub->generateThumbBmpFromSource(height);
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("HOME", "OOM: cover XTC");
      return false;
    }
    book.coverBmpPath = xtc->getThumbBmpPath();
    if (Storage.exists(xtc->getThumbBmpPath(height).c_str())) return true;
    notifyLoading();
    return xtc->load() && xtc->generateThumbBmp(height);
  }
  return false;
}

void HomeActivity::prepareCovers(const Layout& layout) {
  // Thumbs are generated at the exact slot height: rescaling a dithered 1-bit
  // image aliases badly.
  const int heroHeight = layout.heroCover.height;
  const int rowHeight = ROW_COVER_HEIGHT;
  bool showingLoading = false;
  Rect popupRect;
  const int total = 1 + layout.rowCount;
  for (int i = 0; i < total && i < static_cast<int>(books.size()); ++i) {
    const int height = i == 0 ? heroHeight : rowHeight;
    const bool ok = ensureThumb(books[i], height, showingLoading, popupRect);
    coverPaths[i] = ok ? UITheme::getCoverThumbPath(books[i].coverBmpPath, height) : std::string();
    if (showingLoading) GUI.fillPopupProgress(renderer, popupRect, (i + 1) * 100 / total);
  }
  preparedHeroHeight = heroHeight;
  coversReady = true;
  if (coverCache) coverCache->invalidate();
}

void HomeActivity::drawCover(const Rect& rect, const size_t index) {
  if (coverCache && index < coverPaths.size()) {
    coverCache->paint(freeink::ui::Rect{static_cast<int16_t>(rect.x), static_cast<int16_t>(rect.y),
                                        static_cast<int16_t>(rect.width), static_cast<int16_t>(rect.height)},
                      index, coverPaths[index]);
    return;
  }
  GUI.drawCoverPlaceholder(renderer, rect);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);
}

void HomeActivity::drawHero(const Layout& layout) {
  statswidgets::drawTile(renderer, layout.hero);
  const Rect& cover = layout.heroCover;
  if (books.empty()) {
    // No book yet: an invitation where the cover would be.
    renderer.drawRoundedRect(cover.x, cover.y, cover.width, cover.height, 1, TILE_RADIUS, true);
    renderer.drawIcon(BookIcon, cover.x + (cover.width - EMPTY_ICON_SIZE) / 2, cover.y + cover.height / 3,
                      EMPTY_ICON_SIZE);
    const Rect text{cover.x + 6, cover.y + cover.height / 3 + EMPTY_ICON_SIZE + 10, cover.width - 12, cover.height / 2};
    UITheme::drawCenteredWrappedText(renderer, text, UI_10_FONT_ID, tr(STR_NO_OPEN_BOOK), 3, true, EpdFontFamily::BOLD,
                                     UITheme::TextVerticalAlignment::TOP);
  } else {
    drawCover(cover, 0);
    const Rect& caption = layout.heroCaption;
    char percent[8];
    snprintf(percent, sizeof(percent), "%d%%", std::max(0, progress[0]));
    const char* status = finished[0] ? tr(STR_COMPLETED) : percent;
    const int statusWidth = renderer.getTextWidth(UI_10_FONT_ID, status);
    const std::string title =
        renderer.truncatedText(UI_12_FONT_ID, books[0].title.c_str(), caption.width - statusWidth - 8);
    renderer.drawText(UI_12_FONT_ID, caption.x, caption.y, title.c_str(), true);
    renderer.drawText(UI_10_FONT_ID, caption.x + caption.width - statusWidth, caption.y + 2, status, true);
    drawProgress(renderer, caption.x, caption.y + renderer.getLineHeight(UI_12_FONT_ID) + 2, caption.width,
                 finished[0] ? 100 : progress[0]);
  }

  // Today's figures, spread over the column beside the cover.
  const Rect& column = layout.heroFigures;
  const int figureHeight = statswidgets::figureHeight(renderer);
  const int gap = std::max(4, (column.height - 4 * figureHeight) / 4);
  int y = column.y + gap / 2;
  char value[24];
  char delta[24];
  const char* dash = "-";

  const auto trendOf = [](const int32_t d) { return d > 0 ? 1 : (d < 0 ? -1 : 0); };
  if (figures.dated) {
    statswidgets::formatDuration(value, sizeof(value), figures.seconds);
    statswidgets::formatDuration(delta, sizeof(delta), static_cast<uint32_t>(std::abs(figures.secondsDelta)));
  }
  statswidgets::drawFigure(renderer, column.x, y, column.width, figures.dated ? value : dash,
                           figures.dated ? trendOf(figures.secondsDelta) : 0,
                           figures.dated && figures.secondsDelta != 0 ? delta : nullptr, tr(STR_STAT_READING_TIME));
  y += figureHeight + gap;

  if (figures.dated) {
    snprintf(value, sizeof(value), "%u", static_cast<unsigned>(figures.pages));
    snprintf(delta, sizeof(delta), "%u", static_cast<unsigned>(std::abs(figures.pagesDelta)));
  }
  statswidgets::drawFigure(renderer, column.x, y, column.width, figures.dated ? value : dash,
                           figures.dated ? trendOf(figures.pagesDelta) : 0,
                           figures.dated && figures.pagesDelta != 0 ? delta : nullptr, tr(STR_STAT_PAGES));
  y += figureHeight + gap;

  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(figures.streak));
  statswidgets::drawFigure(renderer, column.x, y, column.width, figures.dated ? value : dash, 0, nullptr,
                           tr(STR_STAT_STREAK));
  y += figureHeight + gap;

  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(figures.finished));
  snprintf(delta, sizeof(delta), "%u", static_cast<unsigned>(figures.finishedToday));
  statswidgets::drawFigure(renderer, column.x, y, column.width, value, figures.finishedToday > 0 ? 1 : 0,
                           figures.finishedToday > 0 ? delta : nullptr, tr(STR_STAT_FINISHED));
}

void HomeActivity::drawChart(const Layout& layout) const {
  const Rect& tile = layout.chart;
  statswidgets::drawTile(renderer, tile);
  const int headerHeight = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawText(UI_10_FONT_ID, tile.x + TILE_PAD, tile.y + 6, tr(STR_STATS_LAST_7_DAYS), true, EpdFontFamily::BOLD);
  const Rect chart{tile.x + TILE_PAD, tile.y + 10 + headerHeight, tile.width - 2 * TILE_PAD,
                   tile.height - 16 - headerHeight};
  if (!figures.dated) {
    // The "set the date" button (buildScreen) takes the bottom of the tile.
    const Rect message{chart.x, chart.y, chart.width, chart.height - SET_DATE_HEIGHT - 4};
    UITheme::drawCenteredWrappedText(renderer, message, UI_10_FONT_ID, tr(STR_STATS_NO_DATE), 2);
    return;
  }
  uint32_t weekSeconds = 0;
  for (const auto& day : figures.week) weekSeconds += day.seconds;
  char total[24];
  statswidgets::formatDuration(total, sizeof(total), weekSeconds);
  const int totalWidth = renderer.getTextWidth(UI_10_FONT_ID, total, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, tile.x + tile.width - TILE_PAD - totalWidth, tile.y + 6, total, true,
                    EpdFontFamily::BOLD);
  statswidgets::drawDayChart(renderer, chart, figures.week, 7);
}

void HomeActivity::drawRow(const Layout& layout, const int row) {
  const size_t index = static_cast<size_t>(row) + 1;
  const Rect& rect = layout.rows[row];
  const RecentBook& book = books[index];
  statswidgets::drawTile(renderer, rect);
  const Rect cover{rect.x + TILE_PAD / 2 + 4, rect.y + (rect.height - ROW_COVER_HEIGHT) / 2,
                   coverWidthFor(ROW_COVER_HEIGHT), ROW_COVER_HEIGHT};
  drawCover(cover, index);

  const int textX = cover.x + cover.width + TILE_PAD;
  const int textWidth = rect.x + rect.width - TILE_PAD - textX;
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int smallHeight = renderer.getLineHeight(UI_10_FONT_ID);
  int y = rect.y + TILE_PAD / 2 + 2;
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), textWidth, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, textX, y, title.c_str(), true, EpdFontFamily::BOLD);
  y += titleHeight;
  if (!book.author.empty()) {
    const std::string author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, y, author.c_str(), true);
  }
  char percent[8];
  snprintf(percent, sizeof(percent), "%d%%", std::max(0, progress[index]));
  const char* status = finished[index] ? tr(STR_COMPLETED) : percent;
  const int barY = rect.y + rect.height - TILE_PAD / 2 - PROGRESS_HEIGHT - 2;
  const int statusWidth = renderer.getTextWidth(UI_10_FONT_ID, status);
  renderer.drawText(UI_10_FONT_ID, textX + textWidth - statusWidth, barY - smallHeight - 2, status, true);
  drawProgress(renderer, textX, barY, textWidth, finished[index] ? 100 : progress[index]);
}

void HomeActivity::drawFocus(const Rect& rect) const {
  for (int i = 0; i < FOCUS_WIDTH; ++i) {
    const int grow = FOCUS_GAP + i;
    renderer.drawRoundedRect(rect.x - grow, rect.y - grow, rect.width + 2 * grow, rect.height + 2 * grow, 1,
                             TILE_RADIUS + grow, true);
  }
}

void HomeActivity::screenFn(UiScreen& screen, void* user) { static_cast<HomeActivity*>(user)->buildScreen(screen); }

void HomeActivity::buildScreen(UiScreen& screen) {
  const Layout layout = computeLayout();
  const int slots = bookSlots(layout);
  const bool buttonCursor = !mappedInput.hasTouch();
  const auto visible = appnav::visibleTabs();
  const int focusedTab = buttonCursor && selection >= slots && selection - slots < visible.count
                             ? static_cast<int>(visible.tabs[selection - slots])
                             : -1;
  appnav::buildChrome(screen, renderer, AppTab::Home, focusedTab);
  auto& frame = screen.frame();

  drawHero(layout);
  if (!books.empty()) frame.hit(toUi(layout.hero), ACTION_OPEN_BOOK, 0, fui::InputTouch);

  drawChart(layout);
  if (figures.dated) {
    frame.hit(toUi(layout.chart), ACTION_OPEN_STATS, 0, fui::InputTouch);
  } else {
    const auto& theme = screen.theme();
    fui::ButtonProps setDate;
    setDate.label = tr(STR_SET_DATE);
    setDate.action = ACTION_SET_DATE;
    setDate.inputMask = fui::InputTouch;
    setDate.text = theme.smallText;
    setDate.text.bold = true;
    setDate.text.align = fui::TextAlign::Center;
    setDate.radius = static_cast<uint8_t>(theme.controlRadius);
    setDate.minTouchSize = theme.minTouchSize;
    const int width = std::min(layout.chart.width - 2 * TILE_PAD, layout.chart.width * 3 / 5);
    fui::button(
        frame,
        toUi(Rect{layout.chart.x + (layout.chart.width - width) / 2,
                  layout.chart.y + layout.chart.height - TILE_PAD / 2 - SET_DATE_HEIGHT, width, SET_DATE_HEIGHT}),
        setDate);
  }

  for (int row = 0; row < layout.rowCount; ++row) {
    drawRow(layout, row);
    frame.hit(toUi(layout.rows[row]), ACTION_OPEN_BOOK, static_cast<int16_t>(row + 1), fui::InputTouch);
  }

  if (buttonCursor && selection < slots) drawFocus(selection == 0 ? layout.hero : layout.rows[selection - 1]);
}

void HomeActivity::render(RenderLock&&) {
  const Layout layout = computeLayout();
  if (firstRenderDone && !coversReady && !books.empty()) prepareCovers(layout);
  if (coversReady && (preparedHeroHeight != layout.heroCover.height)) prepareCovers(layout);
  if (coverCache) coverCache->prepare();

  renderer.clearScreen();
  renderUi();
  const auto labels =
      mappedInput.mapLabels(books.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  if (!firstRenderDone) {
    // The first frame goes up before any thumbnail is generated; the next
    // pass prepares the covers and repaints.
    firstRenderDone = true;
    if (!books.empty()) requestUpdate();
  }
}

void HomeActivity::onOpenBook(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<HomeActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->openBook(static_cast<size_t>(event.value));
}

void HomeActivity::onOpenStats(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<HomeActivity*>(user);
  self->app.clearTapFlash();
  appnav::openTab(AppTab::Stats);
}

void HomeActivity::onSetDate(const fui::ActionEvent&, void* user) {
  static_cast<HomeActivity*>(user)->openDateSettings();
}

void HomeActivity::openDateSettings() {
  app.clearTapFlash();
  auto activity = makeUniqueNoThrow<DateTimeSettingsActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("HOME", "OOM: DateTimeSettingsActivity");
    return;
  }
  // Back from the date screen: today's figures may now have a day.
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    {
      // Result handlers run unlocked while the render task reads `figures`.
      RenderLock lock(*this);
      loadFigures();
    }
    requestUpdate();
  });
}

void HomeActivity::openBook(const size_t index) {
  if (index < books.size()) activityManager.goToReader(books[index].path);
}

void HomeActivity::activate(const int target, const Layout& layout) {
  const int slots = bookSlots(layout);
  if (target < slots) {
    openBook(static_cast<size_t>(target));
    return;
  }
  const auto visible = appnav::visibleTabs();
  const int index = target - slots;
  if (index >= visible.count) return;
  if (visible.tabs[index] != AppTab::Home) appnav::openTab(visible.tabs[index]);
}

void HomeActivity::loop() {
  const Layout layout = computeLayout();
  const int slots = bookSlots(layout);

  // Touch goes through the app: tabs, book tiles, the chart and the date link.
  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  // Back resumes the most recent book, as it always has on Home.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && !books.empty()) {
    openBook(0);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(selection, layout);
    return;
  }

  // Side buttons walk the books, front Left/Right the tabs; a press while the
  // cursor sits in the other band jumps into this one.
  const auto cycleBand = [this](const int base, const int count, const int dir) {
    if (count <= 0) return;
    int idx = selection - base;
    idx = (idx < 0 || idx >= count) ? (dir > 0 ? 0 : count - 1) : (idx + count + dir) % count;
    selection = base + idx;
    requestUpdate();
  };
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up},
                                       [&cycleBand, slots] { cycleBand(0, slots, -1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [&cycleBand, slots] { cycleBand(0, slots, +1); });
  const int tabCount = appnav::visibleTabs().count;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                       [&cycleBand, slots, tabCount] { cycleBand(slots, tabCount, -1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                       [&cycleBand, slots, tabCount] { cycleBand(slots, tabCount, +1); });
}
