#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Entry point for the bundled board games (chess and checkers).
// Lets the user pick a game and a mode (2 players on the same device,
// or against a simple built-in AI).
class GamesMenuActivity final : public Activity {
 public:
  GamesMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GamesMenu", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int selectorIndex = 0;
  int menuTop = 0;
  int menuRowHeight = 0;
  ButtonNavigator buttonNavigator;

  static constexpr int ITEM_COUNT = 4;
  std::string labelFor(int index) const;
  void launchSelected() const;
};
