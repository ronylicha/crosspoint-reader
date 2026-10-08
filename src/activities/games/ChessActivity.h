#pragma once

#include <cstdint>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Chess with full rules (castling, en passant, promotion, check/checkmate,
// stalemate). Two modes: 2 players on the same device, or against a simple
// built-in AI (black).
class ChessActivity final : public Activity {
 public:
  ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool vsAi)
      : Activity("Chess", renderer, mappedInput), vsAi(vsAi) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Move {
    uint8_t from = 0;
    uint8_t to = 0;
    uint8_t promo = 0;  // promoted piece code (positive), 0 = none
  };

  struct Position {
    int8_t b[64]{};
    bool whiteTurn = true;
    uint8_t castling = 0x0F;  // K Q k q
    int8_t ep = -1;           // en passant target square, -1 = none
  };

  static constexpr int EMPTY = 0;
  static constexpr int PAWN = 1;
  static constexpr int KNIGHT = 2;
  static constexpr int BISHOP = 3;
  static constexpr int ROOK = 4;
  static constexpr int QUEEN = 5;
  static constexpr int KING = 6;

  Position pos;
  std::vector<Move> legalMoves;       // legal moves for the side to move
  std::vector<const Move*> targets;   // legal moves from `selected`
  int selected = -1;                  // selected square, -1 = none
  int cursorRow = 6;
  int cursorCol = 4;
  bool gameOver = false;
  const char* statusOverride = nullptr;  // end-of-game message
  bool aiPending = false;
  int renderCount = 0;
  bool vsAi;

  // Board geometry, computed in render() and reused by touch input.
  int boardX = 0;
  int boardY = 0;
  int squareSize = 0;

  ButtonNavigator buttonNavigator;

  void reset();
  static bool isWhite(int p) { return p > 0; }
  static int pieceOf(int p) { return p > 0 ? p : -p; }

  void genPseudo(const Position& p, bool white, std::vector<Move>& out) const;
  static void applyMove(Position& p, const Move& m);
  bool inCheck(const Position& p, bool white) const;
  void computeLegalMoves();
  void rebuildTargets();
  void handleSquareChosen(int square);
  void playMove(const Move& m);
  void updateStatusAfterMove();
  void runAi();
  int evaluate(const Position& p) const;
  int squareFromTouch(int x, int y) const;
  void drawPiece(int piece, int x, int y, int size) const;
};
