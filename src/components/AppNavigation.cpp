#include "AppNavigation.h"

#include <BoardConfig.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <components/bars/tab-bar.h>
#include <components/controls/button.h>

#include <algorithm>
#include <cstdint>

#include "OpdsServerStore.h"
#include "UITheme.h"
#include "activities/Activity.h"
#include "activities/plugins/PluginCatalogActivity.h"  // anyPluginInstalled()
#include "icons/appIcons.h"

namespace fui = freeink::ui;

namespace appnav {
namespace {
// Matches the control-center tap band ActivityManager has always used.
constexpr int STATUS_BAND_BOTTOM = 44;
constexpr int TOUCH_TAB_BAR_HEIGHT = 64;
constexpr int BUTTON_TAB_BAR_HEIGHT = 56;
constexpr int16_t TAB_ICON_SIZE = 32;
constexpr int16_t SELECTED_UNDERLINE = 3;
constexpr int16_t FOCUS_PAD = 8;
constexpr uint8_t FOCUS_RADIUS = 10;

constexpr int TOUCH_SUB_BAR_HEIGHT = 52;
constexpr int BUTTON_SUB_BAR_HEIGHT = 44;
constexpr int16_t SUB_PILL_HEIGHT = 36;
constexpr int16_t SUB_PILL_PAD_X = 16;
constexpr int16_t SUB_PILL_GAP = 8;
constexpr uint8_t SUB_PILL_RADIUS = 18;
constexpr int16_t SUB_TRAILING_SIZE = 44;

// Refreshed on entry to the library screens (refreshLibraryCatalogEntry):
// the plugin check scans the SD card, too slow for every frame. Written on
// the main task before the screen exists, then only read.
struct CatalogEntry {
  bool opds = false;
  bool plugins = false;
};
CatalogEntry catalogEntry;

const freeink::Icon& tabIcon(const int tab) {
  static const freeink::Icon* const ICONS[TAB_COUNT] = {&icon_app_home_32,  &icon_app_library_32,
                                                        &icon_app_games_32, &icon_app_transfer_32,
                                                        &icon_app_stats_32, &icon_app_settings_32};
  return *ICONS[tab];
}

int tabBarHeight() { return BoardConfig::hasTouch() ? TOUCH_TAB_BAR_HEIGHT : BUTTON_TAB_BAR_HEIGHT; }

int16_t sideInset(const GfxRenderer& renderer) {
  const Rect content = UITheme::getContentArea(renderer);
  return static_cast<int16_t>(content.x + UITheme::getInstance().getMetrics().contentSidePadding / 2);
}

fui::StyleSet plainTabStyles() {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::White);
  styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  return styles;
}

// The button cursor: an inverted shape behind the focused element.
fui::StyleSet cursorStyles(const uint8_t radius) {
  fui::StyleSet styles;
  styles.explicitlySet = true;
  styles.normal.background = fui::Paint::solid(fui::Color::Black);
  styles.normal.foreground = fui::Paint::solid(fui::Color::White);
  styles.normal.radius = radius;
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  return styles;
}

void onTabAction(const fui::ActionEvent& event, void* user) {
  const auto active = static_cast<AppTab>(reinterpret_cast<uintptr_t>(user));
  if (event.value < 0 || event.value >= TAB_COUNT || event.value == static_cast<int>(active)) return;
  openTab(static_cast<AppTab>(event.value));
}
}  // namespace

TabList visibleTabs() {
  TabList list;
  for (int i = 0; i < TAB_COUNT; ++i) {
    const auto tab = static_cast<AppTab>(i);
#if CROSSPOINT_GAMES
    if (tab == AppTab::Games && !BoardConfig::hasTouch()) continue;
#else
    if (tab == AppTab::Games) continue;
#endif
    list.tabs[list.count++] = tab;
  }
  return list;
}

int statusBandBottom() { return STATUS_BAND_BOTTOM; }

int contentTop(const GfxRenderer&) { return STATUS_BAND_BOTTOM + tabBarHeight(); }

int contentBottom(const GfxRenderer& renderer) {
  return renderer.getScreenHeight() -
         (BoardConfig::hasTouch() ? 0 : UITheme::getInstance().getMetrics().buttonHintsHeight);
}

void paintIcon(fui::DrawTarget& target, const freeink::Icon& icon, const int x, const int y, const bool black) {
  target.bitmap(fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(icon.w),
                          static_cast<int16_t>(icon.h)},
                fui::bitmapFromIcon(icon), fui::BitmapMode::Center,
                fui::Paint::solid(black ? fui::Color::Black : fui::Color::White));
}

void buildChrome(UiScreen& screen, const GfxRenderer& renderer, const AppTab active, const int focusedTab) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  // Clock and battery only: root screens own their stack bottom, so no back
  // button (this also retires any back target a previous screen recorded).
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, STATUS_BAND_BOTTOM - metrics.topPadding}, nullptr,
                 nullptr, false);

  const TabList visible = visibleTabs();
  fui::TabItem tabs[TAB_COUNT];
  for (int i = 0; i < visible.count; ++i) {
    const int tab = static_cast<int>(visible.tabs[i]);
    tabs[i].value = static_cast<int16_t>(tab);
    tabs[i].selected = tab == static_cast<int>(active);
    tabs[i].icon = fui::bitmapFromIcon(tabIcon(tab));
  }
  fui::TabBarProps props;
  props.tabs = tabs;
  props.count = static_cast<uint16_t>(visible.count);
  props.action = ACTION_APP_TAB;
  props.inputMask = fui::InputTouch;  // buttons walk the bar from Home's loop
  props.iconSize = TAB_ICON_SIZE;
  props.tabStyles = plainTabStyles();
  props.selectedUnderline = SELECTED_UNDERLINE;
  props.selectedUnderlinePaint = fui::Paint::solid(fui::Color::Black);
  props.tabInset = fui::Insets{6, 4, 4, 4};
  props.contentInset = fui::Insets{0, FOCUS_PAD, 0, FOCUS_PAD};
  // The button cursor is an inverted pill behind the focused icon.
  props.iconPainterUserData = const_cast<int*>(&focusedTab);
  props.iconPainter = [](fui::DrawTarget& target, const fui::Rect rect, const fui::TabItem& tab, uint8_t, void* user) {
    if (tab.value != *static_cast<const int*>(user)) return false;
    target.fill(
        fui::Rect{static_cast<int16_t>(rect.x - FOCUS_PAD), static_cast<int16_t>(rect.y - FOCUS_PAD),
                  static_cast<int16_t>(rect.width + 2 * FOCUS_PAD), static_cast<int16_t>(rect.height + 2 * FOCUS_PAD)},
        fui::Paint::solid(fui::Color::Black), FOCUS_RADIUS);
    target.bitmap(rect, tab.icon, fui::BitmapMode::Center, fui::Paint::solid(fui::Color::White));
    return true;
  };
  props.divider = false;

  const int16_t inset = sideInset(renderer);
  const fui::Rect bar{inset, static_cast<int16_t>(STATUS_BAND_BOTTOM), static_cast<int16_t>(width - 2 * inset),
                      static_cast<int16_t>(tabBarHeight() - 1)};
  fui::tabBar(screen.frame(), bar, props);
  screen.target().fill(fui::Rect{0, static_cast<int16_t>(contentTop(renderer) - 1), static_cast<int16_t>(width), 1},
                       fui::Paint::solid(fui::Color::Black));
}

void registerTabs(UiApp& app, const AppTab active) {
  app.on(ACTION_APP_TAB, &onTabAction, reinterpret_cast<void*>(static_cast<uintptr_t>(active)));
}

void openTab(const AppTab tab) {
  switch (tab) {
    case AppTab::Home:
      activityManager.goHome();
      break;
    case AppTab::Library:
      activityManager.goToLibrary();
      break;
    case AppTab::Games:
#if CROSSPOINT_GAMES
      activityManager.goToGames();
#endif
      break;
    case AppTab::Transfer:
      activityManager.goToFileTransfer();
      break;
    case AppTab::Stats:
      activityManager.goToStats();
      break;
    case AppTab::Settings:
      activityManager.goToSettings();
      break;
  }
}

int subBarHeight() { return BoardConfig::hasTouch() ? TOUCH_SUB_BAR_HEIGHT : BUTTON_SUB_BAR_HEIGHT; }

int buildSubBar(UiScreen& screen, const GfxRenderer& renderer, const int top, const SubBarSpec& spec) {
  const int width = renderer.getScreenWidth();
  const int height = subBarHeight();
  const int16_t inset = sideInset(renderer);
  const auto& theme = screen.theme();
  const int count = std::min(spec.count, SubBarSpec::MAX_ITEMS);

  // Trailing icon buttons, right-aligned. The button cursor inverts the
  // focused one, as on the app tab bar.
  int16_t right = static_cast<int16_t>(width - inset);
  const int trailingCount = std::min(spec.trailingCount, SubBarSpec::MAX_TRAILING);
  for (int i = trailingCount - 1; i >= 0; --i) {
    right = static_cast<int16_t>(right - SUB_TRAILING_SIZE);
    if (!spec.trailing[i]) continue;
    fui::ButtonProps button;
    button.icon = fui::bitmapFromIcon(*spec.trailing[i]);
    button.action = ACTION_APP_SUBBAR;
    button.value = static_cast<int16_t>(spec.count + i);
    button.inputMask = fui::InputTouch;
    button.styles = spec.focused == count + i ? cursorStyles(FOCUS_RADIUS) : plainTabStyles();
    button.minTouchSize = theme.minTouchSize;
    fui::button(screen.frame(),
                fui::Rect{right, static_cast<int16_t>(top + (height - SUB_TRAILING_SIZE) / 2), SUB_TRAILING_SIZE,
                          SUB_TRAILING_SIZE},
                button);
  }

  // Text pills, content-width from the left; equal slots when the labels do
  // not fit. Each pill is its own one-tab bar so the cursor can style it.
  const int16_t barWidth = static_cast<int16_t>(right - inset - SUB_PILL_GAP);
  int16_t widths[SubBarSpec::MAX_ITEMS] = {};
  int total = count > 0 ? (count - 1) * SUB_PILL_GAP : 0;
  for (int i = 0; i < count; ++i) {
    widths[i] = static_cast<int16_t>(
        screen.target().measureText(theme.bodyText.font, spec.labels[i], theme.bodyText).width + 2 * SUB_PILL_PAD_X);
    total += widths[i];
  }
  if (count > 0 && total > barWidth) {
    for (int i = 0; i < count; ++i) widths[i] = static_cast<int16_t>((barWidth - (count - 1) * SUB_PILL_GAP) / count);
  }
  fui::StyleSet pillStyles;
  pillStyles.explicitlySet = true;
  pillStyles.normal.background = fui::Paint::solid(fui::Color::White);
  pillStyles.normal.foreground = fui::Paint::solid(fui::Color::Black);
  pillStyles.normal.border = fui::Paint::solid(fui::Color::Black);
  pillStyles.normal.borderWidth = 1;
  pillStyles.normal.radius = SUB_PILL_RADIUS;
  pillStyles.selected = pillStyles.normal;
  pillStyles.selected.background = fui::Paint::dither(fui::Color::LightGray);
  pillStyles.focused = pillStyles.selected;
  pillStyles.active = pillStyles.selected;
  const fui::StyleSet pillCursorStyles = cursorStyles(SUB_PILL_RADIUS);

  int16_t x = inset;
  for (int i = 0; i < count; ++i) {
    fui::TabItem pill;
    pill.label = spec.labels[i];
    pill.value = static_cast<int16_t>(i);
    // The cursor wins over the current-section shade: it is drawn as selected.
    pill.selected = i == spec.selected || i == spec.focused;
    fui::TabBarProps props;
    props.tabs = &pill;
    props.count = 1;
    props.action = ACTION_APP_SUBBAR;
    props.inputMask = fui::InputTouch;
    props.text = theme.bodyText;
    props.tabInset = fui::Insets{static_cast<int16_t>((height - SUB_PILL_HEIGHT) / 2), 0,
                                 static_cast<int16_t>((height - SUB_PILL_HEIGHT) / 2), 0};
    props.contentInset = fui::Insets{0, SUB_PILL_PAD_X, 0, SUB_PILL_PAD_X};
    props.tabStyles = i == spec.focused ? pillCursorStyles : pillStyles;
    props.divider = false;
    fui::tabBar(screen.frame(), fui::Rect{x, static_cast<int16_t>(top), widths[i], static_cast<int16_t>(height - 1)},
                props);
    x = static_cast<int16_t>(x + widths[i] + SUB_PILL_GAP);
  }
  screen.target().fill(fui::Rect{0, static_cast<int16_t>(top + height - 1), static_cast<int16_t>(width), 1},
                       fui::Paint::solid(fui::Color::Black));
  return top + height;
}

void refreshLibraryCatalogEntry() { catalogEntry = CatalogEntry{OPDS_STORE.hasServers(), anyPluginInstalled()}; }

int librarySectionCount() { return catalogEntry.plugins || catalogEntry.opds ? 3 : 2; }

int buildLibrarySubBar(UiScreen& screen, const GfxRenderer& renderer, const int top, const LibrarySection selected,
                       const freeink::Icon* const* trailing, const int trailingCount, const int focused) {
  SubBarSpec spec;
  spec.labels[0] = tr(STR_LIBRARY);
  spec.labels[1] = tr(STR_FILES);
  // The former home "library slot": Plugins (whose catalog lists the OPDS
  // servers too) when installed, else the OPDS catalogs.
  spec.labels[2] = catalogEntry.plugins ? tr(STR_PLUGINS) : tr(STR_CATALOGS);
  spec.count = librarySectionCount();
  spec.selected = static_cast<int>(selected);
  spec.trailingCount = std::min(trailingCount, SubBarSpec::MAX_TRAILING);
  for (int i = 0; trailing && i < spec.trailingCount; ++i) spec.trailing[i] = trailing[i];
  spec.focused = focused;
  return buildSubBar(screen, renderer, top, spec);
}

namespace {
// Section a button switch asked the next library screen to focus.
int pendingSubBarFocus = -1;

int activateLibrarySubBarItem(const int value, const LibrarySection current) {
  const int sections = librarySectionCount();
  if (value >= sections) return value - sections;
  if (value == static_cast<int>(current)) return -1;
  switch (static_cast<LibrarySection>(value)) {
    case LibrarySection::Books:
      activityManager.goToLibrary();
      break;
    case LibrarySection::Files:
      activityManager.goToFileBrowser();
      break;
    case LibrarySection::Catalogs:
      if (catalogEntry.plugins) {
        activityManager.goToPlugins(catalogEntry.opds);
      } else {
        activityManager.goToBrowser();
      }
      break;
  }
  return -1;
}
}  // namespace

int handleLibrarySubBarAction(const fui::ActionEvent& event, const LibrarySection current) {
  return activateLibrarySubBarItem(event.value, current);
}

LibrarySubBarInput handleLibrarySubBarButtons(MappedInputManager& input, int& cursor, const LibrarySection current,
                                              const int trailingCount) {
  using Button = MappedInputManager::Button;
  LibrarySubBarInput result;
  const int last = librarySectionCount() + trailingCount - 1;
  if (input.wasPressed(Button::NavPrevious)) {
    if (cursor > 0) --cursor;
    result.consumed = true;
  } else if (input.wasPressed(Button::NavNext)) {
    if (cursor < last) {
      ++cursor;
    } else {
      cursor = -1;
      result.left = true;
    }
    result.consumed = true;
  } else if (input.wasReleased(Button::Back)) {
    cursor = -1;
    result.left = true;
    result.consumed = true;
  } else if (input.wasReleased(Button::Confirm)) {
    const int value = cursor;
    // The Catalogs screens have no sub-bar to land on.
    if (value < librarySectionCount() && value != static_cast<int>(current) &&
        static_cast<LibrarySection>(value) != LibrarySection::Catalogs) {
      pendingSubBarFocus = value;
    }
    result.trailing = activateLibrarySubBarItem(value, current);
    result.consumed = true;
  } else if (input.wasPressed(Button::Confirm) || input.wasPressed(Button::Back)) {
    result.consumed = true;  // its release acts above
  }
  return result;
}

int takeLibrarySubBarFocus(const LibrarySection section) {
  const int focus = pendingSubBarFocus == static_cast<int>(section) ? pendingSubBarFocus : -1;
  pendingSubBarFocus = -1;
  return focus;
}

}  // namespace appnav
