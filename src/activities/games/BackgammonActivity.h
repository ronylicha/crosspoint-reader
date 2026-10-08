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
  void onExit() override;
  void prepareForSleep() override;
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
  uint8_t diceLeft[4]{};    // remaining dice values (1..6), up to 4 for doubles
  int diceCount = 0;        // entries in diceLeft
  uint8_t diceRolled[4]{};  // full roll as thrown (for display), used dice greyed out
  int diceRolledCount = 0;
  int selected = -1;     // selected source 0..23 or SRC_BAR, -1 = none
  int cursorPoint = 12;  // button-navigation cursor (0..23, or SRC_BAR)
  bool gameOver = false;
  bool whiteWon = false;
  const char* statusOverride = nullptr;
  bool aiPending = false;
  bool aiPlaying = false;
  Step aiSteps[4]{};
  uint8_t aiStepCount = 0;
  uint8_t aiStepIndex = 0;
  uint32_t aiNextStepAt = 0;
  bool confirmRestart = false;
  bool backLongHandled = false;
  bool sessionDirty = true;
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
  int restartX = 0;
  int restartY = 0;
  int restartW = 0;
  int restartH = 0;

  ButtonNavigator buttonNavigator;

  void reset();
  bool loadSession();
  bool saveSession();

  // --- Rules ----------------------------------------------------------------
  static bool canBearOff(const State& s, bool white);
  // Legal single-die steps for the side to move; enforces bar-first entry.
  static void genSteps(const State& s, bool white, uint8_t die, std::vector<Step>& out);
  static void applyStep(State& s, bool white, const Step& m);
  static int pipCount(const State& s, bool white);
  // Max number of dice usable from this position with this roll (0..count).
  static int maxUsable(const State& s, bool white, const uint8_t* dice, int count);
  // First steps that are fully legal: only moves belonging to a sequence that
  // uses the maximum number of dice; when only one die of a mixed roll can be
  // played, the higher one is forced.
  static void legalFirstSteps(const State& s, bool white, const uint8_t* dice, int count, std::vector<Step>& out);

  // --- Flow -----------------------------------------------------------------
  void rollDice();
  void consumeDie(uint8_t die);
  bool anyStepAvailable() const;
  void endTurn();
  void handlePointChosen(int point);  // point 0..23 or SRC_BAR
  bool findSequence(int source, int destination, Step out[4], int& count) const;
  static bool findSequenceFrom(const State& s, bool white, const uint8_t* dice, int count, int source, int destination,
                               Step out[4], int& outCount, int depth);
  // --- AI -------------------------------------------------------------------
  void runAi();
  void advanceAi();
  struct AiCtx {
    int nodes = 0;
    int bestScore = 0;
    bool hasBest = false;
    Step path[4]{};
    Step bestSteps[4]{};
    uint8_t bestCount = 0;
  };
  static void dfsSequences(const State& s, bool white, const uint8_t* dice, int count, AiCtx& ctx, int depth = 0);
  static int evaluate(const State& s);

  // --- Input / render helpers ------------------------------------------------
  int pointFromTouch(int x, int y) const;  // 0..23, SRC_BAR, or -1
  void drawChecker(int cx, int cy, int r, bool white) const;
  void drawDice() const;
};
