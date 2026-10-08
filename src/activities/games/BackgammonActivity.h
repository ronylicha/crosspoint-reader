#pragma once

#include <cstdint>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Backgammon with full core rules: dice (doubles count twice), hitting blots,
// re-entry from the bar, bearing off. Two modes: 2 players on the same device,
// or against the built-in AI (black) — exhaustive enumeration of the legal
// move sequences for the roll, scored by a positional heuristic.
class BackgammonActivity final : public Activity {
 public:
  BackgammonActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool vsAi)
      : Activity("Backgammon", renderer, mappedInput), vsAi(vsAi) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Point index 0..23 = points 1..24. Positive = white checkers, negative = black.
  struct State {
    int8_t pts[24]{};
    int8_t bar[2]{};  // [0] white, [1] black
    int8_t off[2]{};
  };

  struct Step {
    int8_t from = 0;  // 0..23, or SRC_BAR
    int8_t to = 0;    // 0..23, or DST_OFF
    uint8_t die = 0;
    bool hit = false;
  };

  static constexpr int8_t SRC_BAR = 24;
  static constexpr int8_t DST_OFF = 24;

  enum Phase : uint8_t { ROLL, MOVE, OVER };

  State st;
  bool whiteTurn = true;
  Phase phase = ROLL;
  uint8_t diceLeft[4]{};  // remaining dice values (1..6), up to 4 for doubles
  int diceCount = 0;      // entries in diceLeft
  int selected = -1;      // selected source 0..23 or SRC_BAR, -1 = none
  int cursorPoint = 12;   // button-navigation cursor (0..23, or SRC_BAR)
  bool gameOver = false;
  bool whiteWon = false;
  const char* statusOverride = nullptr;
  bool aiPending = false;
  int renderCount = 0;
  bool vsAi;

  // Board geometry computed in render(), reused by touch input.
  int boardX = 0;
  int boardY = 0;
  int boardW = 0;
  int boardH = 0;
  int pointW = 0;
  int barW = 0;
  int diceZoneY = 0;
  int diceZoneH = 0;

  ButtonNavigator buttonNavigator;

  void reset();

  // --- Rules ----------------------------------------------------------------
  static bool canBearOff(const State& s, bool white);
  // Legal single-die steps for the side to move; enforces bar-first entry.
  static void genSteps(const State& s, bool white, uint8_t die, std::vector<Step>& out);
  static void applyStep(State& s, bool white, const Step& m);
  static int pipCount(const State& s, bool white);
  // Dice values (from diceLeft) that have at least one legal step; applies the
  // "higher die first when only one is playable" rule.
  void playableDice(bool outMask[7]) const;

  // --- Flow -----------------------------------------------------------------
  void rollDice();
  void consumeDie(uint8_t die);
  bool anyStepAvailable() const;
  void endTurn();
  void handlePointChosen(int point);  // point 0..23 or SRC_BAR
  // --- AI -------------------------------------------------------------------
  void runAi();
  struct AiCtx {
    int nodes = 0;
    std::vector<State> results;
  };
  static void dfsSequences(const State& s, bool white, const uint8_t* dice, int count, AiCtx& ctx);
  static int evaluate(const State& s);

  // --- Input / render helpers ------------------------------------------------
  int pointFromTouch(int x, int y) const;  // 0..23, SRC_BAR, or -1
  void drawChecker(int cx, int cy, int r, bool white) const;
  void drawDice() const;
};
