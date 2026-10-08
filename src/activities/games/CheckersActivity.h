#pragma once

#include <cstdint>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// International draughts (10x10): flying kings, mandatory capture with the
// maximum-piece rule, multi-jumps. Two modes: 2 players on the same device,
// or against the built-in AI (black) — a depth-6 alpha-beta search.
class CheckersActivity final : public Activity {
 public:
  CheckersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool vsAi)
      : Activity("Checkers", renderer, mappedInput), vsAi(vsAi) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Move {
    // Sequence of squares visited (from, then each landing square).
    std::vector<int8_t> path;
    // Squares captured along the way.
    std::vector<int8_t> captured;
  };

  static constexpr int MATE = 1000000;
  static constexpr int AI_DEPTH = 6;
  static constexpr int NODE_LIMIT = 60000;

  // 1 = white man, 2 = white king, -1 = black man, -2 = black king, 0 = empty.
  int8_t b[100]{};
  bool whiteTurn = true;
  std::vector<Move> legalMoves;
  std::vector<const Move*> targets;
  int selected = -1;
  int lockedPiece = -1;  // piece forced to continue a multi-jump
  int cursorRow = 6;
  int cursorCol = 1;
  bool gameOver = false;
  const char* statusOverride = nullptr;
  bool aiPending = false;
  int renderCount = 0;
  bool vsAi;
  int nodes = 0;

  int boardX = 0;
  int boardY = 0;
  int squareSize = 0;

  ButtonNavigator buttonNavigator;

  void reset();
  static bool isWhite(int p) { return p > 0; }
  static bool isKing(int p) { return p == 2 || p == -2; }
  static bool playable(int row, int col) { return ((row + col) & 1) == 1; }

  void computeLegalMoves();
  void genMovesFor(const int8_t board[100], bool white, std::vector<Move>& out) const;
  void capturesFrom(const int8_t board[100], int square, std::vector<int8_t>& path,
                    std::vector<int8_t>& captured, std::vector<Move>& out) const;
  static void applyOnBoard(int8_t board[100], const Move& m);
  int evalBoard(const int8_t board[100]) const;
  int searchBoard(const int8_t board[100], bool white, int depth, int alpha, int beta, int ply);
  void rebuildTargets();
  void handleSquareChosen(int square);
  void playMove(const Move& m);
  void updateStatusAfterMove();
  void runAi();
  int squareFromTouch(int x, int y) const;
  void drawPiece(int piece, int x, int y, int size) const;
};
