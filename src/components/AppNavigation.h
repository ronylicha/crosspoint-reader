#pragma once

#include <Icon.h>

#include <cstdint>

#include "MappedInputManager.h"
#include "activities/AppTab.h"
#include "components/UiAppHost.h"
#include "components/themes/BaseTheme.h"

class GfxRenderer;

// The app's top-level navigation, built with FreeInkUI: a status band (clock,
// battery) above an icon tab bar shared by every root screen (Home,
// Library/Files, Games, Transfer, Statistics, Settings). Root screens host a
// FreeInkApp (UiAppHost): they call buildChrome() first in their screen
// function and registerTabs() once in onEnter; tab taps then route through
// the app's interaction table like any other element.
//
// Button boards reach the tabs from Home (front Left/Right walk the bar, see
// HomeActivity); every other root screen returns Home with Back.
namespace appnav {

using UiApp = UiAppHost::UiApp;
using UiScreen = UiAppHost::UiScreen;

// Upper bound of the bar; boards without games show one tab fewer.
constexpr int TAB_COUNT = 6;
struct TabList {
  AppTab tabs[TAB_COUNT];
  int count = 0;
};
// The tabs this board shows, in bar order: Games only exists in touch
// firmware (games/GamesConfig.h) running on a touch board.
TabList visibleTabs();
// High ids so they never collide with a screen's own actions.
constexpr freeink::ui::ActionId ACTION_APP_TAB = 0x7F00;
constexpr freeink::ui::ActionId ACTION_APP_SUBBAR = 0x7F01;

// Bottom edge of the status band. Taps above it open the control center
// (ActivityManager), so the tab bar starts below.
int statusBandBottom();
// First content row below the tab bar.
int contentTop(const GfxRenderer& renderer);
// Bottom edge reserved for button hints (screen bottom on touch boards).
int contentBottom(const GfxRenderer& renderer);

// Status band + tab bar at the top of the screen. focusedTab (an AppTab value,
// -1 for none) marks the button cursor (Home only). The caller then sets its content margin from
// contentTop().
void buildChrome(UiScreen& screen, const GfxRenderer& renderer, AppTab active, int focusedTab = -1);
// Routes ACTION_APP_TAB taps of this app: tapping a tab other than `active`
// opens its root screen.
void registerTabs(UiApp& app, AppTab active);
// Replaces the current screen with the tab's root screen.
void openTab(AppTab tab);

// Paints a 1-bit SDK icon (freeink::Icon) through FreeInkUI.
void paintIcon(freeink::ui::DrawTarget& target, const freeink::Icon& icon, int x, int y, bool black = true);

// Second-level bar of text pills below the tab bar, with optional trailing
// icon buttons. Pills report value 0..count-1 under ACTION_APP_SUBBAR,
// trailing buttons count.. in order. Returns the bar's bottom edge.
struct SubBarSpec {
  static constexpr int MAX_ITEMS = 4;
  static constexpr int MAX_TRAILING = 2;
  const char* labels[MAX_ITEMS]{};
  int count = 0;
  int selected = -1;
  // Button cursor: an index over the pills then the trailing buttons, -1 for
  // none. The focused element is drawn inverted.
  int focused = -1;
  const freeink::Icon* trailing[MAX_TRAILING]{};
  int trailingCount = 0;
};
int subBarHeight();
int buildSubBar(UiScreen& screen, const GfxRenderer& renderer, int top, const SubBarSpec& spec);

// Library | Files [| Catalogs] sub-bar shared by the library screens. The
// third pill opens the plugin catalog when a plugin is installed, else the
// OPDS browser once a server is configured, and is hidden otherwise.
enum class LibrarySection : uint8_t { Books, Files, Catalogs };
// Re-reads which third pill to show; call when a library screen is entered.
void refreshLibraryCatalogEntry();
int librarySectionCount();
int buildLibrarySubBar(UiScreen& screen, const GfxRenderer& renderer, int top, LibrarySection selected,
                       const freeink::Icon* const* trailing = nullptr, int trailingCount = 0, int focused = -1);
// Handles an ACTION_APP_SUBBAR event of the library sub-bar: switches section
// for a pill tap and returns -1; for a trailing button returns its index.
int handleLibrarySubBarAction(const freeink::ui::ActionEvent& event, LibrarySection current);

// Button boards reach the library sub-bar with Previous from the top of the
// screen. The cursor walks the pills then the trailing buttons; Next past the
// last item and Back hand the focus back to the screen.
struct LibrarySubBarInput {
  bool consumed = false;  // the input belonged to the sub-bar
  bool left = false;      // the cursor left the sub-bar
  int trailing = -1;      // a trailing button to activate, -1 for none
};
// Call only while `cursor` >= 0. A switched section opens with its cursor
// on its own pill (see takeLibrarySubBarFocus).
LibrarySubBarInput handleLibrarySubBarButtons(MappedInputManager& input, int& cursor, LibrarySection current,
                                              int trailingCount);
// Cursor a library screen starts with: its own pill after a button switch,
// -1 otherwise. Consumes the request.
int takeLibrarySubBarFocus(LibrarySection section);

}  // namespace appnav
