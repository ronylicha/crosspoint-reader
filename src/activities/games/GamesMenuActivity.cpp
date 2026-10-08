#include "GamesMenuActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include "BackgammonActivity.h"
#include "CheckersActivity.h"
#include "ChessActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void GamesMenuActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

std::string GamesMenuActivity::labelFor(const int index) const {
  char buf[64];
  const char* game = (index < 2) ? tr(STR_CHESS) : (index < 4) ? tr(STR_CHECKERS) : tr(STR_BACKGAMMON);
  const char* mode = (index % 2 == 0) ? tr(STR_TWO_PLAYERS) : tr(STR_VS_AI);
  snprintf(buf, sizeof(buf), "%s - %s", game, mode);
  return std::string(buf);
}

void GamesMenuActivity::launchSelected() const {
  const bool vsAi = (selectorIndex % 2) == 1;
  if (selectorIndex < 2) {
    activityManager.pushActivity(std::make_unique<ChessActivity>(renderer, mappedInput, vsAi));
  } else if (selectorIndex < 4) {
    activityManager.pushActivity(std::make_unique<CheckersActivity>(renderer, mappedInput, vsAi));
  } else {
    activityManager.pushActivity(std::make_unique<BackgammonActivity>(renderer, mappedInput, vsAi));
  }
}

void GamesMenuActivity::loop() {
  buttonNavigator.onNext([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, ITEM_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    launchSelected();
    return;
  }

  // Touch: tap a row to select it, tap the selected row again to launch.
  if (menuRowHeight > 0) {
    int row = -1;
    const auto touch = mappedInput.rowTouch(row, menuTop, menuRowHeight, ITEM_COUNT);
    if (touch != MappedInputManager::RowTouch::None && row >= 0 && row < ITEM_COUNT) {
      if (touch == MappedInputManager::RowTouch::Tap) {
        if (row == selectorIndex) {
          launchSelected();
          return;
        }
        selectorIndex = row;
        requestUpdate();
      } else if (row != selectorIndex) {
        selectorIndex = row;
        requestUpdate();
      }
    }
  }
}

void GamesMenuActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getInstance().getContentArea(renderer);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight - metrics.topPadding},
                 tr(STR_GAMES), nullptr, true);

  const int menuY = metrics.headerHeight + metrics.verticalSpacing;
  const int menuH = pageHeight - menuY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawButtonMenu(renderer, Rect{content.x, menuY, content.width, menuH}, ITEM_COUNT, selectorIndex,
                     [this](int index) { return labelFor(index); },
                     [](int) { return UIIcon::Blocks; });

  // Remember the touch geometry of the rows as drawn by the theme.
  menuTop = menuY;
  menuRowHeight = GUI.getMenuRowHeight(renderer);

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
