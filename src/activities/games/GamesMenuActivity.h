#pragma once

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Games tab root screen: one card per bundled board game (chess, checkers,
// backgammon), each with a "2 players" and a "vs AI" button that starts a game.
class GamesMenuActivity final : public Activity, private UiAppHost {
 public:
  GamesMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GamesMenu", renderer, mappedInput), UiAppHost(renderer) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int GAME_COUNT = 3;
  static constexpr int MODE_COUNT = 2;  // 0 = two players, 1 = vs AI
  static constexpr int ITEM_COUNT = GAME_COUNT * MODE_COUNT;

  // Flat selection: game * MODE_COUNT + mode (button boards only).
  int selectorIndex = 0;
  ButtonNavigator buttonNavigator;

  static void menuScreen(UiScreen& screen, void* user);
  static void onPlayEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildMenuScreen(UiScreen& screen);
  void buildCard(UiScreen& screen, const freeink::ui::Rect& card, int game);
  void launchSelected() const;
};
