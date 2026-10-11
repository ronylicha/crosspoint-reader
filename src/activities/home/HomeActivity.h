#pragma once
#include <ReadingLog.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "components/AppNavigation.h"
#include "components/HomeCoverCache.h"
#include "components/UiAppHost.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

// Home tab: the current book beside today's reading figures, a seven-day
// chart and the next recent books, under the app tab bar. Sections float on
// the page, separated by whitespace and dotted rules rather than frames.
//
// Built with FreeInkUI: the tab bar, books, figures, chart and "set the date"
// link register their taps in the app's interaction table.
// Buttons: side Up/Down walk the books, front Left/Right walk the tab bar,
// Confirm opens, Back resumes the last book.
class HomeActivity final : public Activity, private UiAppHost {
 public:
  static constexpr int MAX_ROWS = 3;
  static constexpr int MAX_BOOKS = 1 + MAX_ROWS;

  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, AppTab focusTab = AppTab::Home,
                        bool cleanInitialRefresh = false)
      : Activity("Home", renderer, mappedInput),
        UiAppHost(renderer),
        focusTab(focusTab),
        cleanInitialRefresh(cleanInitialRefresh) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  // Borderless sections: whitespace and dotted rules separate them.
  struct Layout {
    Rect heroBook;  // cover and caption: the current book's tap target
    Rect heroCover;
    Rect heroCaption;  // title and progress under the cover
    Rect heroFigures;  // today's figures beside the cover: opens Stats
    Rect chart;
    std::array<Rect, MAX_ROWS> rows;
    int rowCount = 0;
    std::array<Rect, 2> rules;
    int ruleCount = 0;
  };
  // Today's figures, snapshotted on enter so render never touches the store.
  struct TodayFigures {
    bool dated = false;
    uint32_t seconds = 0;
    int32_t secondsDelta = 0;
    uint32_t pages = 0;
    int32_t pagesDelta = 0;
    uint32_t streak = 0;
    uint32_t finished = 0;
    uint32_t finishedToday = 0;
    readingstats::DayTotals week[7]{};
  };

  // Same geometry for render (drawing) and loop (hit tests).
  Layout computeLayout() const;
  int bookSlots(const Layout& layout) const;

  void loadBooks();
  void loadFigures();
  void prepareCovers(const Layout& layout);
  bool ensureThumb(RecentBook& book, int height, bool& showingLoading, Rect& popupRect);

  // Hero and rows paint covers through the cache, hence non-const.
  void drawHero(const Layout& layout);
  void drawChart(const Layout& layout) const;
  void drawRow(const Layout& layout, int row);
  void drawFocus(const Rect& rect) const;
  void drawCover(const Rect& rect, size_t index);

  static void screenFn(UiScreen& screen, void* user);
  void buildScreen(UiScreen& screen);
  static void onOpenBook(const freeink::ui::ActionEvent& event, void* user);
  static void onOpenStats(const freeink::ui::ActionEvent& event, void* user);
  static void onSetDate(const freeink::ui::ActionEvent& event, void* user);
  void openDateSettings();

  void activate(int selection, const Layout& layout);
  void openBook(size_t index);

  std::unique_ptr<HomeCoverCache> coverCache;
  ButtonNavigator buttonNavigator;
  std::vector<RecentBook> books;
  std::array<int, MAX_BOOKS> progress{};
  std::array<bool, MAX_BOOKS> finished{};
  std::array<std::string, MAX_BOOKS> coverPaths;
  TodayFigures figures;
  int selection = 0;
  int preparedHeroHeight = 0;
  bool coversReady = false;
  bool firstRenderDone = false;
  const AppTab focusTab;
  const bool cleanInitialRefresh;
};
