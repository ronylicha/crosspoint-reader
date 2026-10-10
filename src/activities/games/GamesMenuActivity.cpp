#include "GamesConfig.h"

#if CROSSPOINT_GAMES

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>

#include "BackgammonActivity.h"
#include "CheckersActivity.h"
#include "ChessActivity.h"
#include "GamesMenuActivity.h"
#include "MappedInputManager.h"
#include "components/AppNavigation.h"
#include "components/UITheme.h"
#include "components/icons/appIcons.h"
#include "components/icons/gameCustomIcons.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_PLAY = 1;  // value = game * MODE_COUNT + mode

constexpr int16_t ICON_SIZE = 48;

const char* gameTitle(const int game) {
  switch (game) {
    case 0:
      return tr(STR_CHESS);
    case 1:
      return tr(STR_CHECKERS);
    default:
      return tr(STR_BACKGAMMON);
  }
}

const char* gameDescription(const int game) {
  switch (game) {
    case 0:
      return tr(STR_GAME_CHESS_DESC);
    case 1:
      return tr(STR_GAME_CHECKERS_DESC);
    default:
      return tr(STR_GAME_BACKGAMMON_DESC);
  }
}

const freeink::Icon& gameIcon(const int game) {
  switch (game) {
    case 0:
      return icon_game_chess_48;
    case 1:
      return icon_game_checkers_48;
    default:
      return icon_game_backgammon_48;
  }
}

const char* modeLabel(const int mode) { return mode == 0 ? tr(STR_TWO_PLAYERS) : tr(STR_VS_AI); }

int16_t clampToInt16(const int value) { return static_cast<int16_t>(std::max(0, value)); }
}  // namespace

void GamesMenuActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  appnav::registerTabs(app, AppTab::Games);
  app.on(ACTION_PLAY, &GamesMenuActivity::onPlayEvent, this);
  app.setScreen(&GamesMenuActivity::menuScreen, this);
  requestUpdate();
}

void GamesMenuActivity::onPlayEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<GamesMenuActivity*>(user);
  if (event.value < 0 || event.value >= ITEM_COUNT) return;
  self->selectorIndex = event.value;
  self->app.clearTapFlash();  // the tap leaves this screen
  self->launchSelected();
}

void GamesMenuActivity::launchSelected() const {
  const int game = selectorIndex / MODE_COUNT;
  const bool vsAi = (selectorIndex % MODE_COUNT) == 1;
  if (game == 0) {
    activityManager.pushActivity(std::make_unique<ChessActivity>(renderer, mappedInput, vsAi));
  } else if (game == 1) {
    activityManager.pushActivity(std::make_unique<CheckersActivity>(renderer, mappedInput, vsAi));
  } else {
    activityManager.pushActivity(std::make_unique<BackgammonActivity>(renderer, mappedInput, vsAi));
  }
}

void GamesMenuActivity::loop() {
  // Tab bar and mode buttons route through the app's interaction table.
  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;

  buttonNavigator.onNext([this] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, ITEM_COUNT);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(AppTab::Games);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    launchSelected();
    return;
  }
}

void GamesMenuActivity::menuScreen(UiScreen& screen, void* user) {
  static_cast<GamesMenuActivity*>(user)->buildMenuScreen(screen);
}

void GamesMenuActivity::buildMenuScreen(UiScreen& screen) {
  appnav::buildChrome(screen, renderer, AppTab::Games);
  const auto& theme = screen.theme();
  screen.setContentMarginFromScreen(
      fui::Insets{clampToInt16(appnav::contentTop(renderer)), 0,
                  clampToInt16(renderer.getScreenHeight() - appnav::contentBottom(renderer)), 0});
  screen.insetContent(fui::Insets{theme.spaceLg, theme.spaceMd, theme.spaceLg, theme.spaceMd});

  // Cards share the band below the tab bar equally.
  const fui::Rect area = screen.contentRect();
  const int16_t gap = theme.spaceLg;
  const int16_t cardHeight = clampToInt16((area.height - (GAME_COUNT - 1) * gap) / GAME_COUNT);
  for (int game = 0; game < GAME_COUNT; ++game) {
    const fui::Rect card{area.x, static_cast<int16_t>(area.y + game * (cardHeight + gap)), area.width, cardHeight};
    buildCard(screen, card, game);
  }
}

void GamesMenuActivity::buildCard(UiScreen& screen, const fui::Rect& card, const int game) {
  if (card.empty()) return;
  const auto& theme = screen.theme();
  auto& target = screen.target();
  target.stroke(card, fui::Paint::solid(fui::Color::Black), 1, theme.controlRadius);

  fui::TextStyle titleStyle = theme.bodyText;
  titleStyle.bold = true;
  const fui::TextStyle& descStyle = theme.smallText;
  fui::TextStyle buttonText = theme.smallText;
  buttonText.bold = true;
  buttonText.align = fui::TextAlign::Center;

  const int16_t pad = theme.spaceLg;
  const int16_t titleHeight = target.lineHeight(titleStyle.font);
  const int16_t descHeight = target.lineHeight(descStyle.font);
  const int16_t textHeight = static_cast<int16_t>(titleHeight + theme.spaceSm + descHeight);
  const int16_t headerHeight = std::max(ICON_SIZE, textHeight);
  const int16_t buttonHeight =
      static_cast<int16_t>(std::max<int>(theme.minTouchSize, target.lineHeight(buttonText.font) + 2 * theme.spaceMd));

  // Equal-width buttons sized to the longer label, shrunk to fit narrow cards.
  const int innerWidth = card.width - 2 * pad;
  int labelWidth = 0;
  for (int mode = 0; mode < MODE_COUNT; ++mode) {
    labelWidth = std::max<int>(labelWidth, target.measureText(buttonText.font, modeLabel(mode), buttonText).width);
  }
  const int16_t buttonWidth =
      clampToInt16(std::min(labelWidth + 2 * pad, (innerWidth - (MODE_COUNT - 1) * theme.spaceMd) / MODE_COUNT));
  const int buttonsWidth = MODE_COUNT * buttonWidth + (MODE_COUNT - 1) * theme.spaceMd;

  const int16_t iconX = static_cast<int16_t>(card.x + pad);
  const int16_t textX = static_cast<int16_t>(iconX + ICON_SIZE + pad);
  int textRight = card.right() - pad;
  int16_t rowHeight = buttonHeight;
  int iconY = 0;
  int titleY = 0;
  int buttonY = 0;
  const int stackedHeight = headerHeight + theme.spaceMd + buttonHeight;
  if (card.height >= stackedHeight + 2 * pad) {
    // Icon and text on top, buttons bottom right; the block is centred in the card.
    const int blockTop = card.y + (card.height - stackedHeight) / 2;
    iconY = blockTop + (headerHeight - ICON_SIZE) / 2;
    titleY = blockTop + (headerHeight - textHeight) / 2;
    buttonY = blockTop + headerHeight + theme.spaceMd;
  } else {
    // Short cards (landscape): a single row with the buttons on the right.
    rowHeight = clampToInt16(std::min<int>(buttonHeight, card.height - 2 * theme.spaceMd));
    buttonY = card.y + (card.height - rowHeight) / 2;
    iconY = card.y + (card.height - ICON_SIZE) / 2;
    titleY = card.y + (card.height - textHeight) / 2;
    textRight -= buttonsWidth + pad;
  }

  target.bitmap(fui::Rect{iconX, static_cast<int16_t>(iconY), ICON_SIZE, ICON_SIZE},
                fui::bitmapFromIcon(gameIcon(game)), fui::BitmapMode::Center);

  const int16_t textWidth = clampToInt16(textRight - textX);
  if (textWidth > 0) {
    target.text(fui::Rect{textX, static_cast<int16_t>(titleY), textWidth, titleHeight}, gameTitle(game), titleStyle);
    target.text(fui::Rect{textX, static_cast<int16_t>(titleY + titleHeight + theme.spaceSm), textWidth, descHeight},
                gameDescription(game), descStyle);
  }

  if (buttonWidth <= 0 || rowHeight <= 0) return;
  // The button cursor only: touch boards launch on tap and show no cursor.
  const bool buttonCursor = !mappedInput.hasTouch();
  const int buttonsX = card.right() - pad - buttonsWidth;
  for (int mode = 0; mode < MODE_COUNT; ++mode) {
    const int value = game * MODE_COUNT + mode;
    fui::ButtonProps props;
    props.label = modeLabel(mode);
    props.action = ACTION_PLAY;
    props.value = static_cast<int16_t>(value);
    props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
    props.text = buttonText;
    props.styles = theme.button;
    props.radius = theme.controlRadius;
    props.minTouchSize = theme.minTouchSize;
    props.state = buttonCursor && selectorIndex == value ? fui::StateSelected : fui::StateNormal;
    const fui::Rect rect{static_cast<int16_t>(buttonsX + mode * (buttonWidth + theme.spaceMd)),
                         static_cast<int16_t>(buttonY), buttonWidth, rowHeight};
    fui::button(screen.frame(), rect, props);
  }
}

void GamesMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif  // CROSSPOINT_GAMES
