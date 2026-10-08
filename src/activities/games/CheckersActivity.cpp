#include "CheckersActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include <esp_random.h>

#include "PieceArt.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int DR[4] = {-1, -1, 1, 1};
constexpr int DC[4] = {-1, 1, -1, 1};

inline bool onBoard(int row, int col) { return row >= 0 && row < 10 && col >= 0 && col < 10; }
}  // namespace

void CheckersActivity::onEnter() {
  Activity::onEnter();
  reset();
  requestUpdate();
}

void CheckersActivity::reset() {
  for (int i = 0; i < 100; i++) b[i] = 0;
  for (int row = 0; row < 4; row++)
    for (int col = 0; col < 10; col++)
      if (playable(row, col)) b[row * 10 + col] = -1;  // black on top
  for (int row = 6; row < 10; row++)
    for (int col = 0; col < 10; col++)
      if (playable(row, col)) b[row * 10 + col] = 1;  // white at the bottom
  whiteTurn = true;
  selected = -1;
  lockedPiece = -1;
  cursorRow = 6;
  cursorCol = 1;
  gameOver = false;
  statusOverride = nullptr;
  aiPending = false;
  computeLegalMoves();
  targets.clear();
}

// ---------------------------------------------------------------------------
// Move generation (maximum-capture rule)
// ---------------------------------------------------------------------------

void CheckersActivity::capturesFrom(const int8_t board[100], const int square, std::vector<int8_t>& path,
                                    std::vector<int8_t>& captured, std::vector<Move>& out) const {
  // path holds every visited square, starting with the origin; captured pieces
  // are removed immediately in the working board so they can't be jumped twice.
  const int row = square / 10;
  const int col = square % 10;
  const int piece = board[square];
  const bool white = piece > 0;
  const bool king = isKing(piece);
  bool extended = false;

  for (int d = 0; d < 4; d++) {
    int r = row + DR[d];
    int c = col + DC[d];
    while (onBoard(r, c)) {
      const int mid = r * 10 + c;
      if (board[mid] != 0) {
        if ((board[mid] > 0) == white) break;  // own piece blocks
        // Enemy piece: try every empty landing square beyond it.
        int lr = r + DR[d];
        int lc = c + DC[d];
        while (onBoard(lr, lc) && board[lr * 10 + lc] == 0) {
          const int land = lr * 10 + lc;
          int8_t next[100];
          memcpy(next, board, sizeof(next));
          next[square] = 0;
          next[mid] = 0;
          next[land] = piece;
          path.push_back(land);
          captured.push_back(mid);
          // A man crowned by capture stops there (no king continuation).
          const bool crowned = !king && (land / 10 == 0 || land / 10 == 9);
          if (crowned) {
            Move m;
            m.path = path;
            m.captured = captured;
            out.push_back(std::move(m));
          } else {
            capturesFrom(next, land, path, captured, out);
          }
          extended = true;
          path.pop_back();
          captured.pop_back();
          if (!king) break;  // men land just beyond the captured piece
          lr += DR[d];
          lc += DC[d];
        }
        break;
      }
      if (!king) break;  // men only look at the adjacent square
      r += DR[d];
      c += DC[d];
    }
  }

  // Leaf of a capture tree: record the complete sequence (origin included).
  if (!extended && path.size() > 1) {
    Move m;
    m.path = path;
    m.captured = captured;
    out.push_back(std::move(m));
  }
}

void CheckersActivity::genMovesFor(const int8_t board[100], const bool white, std::vector<Move>& out) const {
  out.clear();

  // Collect captures first (mandatory, and only the longest count).
  std::vector<Move> captures;
  for (int sq = 0; sq < 100; sq++) {
    const int piece = board[sq];
    if (piece == 0 || (piece > 0) != white) continue;
    std::vector<int8_t> path{static_cast<int8_t>(sq)};
    std::vector<int8_t> captured;
    capturesFrom(board, sq, path, captured, captures);
  }

  if (!captures.empty()) {
    size_t maxCap = 0;
    for (const auto& m : captures) maxCap = std::max(maxCap, m.captured.size());
    for (auto& m : captures)
      if (m.captured.size() == maxCap) out.push_back(std::move(m));
    return;
  }

  // Quiet moves.
  for (int sq = 0; sq < 100; sq++) {
    const int piece = board[sq];
    if (piece == 0 || (piece > 0) != white) continue;
    const int row = sq / 10;
    const int col = sq % 10;
    if (isKing(piece)) {
      for (int d = 0; d < 4; d++) {
        int r = row + DR[d];
        int c = col + DC[d];
        while (onBoard(r, c) && board[r * 10 + c] == 0) {
          Move m;
          m.path = {static_cast<int8_t>(sq), static_cast<int8_t>(r * 10 + c)};
          out.push_back(m);
          r += DR[d];
          c += DC[d];
        }
      }
    } else {
      const int forward = white ? -1 : 1;
      for (const int dc : {-1, 1}) {
        const int r = row + forward;
        const int c = col + dc;
        if (onBoard(r, c) && board[r * 10 + c] == 0) {
          Move m;
          m.path = {static_cast<int8_t>(sq), static_cast<int8_t>(r * 10 + c)};
          out.push_back(m);
        }
      }
    }
  }
}

void CheckersActivity::computeLegalMoves() {
  if (lockedPiece >= 0) {
    // A forced continuation must be a capture by the locked piece.
    legalMoves.clear();
    std::vector<int8_t> path{static_cast<int8_t>(lockedPiece)};
    std::vector<int8_t> captured;
    capturesFrom(b, lockedPiece, path, captured, legalMoves);
    return;
  }
  genMovesFor(b, whiteTurn, legalMoves);
}

void CheckersActivity::rebuildTargets() {
  targets.clear();
  if (selected < 0) return;
  for (const auto& m : legalMoves)
    if (!m.path.empty() && m.path.front() == selected) targets.push_back(&m);
}

// ---------------------------------------------------------------------------
// Game flow
// ---------------------------------------------------------------------------

void CheckersActivity::playMove(const Move& m) {
  const int from = m.path.front();
  const int to = m.path.back();
  const int piece = b[from];

  b[from] = 0;
  b[to] = piece;
  for (const int8_t cap : m.captured) b[cap] = 0;

  // Promotion on the last row (even mid-sequence the run stops, per the rules:
  // a man reaching the last rank by capture is crowned and stops).
  if (!isKing(piece) && (to / 10 == 0 || to / 10 == 9)) {
    b[to] = piece > 0 ? 2 : -2;
  }

  whiteTurn = !whiteTurn;
  lockedPiece = -1;
  selected = -1;
  targets.clear();
  computeLegalMoves();
  updateStatusAfterMove();
}

void CheckersActivity::updateStatusAfterMove() {
  // Count material.
  int whiteCount = 0, blackCount = 0;
  for (const int8_t p : b) {
    if (p > 0) whiteCount++;
    if (p < 0) blackCount++;
  }
  if (whiteCount == 0 || blackCount == 0 || legalMoves.empty()) {
    gameOver = true;
    statusOverride = whiteTurn ? tr(STR_BLACK_WINS) : tr(STR_WHITE_WINS);
    return;
  }
  // Any capture available? Remind the player it is mandatory.
  statusOverride = nullptr;
  for (const auto& m : legalMoves)
    if (!m.captured.empty()) {
      statusOverride = tr(STR_MUST_CAPTURE);
      break;
    }
  if (vsAi && !whiteTurn) aiPending = true;
}

void CheckersActivity::handleSquareChosen(const int square) {
  if (gameOver || (vsAi && !whiteTurn)) return;
  const int piece = b[square];

  if (selected < 0) {
    if (piece != 0 && (piece > 0) == whiteTurn) {
      // Only pieces that actually have a legal move may be selected.
      for (const auto& m : legalMoves) {
        if (!m.path.empty() && m.path.front() == square) {
          selected = square;
          rebuildTargets();
          requestUpdate();
          return;
        }
      }
    }
    return;
  }

  if (square == selected) {
    if (lockedPiece < 0) {
      selected = -1;
      targets.clear();
      requestUpdate();
    }
    return;
  }

  for (const auto* m : targets) {
    if (m->path.size() >= 2 && m->path[1] == square) {
      playMove(*m);
      requestUpdate();
      return;
    }
  }

  if (lockedPiece < 0) {
    if (piece != 0 && (piece > 0) == whiteTurn) {
      selected = square;
      rebuildTargets();
    } else {
      selected = -1;
      targets.clear();
    }
    requestUpdate();
  }
}

void CheckersActivity::applyOnBoard(int8_t board[100], const Move& m) {
  const int from = m.path.front();
  const int to = m.path.back();
  const int piece = board[from];
  board[from] = 0;
  board[to] = piece;
  for (const int8_t cap : m.captured) board[cap] = 0;
  if (!isKing(piece) && (to / 10 == 0 || to / 10 == 9)) {
    board[to] = piece > 0 ? 2 : -2;
  }
}

int CheckersActivity::evalBoard(const int8_t board[100]) const {
  // White perspective.
  int score = 0;
  for (int sq = 0; sq < 100; sq++) {
    const int p = board[sq];
    if (p == 0) continue;
    const int row = sq / 10;
    int v;
    if (isKing(p)) {
      v = 300;
    } else {
      // Men: material + advancement (moving towards promotion).
      v = 100 + (p > 0 ? (9 - row) : row) * 6;
      // Back-rank guard: a man on its home row keeps the king row protected.
      if ((p > 0 && row == 9) || (p < 0 && row == 0)) v += 8;
    }
    score += p > 0 ? v : -v;
  }
  return score;
}

int CheckersActivity::searchBoard(const int8_t board[100], const bool white, const int depth, int alpha,
                                  const int beta, const int ply) {
  nodes++;
  if (depth == 0 || nodes > NODE_LIMIT) {
    const int eval = evalBoard(board);
    return white ? eval : -eval;
  }

  std::vector<Move> moves;
  genMovesFor(board, white, moves);
  if (moves.empty()) return -MATE + ply;  // no moves: side to move loses

  // Captures (already longest-only) first, bigger hauls first.
  std::vector<size_t> order(moves.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(),
            [&](size_t a, size_t b) { return moves[a].captured.size() > moves[b].captured.size(); });

  int best = -MATE - 1;
  for (const size_t idx : order) {
    int8_t next[100];
    memcpy(next, board, sizeof(next));
    applyOnBoard(next, moves[idx]);
    const int score = -searchBoard(next, !white, depth - 1, -beta, -alpha, ply + 1);
    if (score > best) best = score;
    if (score > alpha) alpha = score;
    if (alpha >= beta) break;
    if (nodes > NODE_LIMIT) break;
  }
  return best;
}

void CheckersActivity::runAi() {
  if (gameOver || whiteTurn || legalMoves.empty()) {
    aiPending = false;
    return;
  }
  nodes = 0;
  int bestScore = -MATE - 1;
  std::vector<const Move*> bestMoves;
  for (const auto& m : legalMoves) {
    int8_t next[100];
    memcpy(next, b, sizeof(next));
    applyOnBoard(next, m);
    const int score = -searchBoard(next, true, AI_DEPTH - 1, -MATE, MATE, 1);
    if (score > bestScore) {
      bestScore = score;
      bestMoves.clear();
      bestMoves.push_back(&m);
    } else if (score == bestScore) {
      bestMoves.push_back(&m);
    }
  }
  aiPending = false;
  if (!bestMoves.empty()) {
    // Random pick among equally best moves for variety.
    playMove(*bestMoves[esp_random() % bestMoves.size()]);
  }
  requestUpdate();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

int CheckersActivity::squareFromTouch(const int x, const int y) const {
  if (squareSize <= 0) return -1;
  if (x < boardX || y < boardY) return -1;
  const int col = (x - boardX) / squareSize;
  const int row = (y - boardY) / squareSize;
  if (col < 0 || col > 9 || row < 0 || row > 9) return -1;
  return row * 10 + col;
}

void CheckersActivity::loop() {
  if (aiPending) {
    runAi();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenLeft}, [this] {
    if (cursorCol > 0) cursorCol--;
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenRight}, [this] {
    if (cursorCol < 9) cursorCol++;
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenUp}, [this] {
    if (cursorRow > 0) cursorRow--;
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenDown}, [this] {
    if (cursorRow < 9) cursorRow++;
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (selected >= 0 && lockedPiece < 0) {
      selected = -1;
      targets.clear();
      requestUpdate();
    } else if (selected < 0) {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (gameOver) {
      reset();
      requestUpdate();
    } else {
      handleSquareChosen(cursorRow * 10 + cursorCol);
    }
    return;
  }

  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (gameOver) {
      reset();
      requestUpdate();
      return;
    }
    const int sq = squareFromTouch(tx, ty);
    if (sq >= 0) {
      cursorRow = sq / 10;
      cursorCol = sq % 10;
      handleSquareChosen(sq);
    }
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void CheckersActivity::drawPiece(const int piece, const int x, const int y, const int size) const {
  const int margin = size / 10;
  const int tx = x + margin;
  const int ty = y + margin;
  const int ts = size - 2 * margin;
  const bool white = piece > 0;

  renderer.fillRoundedRect(tx, ty, ts, ts, ts / 2, white ? White : Black);
  renderer.drawRoundedRect(tx, ty, ts, ts, 2, ts / 2, !white);
  if (isKing(piece)) {
    // Crown marker in the opposite color.
    const int inset = ts / 5;
    gameart::drawCrown(renderer, tx + inset, ty + inset, ts - 2 * inset, white);
  }
}

void CheckersActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight - metrics.topPadding},
                 tr(STR_CHECKERS), nullptr, true);

  const int statusY = metrics.headerHeight + 4;
  const char* status = statusOverride;
  if (!status) status = whiteTurn ? tr(STR_WHITE_PLAYS) : tr(STR_BLACK_PLAYS);
  renderer.drawCenteredText(UI_10_FONT_ID, statusY, status);

  const int top = statusY + renderer.getTextHeight(UI_10_FONT_ID) + 12;
  const int bottomReserve = metrics.buttonHintsHeight + 8;
  const int availH = pageHeight - top - bottomReserve;
  const int availW = pageWidth - 24;
  squareSize = (availW < availH ? availW : availH) / 10;
  boardX = (pageWidth - squareSize * 10) / 2;
  boardY = top + (availH - squareSize * 10) / 2;

  for (int row = 0; row < 10; row++) {
    for (int col = 0; col < 10; col++) {
      const int x = boardX + col * squareSize;
      const int y = boardY + row * squareSize;
      if (playable(row, col)) {
        renderer.fillRectDither(x, y, squareSize, squareSize, LightGray);
      } else {
        renderer.fillRect(x, y, squareSize, squareSize, false);
      }
      const int sq = row * 10 + col;
      if (b[sq] != 0) drawPiece(b[sq], x, y, squareSize);
    }
  }
  renderer.drawRect(boardX, boardY, squareSize * 10, squareSize * 10, 2, true);

  for (const auto* m : targets) {
    const int to = m->path[1];
    const int x = boardX + (to % 10) * squareSize;
    const int y = boardY + (to / 10) * squareSize;
    const int d = squareSize / 4;
    renderer.fillRoundedRect(x + (squareSize - d) / 2, y + (squareSize - d) / 2, d, d, d / 2, DarkGray);
  }

  if (selected >= 0) {
    renderer.drawRect(boardX + (selected % 10) * squareSize, boardY + (selected / 10) * squareSize, squareSize,
                      squareSize, 3, true);
  }

  renderer.drawRect(boardX + cursorCol * squareSize + 2, boardY + cursorRow * squareSize + 2, squareSize - 4,
                    squareSize - 4, 1, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), gameOver ? tr(STR_NEW_GAME) : tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const bool halfRefresh = (renderCount % 12) == 0;
  renderCount++;
  renderer.displayBuffer(halfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
