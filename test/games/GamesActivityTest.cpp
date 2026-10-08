#include <GameSession.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Inspect state while still compiling and executing the actual activity CPPs.
#define private public
#include "BackgammonActivity.h"
#include "CheckersActivity.h"
#include "ChessActivity.h"
#undef private

namespace {
class GamesTest : public ::testing::Test {
 protected:
  GfxRenderer renderer;
  MappedInputManager input;
  void SetUp() override {
    host::fs = {};
    host::reset();
  }
  void TearDown() override {
    EXPECT_EQ(host::lockViolations, 0);
    EXPECT_EQ(host::lockDepth, 0);
  }
  void setupCapture(CheckersActivity& game, bool white = true) {
    game.reset();
    std::fill(std::begin(game.b), std::end(game.b), 0);
    if (white) {
      game.b[61] = 1;
      game.b[52] = -1;
      game.b[34] = -1;
      game.b[9] = -1;
    } else {
      game.b[38] = -1;
      game.b[47] = 1;
      game.b[65] = 1;
      game.b[90] = 1;
    }
    std::copy(std::begin(game.b), std::end(game.b), game.turnBoard);
    game.whiteTurn = white;
    game.computeLegalMoves();
    game.updateStatusAfterMove();
  }
  void setupBackgammon(BackgammonActivity& game, std::initializer_list<uint8_t> dice = {3, 4}) {
    game.reset();
    game.st = {};
    game.st.pts[23] = 1;
    game.st.off[0] = 14;
    game.st.pts[0] = -15;
    game.phase = BackgammonActivity::MOVE;
    game.diceCount = game.diceRolledCount = static_cast<int>(dice.size());
    std::copy(dice.begin(), dice.end(), game.diceLeft);
    std::copy(dice.begin(), dice.end(), game.diceRolled);
  }
  template <class Game>
  void callerExit(Game& game) {
    RenderLock caller(game);
    game.onExit();
  }
  template <class Game>
  void callerSleep(Game& game) {
    RenderLock caller(game);
    game.prepareForSleep();
  }
  template <class Game>
  void callerEnter(Game& game) {
    game.onEnter();
  }
  template <class Game>
  void verifyRestart(Game& game) {
    callerEnter(game);
    const int initialWrites = host::fs.writes;
    input.held[0] = true;
    input.longPressed[0] = true;
    input.heldTime = 700;
    game.loop();
    EXPECT_TRUE(game.confirmRestart);
    input.clear();
    input.released[0] = true;
    game.loop();
    EXPECT_TRUE(game.confirmRestart) << "release of long Back must be consumed";
    EXPECT_EQ(host::finishCalls, 0);
    input.clear();
    input.released[0] = true;
    game.loop();
    EXPECT_FALSE(game.confirmRestart);
    EXPECT_EQ(host::fs.writes, initialWrites) << "cancel must preserve state without writing";
    input.clear();
    game.confirmRestart = true;
    input.released[1] = true;
    game.loop();
    EXPECT_FALSE(game.confirmRestart);
    EXPECT_GT(host::fs.writes, initialWrites);
    input.clear();
    callerExit(game);
  }
  void verifyTouchRestart(int width, int height) {
    renderer.screenWidth = width;
    renderer.screenHeight = height;
    ASSERT_EQ(UITheme::getInstance().getMetrics().buttonHintsHeight, 0);
    CheckersActivity game(renderer, input, false);
    setupCapture(game);
    game.handleSquareChosen(61);
    game.handleSquareChosen(43);
    game.requestUpdateAndWait();
    const auto before = std::to_array(game.b);
    auto openDialog = [&] {
      input.clear();
      input.tapped = true;
      input.tapX = game.restartX + game.restartW / 2;
      input.tapY = game.restartY + game.restartH / 2;
      game.loop();
      if (!game.confirmRestart) return false;
      input.clear();
      renderer.borders.clear();
      game.requestUpdateAndWait();
      return renderer.borders.size() >= 3U;
    };
    ASSERT_TRUE(openDialog());
    const auto cancel = renderer.borders[renderer.borders.size() - 2];
    const auto confirm = renderer.borders.back();
    EXPECT_GT(cancel.height, 0);
    EXPECT_GT(confirm.height, 0);
    EXPECT_EQ(cancel.height, renderer.getTextHeight(UI_10_FONT_ID) + 16);
    EXPECT_EQ(cancel.y, confirm.y);
    // A tap immediately outside the drawn choices must leave the dialog open.
    input.tapped = true;
    input.tapX = cancel.x + cancel.width / 2;
    input.tapY = cancel.y - 1;
    game.loop();
    EXPECT_TRUE(game.confirmRestart);
    input.tapY = cancel.y + cancel.height / 2;
    game.loop();
    EXPECT_FALSE(game.confirmRestart);
    EXPECT_EQ(std::to_array(game.b), before);
    EXPECT_EQ(game.lockedPiece, 43);
    EXPECT_EQ(game.capturePathCount, 2);
    ASSERT_TRUE(openDialog());
    const auto drawnConfirm = renderer.borders.back();
    input.tapped = true;
    input.tapX = drawnConfirm.x + drawnConfirm.width / 2;
    input.tapY = drawnConfirm.y + drawnConfirm.height / 2;
    game.loop();
    EXPECT_FALSE(game.confirmRestart);
    EXPECT_EQ(game.capturePathCount, 0);
    EXPECT_EQ(game.lockedPiece, -1);
    EXPECT_EQ(std::count(std::begin(game.b), std::end(game.b), 1), 20);
    EXPECT_TRUE(game.whiteTurn);
  }
};

TEST_F(GamesTest, TouchThemeSuppressesPhysicalButtonHintsOnly) {
  EXPECT_EQ(UITheme::getInstance().getMetrics().buttonHintsHeight, 0);
  host::hasTouch = false;
  EXPECT_EQ(UITheme::getInstance().getMetrics().buttonHintsHeight, 36);
  host::hasTouch = true;
  EXPECT_EQ(UITheme::getInstance().getMetrics().buttonHintsHeight, 0);
}
TEST_F(GamesTest, CheckersTouchRestartCancelAndConfirmInPortrait) { verifyTouchRestart(480, 800); }
TEST_F(GamesTest, CheckersTouchRestartCancelAndConfirmInLandscape) { verifyTouchRestart(800, 480); }

TEST_F(GamesTest, CheckersFullAndSteppedCaptureReachSamePosition) {
  CheckersActivity full(renderer, input, false), stepped(renderer, input, false);
  setupCapture(full);
  setupCapture(stepped);
  full.handleSquareChosen(61);
  full.handleSquareChosen(25);
  stepped.handleSquareChosen(61);
  stepped.handleSquareChosen(43);
  EXPECT_EQ(stepped.b[43], 1);
  EXPECT_EQ(stepped.b[52], 0);
  EXPECT_EQ(stepped.b[34], -1);
  EXPECT_TRUE(stepped.whiteTurn);
  EXPECT_EQ(stepped.lockedPiece, 43);
  stepped.handleSquareChosen(25);
  EXPECT_EQ(std::memcmp(full.b, stepped.b, sizeof(full.b)), 0);
  EXPECT_FALSE(stepped.whiteTurn);
  EXPECT_EQ(stepped.lockedPiece, -1);
}
TEST_F(GamesTest, CheckersForcedCaptureCannotSwitchPiecesOrTakeShorterPath) {
  CheckersActivity game(renderer, input, false);
  setupCapture(game);
  game.b[67] = 1;
  game.b[58] = -1;
  std::copy(std::begin(game.b), std::end(game.b), game.turnBoard);
  game.computeLegalMoves();
  ASSERT_FALSE(game.legalMoves.empty());
  for (const auto& move : game.legalMoves) EXPECT_EQ(move.captured.size(), 2U);
  game.handleSquareChosen(67);
  EXPECT_EQ(game.selected, -1);
  game.handleSquareChosen(61);
  game.handleSquareChosen(43);
  game.handleSquareChosen(67);
  EXPECT_EQ(game.selected, 43);
  EXPECT_EQ(game.lockedPiece, 43);
  EXPECT_EQ(game.b[67], 1);
}
TEST_F(GamesTest, CheckersCaptureSurvivesExitAndReentryWithLockedContinuation) {
  CheckersActivity first(renderer, input, false);
  setupCapture(first);
  first.handleSquareChosen(61);
  first.handleSquareChosen(43);
  callerExit(first);
  CheckersActivity restored(renderer, input, false);
  callerEnter(restored);
  EXPECT_EQ(restored.b[43], 1);
  EXPECT_EQ(restored.b[52], 0);
  EXPECT_EQ(restored.b[34], -1);
  EXPECT_EQ(restored.capturePathCount, 2);
  EXPECT_EQ(restored.lockedPiece, 43);
  EXPECT_TRUE(restored.whiteTurn);
  restored.handleSquareChosen(25, true);
  EXPECT_EQ(restored.b[25], 1);
  EXPECT_FALSE(restored.whiteTurn);
}
TEST_F(GamesTest, CheckersContinuationRetainsChosenBranch) {
  CheckersActivity game(renderer, input, false);
  game.reset();
  std::fill(std::begin(game.b), std::end(game.b), 0);
  game.b[63] = 1;
  game.b[52] = game.b[32] = game.b[54] = game.b[36] = game.b[9] = -1;
  std::copy(std::begin(game.b), std::end(game.b), game.turnBoard);
  game.computeLegalMoves();
  ASSERT_GE(game.legalMoves.size(), 2U);
  game.handleSquareChosen(63);
  game.handleSquareChosen(41);
  ASSERT_EQ(game.lockedPiece, 41);
  game.handleSquareChosen(27, true);
  EXPECT_EQ(game.b[41], 1);
  EXPECT_EQ(game.lockedPiece, 41);
  game.handleSquareChosen(23, true);
  EXPECT_EQ(game.b[23], 1);
  EXPECT_EQ(game.b[54], -1);
  EXPECT_EQ(game.b[36], -1);
  EXPECT_FALSE(game.whiteTurn);
}
TEST_F(GamesTest, CheckersCompleteLongConfirmConsumesReleaseWithoutSelectingOpponent) {
  CheckersActivity game(renderer, input, false);
  setupCapture(game);
  game.handleSquareChosen(61);
  game.cursorRow = 6;
  game.cursorCol = 1;
  input.longPressed[1] = true;
  input.held[1] = true;
  game.loop();
  EXPECT_EQ(game.b[25], 1);
  EXPECT_FALSE(game.whiteTurn);
  input.clear();
  input.released[1] = true;
  game.loop();
  EXPECT_EQ(game.selected, -1);
  EXPECT_EQ(game.b[9], -1);
}
TEST_F(GamesTest, CheckersAiShowsEveryJumpAndPausesAfterCompletedRender) {
  CheckersActivity game(renderer, input, true);
  setupCapture(game, false);
  host::renderDuration = 200;
  game.loop();
  ASSERT_TRUE(game.aiPlaying);
  EXPECT_EQ(game.b[38], -1);
  EXPECT_EQ(game.aiNextStepAt, 1000U);
  EXPECT_EQ(host::waitedRenders, 1);
  host::now = 999;
  game.loop();
  EXPECT_EQ(game.b[38], -1);
  host::now = 1000;
  game.loop();
  EXPECT_EQ(game.b[38], 0);
  EXPECT_EQ(game.b[56], -1);
  EXPECT_EQ(game.b[65], 1);
  EXPECT_EQ(game.aiNextStepAt, 2000U);
  host::now = 1999;
  game.loop();
  EXPECT_EQ(game.b[56], -1);
  host::now = 2000;
  game.loop();
  EXPECT_EQ(game.b[74], -1);
  EXPECT_FALSE(game.whiteTurn);
  EXPECT_TRUE(game.aiPlaying);
  host::now = 2999;
  game.loop();
  EXPECT_FALSE(game.whiteTurn);
  host::now = 3000;
  game.loop();
  EXPECT_TRUE(game.whiteTurn);
  EXPECT_FALSE(game.aiPlaying);
}
TEST_F(GamesTest, CheckersAiMidSequenceResumesRemainingJumpWithoutRepeatingFirst) {
  CheckersActivity game(renderer, input, true);
  setupCapture(game, false);
  game.loop();
  host::now = 800;
  game.loop();
  callerSleep(game);
  CheckersActivity restored(renderer, input, true);
  callerEnter(restored);
  EXPECT_EQ(restored.b[56], -1);
  EXPECT_EQ(restored.aiStepIndex, 1);
  EXPECT_TRUE(restored.aiPlaying);
  restored.loop();
  EXPECT_EQ(restored.b[56], -1);
  host::now += 799;
  restored.loop();
  EXPECT_EQ(restored.b[56], -1);
  ++host::now;
  restored.loop();
  EXPECT_EQ(restored.b[74], -1);
  EXPECT_EQ(restored.aiStepIndex, 2);
}
TEST_F(GamesTest, CheckersAiLastJumpReloadRetainsFinalDisplayPause) {
  CheckersActivity game(renderer, input, true);
  setupCapture(game, false);
  game.loop();
  host::now = game.aiNextStepAt;
  game.loop();
  host::now = game.aiNextStepAt;
  game.loop();
  callerSleep(game);
  CheckersActivity restored(renderer, input, true);
  callerEnter(restored);
  ASSERT_TRUE(restored.aiPlaying);
  ASSERT_EQ(restored.aiStepIndex, 2);
  EXPECT_EQ(restored.b[74], -1);
  EXPECT_FALSE(restored.whiteTurn);
  restored.loop();
  host::now += 799;
  restored.loop();
  EXPECT_FALSE(restored.whiteTurn);
  ++host::now;
  restored.loop();
  EXPECT_TRUE(restored.whiteTurn);
  EXPECT_FALSE(restored.aiPlaying);
}
TEST_F(GamesTest, CheckersCancelRestartDuringAiResumesWithFreshPause) {
  CheckersActivity game(renderer, input, true);
  setupCapture(game, false);
  game.loop();
  game.confirmRestart = true;
  input.released[0] = true;
  host::now = 5000;
  game.loop();
  input.clear();
  EXPECT_FALSE(game.confirmRestart);
  EXPECT_EQ(game.b[38], -1);
  game.loop();
  EXPECT_EQ(game.aiNextStepAt, 5800U);
  EXPECT_EQ(game.aiStepIndex, 0);
  host::now = 5799;
  game.loop();
  EXPECT_EQ(game.b[38], -1);
  host::now = 5800;
  game.loop();
  EXPECT_EQ(game.b[56], -1);
}
TEST_F(GamesTest, BackgammonCombinedDiceMatchesIndividualMoves) {
  BackgammonActivity full(renderer, input, false), stepped(renderer, input, false);
  setupBackgammon(full);
  setupBackgammon(stepped);
  full.handlePointChosen(23);
  full.handlePointChosen(16);
  stepped.handlePointChosen(23);
  stepped.handlePointChosen(20);
  EXPECT_EQ(stepped.diceCount, 1);
  EXPECT_EQ(stepped.st.pts[20], 1);
  stepped.handlePointChosen(20);
  stepped.handlePointChosen(16);
  EXPECT_EQ(std::memcmp(&full.st, &stepped.st, sizeof(full.st)), 0);
  EXPECT_EQ(full.st.pts[16], 1);
  EXPECT_EQ(full.diceCount, 0);
  EXPECT_FALSE(full.whiteTurn);
}
TEST_F(GamesTest, BackgammonCannotJumpBlockedIntermediatePoints) {
  BackgammonActivity game(renderer, input, false);
  setupBackgammon(game);
  game.st.pts[20] = -2;
  game.st.pts[19] = -2;
  game.st.pts[0] = -11;
  BackgammonActivity::Step steps[4]{};
  int count = 0;
  EXPECT_FALSE(game.findSequence(23, 16, steps, count));
  EXPECT_EQ(game.st.pts[23], 1);
  EXPECT_EQ(game.diceCount, 2);
}
TEST_F(GamesTest, BackgammonHigherDieForcedOnlyWhenItCanActuallyBeUsed) {
  BackgammonActivity game(renderer, input, false);
  setupBackgammon(game);
  game.st.pts[19] = -2;
  game.st.pts[16] = -2;
  game.st.pts[0] = -11;
  std::vector<BackgammonActivity::Step> legal;
  BackgammonActivity::legalFirstSteps(game.st, true, game.diceLeft, game.diceCount, legal);
  ASSERT_EQ(legal.size(), 1U);
  EXPECT_EQ(legal[0].die, 3);
  game.st = {};
  game.st.bar[0] = 1;
  game.st.pts[0] = 14;
  game.st.pts[21] = -2;
  game.st.pts[0] = 14;
  game.st.pts[5] = -13;
  uint8_t dice[2] = {1, 2};
  BackgammonActivity::legalFirstSteps(game.st, true, dice, 2, legal);
  ASSERT_EQ(legal.size(), 1U);
  EXPECT_EQ(legal[0].from, BackgammonActivity::SRC_BAR);
  EXPECT_EQ(legal[0].die, 2);
}
TEST_F(GamesTest, BackgammonBarPriorityBlocksCombinedSameCheckerUntilAllReentered) {
  BackgammonActivity game(renderer, input, false);
  setupBackgammon(game, {1, 2});
  game.st.pts[23] = 0;
  game.st.off[0] = 13;
  game.st.bar[0] = 2;
  BackgammonActivity::Step path[4]{};
  int count = 0;
  EXPECT_FALSE(game.findSequence(BackgammonActivity::SRC_BAR, 21, path, count));
  EXPECT_TRUE(game.findSequence(BackgammonActivity::SRC_BAR, 23, path, count));
  EXPECT_EQ(count, 1);
  game.st.bar[0] = 1;
  game.st.off[0] = 14;
  EXPECT_TRUE(game.findSequence(BackgammonActivity::SRC_BAR, 21, path, count));
  EXPECT_EQ(count, 2);
}
TEST_F(GamesTest, BackgammonDoublesConsumeAllFourDiceOnOneChecker) {
  BackgammonActivity game(renderer, input, false);
  setupBackgammon(game, {2, 2, 2, 2});
  game.handlePointChosen(23);
  game.handlePointChosen(15);
  EXPECT_EQ(game.st.pts[15], 1);
  EXPECT_EQ(game.diceCount, 0);
  EXPECT_FALSE(game.whiteTurn);
}
TEST_F(GamesTest, BackgammonCombinedMovementCanFinishWithBearingOff) {
  BackgammonActivity game(renderer, input, false);
  setupBackgammon(game, {3, 4});
  game.st.pts[23] = 0;
  game.st.pts[6] = 1;
  game.handlePointChosen(6);
  game.handlePointChosen(BackgammonActivity::DST_OFF);
  EXPECT_EQ(game.st.off[0], 15);
  EXPECT_TRUE(game.gameOver);
  EXPECT_TRUE(game.whiteWon);
}
TEST_F(GamesTest, BackgammonInterruptedRollRetainsRemainingAndVisibleDice) {
  BackgammonActivity game(renderer, input, false);
  setupBackgammon(game);
  game.handlePointChosen(23);
  game.handlePointChosen(20);
  callerExit(game);
  BackgammonActivity restored(renderer, input, false);
  callerEnter(restored);
  EXPECT_EQ(restored.phase, BackgammonActivity::MOVE);
  EXPECT_EQ(restored.diceCount, 1);
  EXPECT_EQ(restored.diceLeft[0], 4);
  EXPECT_EQ(restored.diceRolledCount, 2);
  EXPECT_EQ(restored.st.pts[20], 1);
  EXPECT_EQ(host::randomCalls, 0);
  restored.handlePointChosen(20);
  restored.handlePointChosen(16);
  EXPECT_FALSE(restored.whiteTurn);
}
TEST_F(GamesTest, BackgammonDiceAlwaysHaveBorderIncludingSpentDice) {
  BackgammonActivity game(renderer, input, false);
  setupBackgammon(game);
  game.boardW = 400;
  game.boardH = 500;
  game.diceCount = 1;
  game.diceLeft[0] = 4;
  game.drawDice();
  ASSERT_EQ(renderer.borders.size(), 2U);
  EXPECT_GE(renderer.borders[0].thickness, 2);
  EXPECT_GE(renderer.borders[1].thickness, 2);
  EXPECT_EQ(renderer.borders[0].width, 40);
  EXPECT_EQ(renderer.borders[1].width, 40);
}
TEST_F(GamesTest, BackgammonAiShowsRollBeforeMovingThenOneCheckerStepPerPause) {
  BackgammonActivity game(renderer, input, true);
  setupBackgammon(game);
  game.st = {};
  game.st.pts[0] = -1;
  game.st.off[1] = 14;
  game.st.pts[23] = 15;
  game.phase = BackgammonActivity::ROLL;
  game.whiteTurn = false;
  game.aiPending = true;
  host::renderDuration = 200;
  game.loop();
  ASSERT_TRUE(game.aiPlaying);
  EXPECT_EQ(game.st.pts[0], -1);
  EXPECT_EQ(game.diceRolledCount, 4);
  EXPECT_EQ(game.diceCount, 4);
  EXPECT_EQ(host::randomCalls, 2);
  EXPECT_EQ(game.aiNextStepAt, 1000U);
  for (int step = 1; step <= 4; ++step) {
    host::now = game.aiNextStepAt - 1;
    game.loop();
    EXPECT_EQ(game.aiStepIndex, step - 1);
    host::now = game.aiNextStepAt;
    game.loop();
    EXPECT_EQ(game.st.pts[step], -1);
    EXPECT_EQ(game.aiStepIndex, step);
    EXPECT_EQ(game.diceCount, 4 - step);
    EXPECT_FALSE(game.whiteTurn);
  }
  host::now = game.aiNextStepAt - 1;
  game.loop();
  EXPECT_FALSE(game.whiteTurn);
  host::now = game.aiNextStepAt;
  game.loop();
  EXPECT_TRUE(game.whiteTurn);
}
TEST_F(GamesTest, BackgammonAiReloadDoesNotRerollOrReplayConsumedDice) {
  BackgammonActivity game(renderer, input, true);
  setupBackgammon(game);
  game.st = {};
  game.st.pts[0] = -1;
  game.st.off[1] = 14;
  game.st.pts[23] = 15;
  game.phase = BackgammonActivity::ROLL;
  game.whiteTurn = false;
  game.aiPending = true;
  game.loop();
  host::now = game.aiNextStepAt;
  game.loop();
  callerSleep(game);
  const int randomCalls = host::randomCalls;
  BackgammonActivity restored(renderer, input, true);
  callerEnter(restored);
  EXPECT_EQ(restored.aiStepIndex, 1);
  EXPECT_EQ(restored.diceCount, 3);
  EXPECT_EQ(restored.st.pts[1], -1);
  restored.loop();
  EXPECT_EQ(restored.st.pts[1], -1);
  host::now = restored.aiNextStepAt;
  restored.loop();
  EXPECT_EQ(restored.st.pts[2], -1);
  EXPECT_EQ(restored.aiStepIndex, 2);
  EXPECT_EQ(host::randomCalls, randomCalls);
}
TEST_F(GamesTest, BackgammonCancelRestartDuringAiResumesWithoutReroll) {
  BackgammonActivity game(renderer, input, true);
  setupBackgammon(game);
  game.st = {};
  game.st.pts[0] = -1;
  game.st.off[1] = 14;
  game.st.pts[23] = 15;
  game.phase = BackgammonActivity::ROLL;
  game.whiteTurn = false;
  game.aiPending = true;
  game.loop();
  game.confirmRestart = true;
  const int randomCalls = host::randomCalls;
  input.released[0] = true;
  host::now = 5000;
  game.loop();
  input.clear();
  EXPECT_FALSE(game.confirmRestart);
  EXPECT_EQ(game.st.pts[0], -1);
  game.loop();
  EXPECT_EQ(game.aiNextStepAt, 5800U);
  EXPECT_EQ(game.aiStepIndex, 0);
  host::now = 5800;
  game.loop();
  EXPECT_EQ(game.st.pts[1], -1);
  EXPECT_EQ(host::randomCalls, randomCalls);
}
TEST_F(GamesTest, ChessSnapshotPreservesEnPassantAndCastlingRights) {
  ChessActivity game(renderer, input, false);
  callerEnter(game);
  game.handleSquareChosen(52);
  game.handleSquareChosen(36);
  EXPECT_EQ(game.pos.ep, 44);
  callerExit(game);
  ChessActivity restored(renderer, input, false);
  callerEnter(restored);
  EXPECT_EQ(restored.pos.ep, 44);
  EXPECT_EQ(restored.pos.castling, 15);
  EXPECT_FALSE(restored.pos.whiteTurn);
  EXPECT_EQ(std::memcmp(game.pos.b, restored.pos.b, sizeof(game.pos.b)), 0);
}
TEST_F(GamesTest, ChessRetainsLostCastlingRightsAfterRookMovement) {
  ChessActivity game(renderer, input, false);
  game.reset();
  game.pos = {};
  game.pos.b[60] = 6;
  game.pos.b[4] = -6;
  game.pos.b[63] = 4;
  game.pos.castling = 1;
  game.computeLegalMoves();
  game.handleSquareChosen(63);
  game.handleSquareChosen(55);
  callerExit(game);
  ChessActivity restored(renderer, input, false);
  callerEnter(restored);
  EXPECT_EQ(restored.pos.castling, 0);
  EXPECT_EQ(restored.pos.b[55], 4);
}
TEST_F(GamesTest, ChessRejectsSemanticallyInvalidSnapshotEvenWithValidCrc) {
  ChessActivity game(renderer, input, false);
  callerEnter(game);
  uint8_t payload[68]{};
  ASSERT_TRUE(GameSession::load(GameSession::Game::Chess, false, payload, sizeof(payload)));
  payload[64] = 0;  // h1 rook absent while kingside castling remains enabled.
  ASSERT_TRUE(GameSession::save(GameSession::Game::Chess, false, payload, sizeof(payload)));
  EXPECT_FALSE(game.loadSession());
  game.sessionDirty = true;
  ASSERT_TRUE(game.saveSession());
  ASSERT_TRUE(GameSession::load(GameSession::Game::Chess, false, payload, sizeof(payload)));
  payload[67] = 45;  // An en passant target on rank three is invalid for White's turn.
  ASSERT_TRUE(GameSession::save(GameSession::Game::Chess, false, payload, sizeof(payload)));
  EXPECT_FALSE(game.loadSession());
  std::fill(payload + 1, payload + 65, 0);
  payload[61] = 6;
  payload[5] = static_cast<uint8_t>(-6);
  payload[53] = static_cast<uint8_t>(-4);
  payload[65] = 0;
  payload[66] = 0;
  payload[67] = 0;
  ASSERT_TRUE(GameSession::save(GameSession::Game::Chess, false, payload, sizeof(payload)));
  EXPECT_FALSE(game.loadSession()) << "previous mover cannot have left their king in check";
}
TEST_F(GamesTest, ChessRestartConfirmationAndLongBackRelease) {
  ChessActivity game(renderer, input, false);
  callerEnter(game);
  game.handleSquareChosen(52);
  game.handleSquareChosen(36);
  callerExit(game);
  verifyRestart(game);
  EXPECT_TRUE(game.pos.whiteTurn);
  EXPECT_EQ(game.pos.b[52], 1);
  EXPECT_EQ(game.pos.b[36], 0);
}
TEST_F(GamesTest, CheckersRestartConfirmationAndLongBackRelease) {
  CheckersActivity game(renderer, input, false);
  setupCapture(game);
  game.handleSquareChosen(61);
  game.handleSquareChosen(43);
  callerExit(game);
  verifyRestart(game);
  EXPECT_TRUE(game.whiteTurn);
  EXPECT_EQ(game.capturePathCount, 0);
  EXPECT_EQ(game.lockedPiece, -1);
  EXPECT_EQ(std::count(std::begin(game.b), std::end(game.b), 1), 20);
}
TEST_F(GamesTest, BackgammonRestartConfirmationAndLongBackRelease) {
  BackgammonActivity game(renderer, input, false);
  setupBackgammon(game);
  game.handlePointChosen(23);
  game.handlePointChosen(20);
  callerExit(game);
  verifyRestart(game);
  EXPECT_TRUE(game.whiteTurn);
  EXPECT_EQ(game.phase, BackgammonActivity::ROLL);
  EXPECT_EQ(game.diceCount, 0);
  EXPECT_EQ(game.st.off[0], 0);
  EXPECT_EQ(game.st.pts[23], 2);
}
TEST_F(GamesTest, AllGamesSkipRedundantSavesAndRetryAfterStorageFailure) {
  ChessActivity chess(renderer, input, false);
  CheckersActivity checkers(renderer, input, false);
  BackgammonActivity backgammon(renderer, input, false);
  callerEnter(chess);
  callerEnter(checkers);
  callerEnter(backgammon);
  callerSleep(chess);
  callerSleep(checkers);
  callerSleep(backgammon);
  const int writes = host::fs.writes;
  callerSleep(chess);
  callerSleep(checkers);
  callerSleep(backgammon);
  EXPECT_EQ(host::fs.writes, writes);
  chess.sessionDirty = checkers.sessionDirty = backgammon.sessionDirty = true;
  host::fs.shortWrite = true;
  callerSleep(chess);
  callerSleep(checkers);
  callerSleep(backgammon);
  EXPECT_TRUE(chess.sessionDirty);
  EXPECT_TRUE(checkers.sessionDirty);
  EXPECT_TRUE(backgammon.sessionDirty);
  host::fs.shortWrite = false;
  callerSleep(chess);
  callerSleep(checkers);
  callerSleep(backgammon);
  EXPECT_FALSE(chess.sessionDirty);
  EXPECT_FALSE(checkers.sessionDirty);
  EXPECT_FALSE(backgammon.sessionDirty);
}
}  // namespace
