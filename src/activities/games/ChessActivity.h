#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Chess with full rules (castling, en passant, promotion with piece choice,
// check/checkmate, stalemate). Two modes: 2 players on the same device, or
// against the built-in AI (black) — a depth-3 alpha-beta search with
// piece-square tables.
class ChessActivity final : public Activity {
 public:
  ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool vsAi)
      : Activity("Chess", renderer, mappedInput), vsAi(vsAi) {}

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
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

  static constexpr int MATE = 1000000;
  static constexpr int AI_DEPTH = 3;
  static constexpr int NODE_LIMIT = 60000;
  static constexpr uint8_t SESSION_VERSION = 1;
  static constexpr size_t SESSION_SIZE = 68;  // version, board[64], turn, castling, en passant

  Position pos;
  std::vector<Move> legalMoves;      // legal moves for the side to move
  std::vector<const Move*> targets;  // legal moves from `selected`
  int selected = -1;                 // selected square, -1 = none
  int cursorRow = 6;
  int cursorCol = 4;
  bool gameOver = false;
  const char* statusOverride = nullptr;  // end-of-game message
  bool aiPending = false;
  bool confirmRestart = false;
  bool backLongHandled = false;
  bool sessionDirty = true;
  int renderCount = 0;
  bool vsAi;

  // Promotion picker: set when the human plays a pawn to the last rank.
  Move promoCands[4]{};  // candidate moves (Q, N, R, B) for the pending promotion
  int promoCount = 0;    // 0 = no pending promotion
  int promoSel = 0;      // highlighted option (keyboard navigation)

  // Search state (member to avoid passing through every call).
  int nodes = 0;

  // Board geometry, computed in render() and reused by touch input.
  int boardX = 0;
  int boardY = 0;
  int squareSize = 0;
  // Promotion overlay geometry (set in render()).
  int promoBoxX = 0;
  int promoBoxY = 0;
  int promoBoxW = 0;
  int promoBoxH = 0;
  int restartX = 0;
  int restartY = 0;
  int restartW = 0;
  int restartH = 0;

  ButtonNavigator buttonNavigator;

  void reset();
  bool loadSession();
  bool saveSession();
  static bool isWhite(int p) { return p > 0; }
  static int pieceOf(int p) { return p > 0 ? p : -p; }

  void genPseudo(const Position& p, bool white, std::vector<Move>& out) const;
  static void applyMove(Position& p, const Move& m);
  bool inCheck(const Position& p, bool white) const;
  bool isLegalMove(const Position& p, const Move& m) const;
  void computeLegalMoves();
  void rebuildTargets();
  void handleSquareChosen(int square);
  void beginPromotionChoice(const Move* const* cands, int count);
  void playMove(const Move& m);
  void updateStatusAfterMove();
  void runAi();
  int evaluate(const Position& p) const;
  int search(Position& p, int depth, int alpha, int beta, int ply);
  int squareFromTouch(int x, int y) const;
  void drawPiece(int piece, int x, int y, int size) const;
  void renderPromotionOverlay();
};
