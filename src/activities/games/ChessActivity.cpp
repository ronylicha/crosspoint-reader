#include "ChessActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <cstdlib>

#include <esp_random.h>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int8_t INITIAL_BOARD[64] = {
    -4, -2, -3, -5, -6, -3, -2, -4,  //
    -1, -1, -1, -1, -1, -1, -1, -1,  //
    0,  0,  0,  0,  0,  0,  0,  0,   //
    0,  0,  0,  0,  0,  0,  0,  0,   //
    0,  0,  0,  0,  0,  0,  0,  0,   //
    0,  0,  0,  0,  0,  0,  0,  0,   //
    1,  1,  1,  1,  1,  1,  1,  1,   //
    4,  2,  3,  5,  6,  3,  2,  4,   //
};
constexpr int PIECE_VALUE[7] = {0, 100, 320, 330, 500, 900, 0};
constexpr char PIECE_LETTER[7] = {' ', 'P', 'N', 'B', 'R', 'Q', 'K'};
}  // namespace

void ChessActivity::onEnter() {
  Activity::onEnter();
  reset();
  requestUpdate();
}

void ChessActivity::reset() {
  for (int i = 0; i < 64; i++) pos.b[i] = INITIAL_BOARD[i];
  pos.whiteTurn = true;
  pos.castling = 0x0F;
  pos.ep = -1;
  selected = -1;
  cursorRow = 6;
  cursorCol = 4;
  gameOver = false;
  statusOverride = nullptr;
  aiPending = false;
  computeLegalMoves();
  targets.clear();
}

// ---------------------------------------------------------------------------
// Move generation
// ---------------------------------------------------------------------------

void ChessActivity::genPseudo(const Position& p, const bool white, std::vector<Move>& out) const {
  out.clear();
  for (int sq = 0; sq < 64; sq++) {
    const int piece = p.b[sq];
    if (piece == EMPTY || isWhite(piece) != white) continue;
    const int row = sq / 8;
    const int col = sq % 8;
    const int type = pieceOf(piece);

    auto tryAdd = [&](int r, int c, bool captureOnly = false, bool moveOnly = false) {
      if (r < 0 || r > 7 || c < 0 || c > 7) return false;
      const int target = p.b[r * 8 + c];
      if (target != EMPTY && isWhite(target) == white) return false;
      if (captureOnly && target == EMPTY) return true;  // keep sliding
      if (moveOnly && target != EMPTY) return false;
      Move m;
      m.from = sq;
      m.to = r * 8 + c;
      out.push_back(m);
      return target == EMPTY;  // can keep sliding only through empty squares
    };

    if (type == PAWN) {
      const int dir = white ? -1 : 1;
      const int startRow = white ? 6 : 1;
      const int promoRow = white ? 0 : 7;
      auto addPawnMove = [&](int to) {
        Move m;
        m.from = sq;
        m.to = to;
        if (to / 8 == promoRow) m.promo = QUEEN;  // auto-queen
        out.push_back(m);
      };
      if (row + dir >= 0 && row + dir <= 7 && p.b[(row + dir) * 8 + col] == EMPTY) {
        addPawnMove((row + dir) * 8 + col);
        if (row == startRow && p.b[(row + 2 * dir) * 8 + col] == EMPTY) {
          addPawnMove((row + 2 * dir) * 8 + col);
        }
      }
      for (const int dc : {-1, 1}) {
        const int c = col + dc;
        const int r = row + dir;
        if (c < 0 || c > 7 || r < 0 || r > 7) continue;
        const int to = r * 8 + c;
        if (p.b[to] != EMPTY && isWhite(p.b[to]) != white) addPawnMove(to);
        if (to == p.ep) addPawnMove(to);
      }
    } else if (type == KNIGHT) {
      static constexpr int JUMPS[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
      for (const auto& j : JUMPS) tryAdd(row + j[0], col + j[1]);
    } else if (type == KING) {
      for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
          if (dr || dc) tryAdd(row + dr, col + dc);
      // Castling (legality regarding attacked squares is checked below).
      if (white && sq == 60) {
        if ((p.castling & 0x01) && p.b[61] == EMPTY && p.b[62] == EMPTY && p.b[63] == ROOK) tryAdd(7, 6);
        if ((p.castling & 0x02) && p.b[59] == EMPTY && p.b[58] == EMPTY && p.b[57] == EMPTY && p.b[56] == ROOK)
          tryAdd(7, 2);
      } else if (!white && sq == 4) {
        if ((p.castling & 0x04) && p.b[5] == EMPTY && p.b[6] == EMPTY && p.b[7] == -ROOK) tryAdd(0, 6);
        if ((p.castling & 0x08) && p.b[3] == EMPTY && p.b[2] == EMPTY && p.b[1] == EMPTY && p.b[0] == -ROOK)
          tryAdd(0, 2);
      }
    } else {
      static constexpr int DIAG[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
      static constexpr int ORTHO[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
      const auto* dirs = (type == BISHOP) ? DIAG : ORTHO;
      const int dirCount = (type == QUEEN) ? 8 : 4;
      for (int d = 0; d < dirCount; d++) {
        const int dr = (type == QUEEN) ? (d < 4 ? DIAG[d][0] : ORTHO[d - 4][0]) : dirs[d][0];
        const int dc = (type == QUEEN) ? (d < 4 ? DIAG[d][1] : ORTHO[d - 4][1]) : dirs[d][1];
        for (int step = 1; step < 8; step++) {
          if (!tryAdd(row + dr * step, col + dc * step)) break;
        }
      }
    }
  }
}

void ChessActivity::applyMove(Position& p, const Move& m) {
  const int piece = p.b[m.from];
  const bool white = piece > 0;

  // En passant capture
  if (pieceOf(piece) == PAWN && m.to == p.ep && (m.from % 8) != (m.to % 8) && p.b[m.to] == EMPTY) {
    p.b[m.to + (white ? 8 : -8)] = EMPTY;
  }
  p.ep = -1;
  if (pieceOf(piece) == PAWN && (m.to / 8 - m.from / 8 == 2 || m.from / 8 - m.to / 8 == 2)) {
    p.ep = (m.from + m.to) / 2;
  }

  p.b[m.to] = m.promo ? (white ? m.promo : -m.promo) : piece;
  p.b[m.from] = EMPTY;

  // Castling: move the rook too
  if (pieceOf(piece) == KING) {
    if (m.from == 60 && m.to == 62) { p.b[61] = p.b[63]; p.b[63] = EMPTY; }
    if (m.from == 60 && m.to == 58) { p.b[59] = p.b[56]; p.b[56] = EMPTY; }
    if (m.from == 4 && m.to == 6)   { p.b[5] = p.b[7];   p.b[7] = EMPTY; }
    if (m.from == 4 && m.to == 2)   { p.b[3] = p.b[0];   p.b[0] = EMPTY; }
    p.castling &= white ? 0x0C : 0x03;
  }
  // Rook moves or is captured: drop the matching right
  if (m.from == 63 || m.to == 63) p.castling &= ~0x01;
  if (m.from == 56 || m.to == 56) p.castling &= ~0x02;
  if (m.from == 7 || m.to == 7)   p.castling &= ~0x04;
  if (m.from == 0 || m.to == 0)   p.castling &= ~0x08;

  p.whiteTurn = !p.whiteTurn;
}

bool ChessActivity::inCheck(const Position& p, const bool white) const {
  int kingSq = -1;
  const int king = white ? KING : -KING;
  for (int i = 0; i < 64; i++)
    if (p.b[i] == king) kingSq = i;
  if (kingSq < 0) return true;  // should not happen; treat as in check

  // Attacked by any enemy pseudo-legal move?
  std::vector<Move> enemy;
  Position probe = p;
  genPseudo(probe, !white, enemy);
  for (const auto& m : enemy)
    if (m.to == kingSq) return true;
  return false;
}

void ChessActivity::computeLegalMoves() {
  std::vector<Move> pseudo;
  genPseudo(pos, pos.whiteTurn, pseudo);
  legalMoves.clear();
  for (const auto& m : pseudo) {
    // Castling may not pass through or out of check.
    if (pieceOf(pos.b[m.from]) == KING && (m.to - m.from == 2 || m.from - m.to == 2)) {
      if (inCheck(pos, pos.whiteTurn)) continue;
      Position mid = pos;
      Move step;
      step.from = m.from;
      step.to = (m.from + m.to) / 2;
      applyMove(mid, step);
      if (inCheck(mid, pos.whiteTurn)) continue;
    }
    Position next = pos;
    applyMove(next, m);
    if (!inCheck(next, pos.whiteTurn)) legalMoves.push_back(m);
  }
}

void ChessActivity::rebuildTargets() {
  targets.clear();
  if (selected < 0) return;
  for (const auto& m : legalMoves)
    if (m.from == selected) targets.push_back(&m);
}

// ---------------------------------------------------------------------------
// Game flow
// ---------------------------------------------------------------------------

void ChessActivity::playMove(const Move& m) {
  applyMove(pos, m);
  selected = -1;
  targets.clear();
  computeLegalMoves();
  updateStatusAfterMove();
}

void ChessActivity::updateStatusAfterMove() {
  if (legalMoves.empty()) {
    gameOver = true;
    if (inCheck(pos, pos.whiteTurn)) {
      statusOverride = pos.whiteTurn ? tr(STR_BLACK_WINS) : tr(STR_WHITE_WINS);
    } else {
      statusOverride = tr(STR_STALEMATE);
    }
    return;
  }
  if (inCheck(pos, pos.whiteTurn)) {
    statusOverride = tr(STR_CHECK);
  } else {
    statusOverride = nullptr;
  }
  if (vsAi && !pos.whiteTurn) aiPending = true;
}

void ChessActivity::handleSquareChosen(const int square) {
  if (gameOver || (vsAi && !pos.whiteTurn)) return;
  const int piece = pos.b[square];

  if (selected < 0) {
    if (piece != EMPTY && isWhite(piece) == pos.whiteTurn) {
      selected = square;
      rebuildTargets();
      requestUpdate();
    }
    return;
  }

  if (square == selected) {
    selected = -1;
    targets.clear();
    requestUpdate();
    return;
  }

  for (const auto* m : targets) {
    if (m->to == square) {
      playMove(*m);
      requestUpdate();
      return;
    }
  }

  // Reselect another own piece, else cancel selection.
  if (piece != EMPTY && isWhite(piece) == pos.whiteTurn) {
    selected = square;
    rebuildTargets();
  } else {
    selected = -1;
    targets.clear();
  }
  requestUpdate();
}

int ChessActivity::evaluate(const Position& p) const {
  int score = 0;
  for (int i = 0; i < 64; i++) {
    const int piece = p.b[i];
    if (piece == EMPTY) continue;
    const int value = PIECE_VALUE[pieceOf(piece)];
    // Small centrality bonus to avoid aimless shuffling.
    const int row = i / 8, col = i % 8;
    const int center = 7 - (std::abs(2 * row - 7) + std::abs(2 * col - 7));
    score += (piece > 0 ? value + center : -(value + center));
  }
  return score;
}

void ChessActivity::runAi() {
  if (gameOver || pos.whiteTurn || legalMoves.empty()) {
    aiPending = false;
    return;
  }
  int bestScore = INT32_MIN;
  const Move* best = nullptr;
  for (const auto& m : legalMoves) {
    Position next = pos;
    applyMove(next, m);
    int score = -evaluate(next);  // black maximizes
    score += static_cast<int>(esp_random() % 30);  // small randomness for variety
    if (score > bestScore) {
      bestScore = score;
      best = &m;
    }
  }
  aiPending = false;
  if (best) playMove(*best);
  requestUpdate();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

int ChessActivity::squareFromTouch(const int x, const int y) const {
  if (squareSize <= 0) return -1;
  if (x < boardX || y < boardY) return -1;
  const int col = (x - boardX) / squareSize;
  const int row = (y - boardY) / squareSize;
  if (col < 0 || col > 7 || row < 0 || row > 7) return -1;
  return row * 8 + col;
}

void ChessActivity::loop() {
  if (aiPending) {
    // One loop pass after the render that announced the AI turn.
    runAi();
    return;
  }

  buttonNavigator.onPressAndContinuous(
      {MappedInputManager::Button::ScreenLeft}, [this] {
        if (cursorCol > 0) cursorCol--;
        requestUpdate();
      });
  buttonNavigator.onPressAndContinuous(
      {MappedInputManager::Button::ScreenRight}, [this] {
        if (cursorCol < 7) cursorCol++;
        requestUpdate();
      });
  buttonNavigator.onPressAndContinuous(
      {MappedInputManager::Button::ScreenUp}, [this] {
        if (cursorRow > 0) cursorRow--;
        requestUpdate();
      });
  buttonNavigator.onPressAndContinuous(
      {MappedInputManager::Button::ScreenDown}, [this] {
        if (cursorRow < 7) cursorRow++;
        requestUpdate();
      });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (selected >= 0) {
      selected = -1;
      targets.clear();
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (gameOver) {
      reset();
      requestUpdate();
    } else {
      handleSquareChosen(cursorRow * 8 + cursorCol);
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
      cursorRow = sq / 8;
      cursorCol = sq % 8;
      handleSquareChosen(sq);
    }
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ChessActivity::drawPiece(const int piece, const int x, const int y, const int size) const {
  const int margin = size / 8;
  const int tx = x + margin;
  const int ty = y + margin;
  const int ts = size - 2 * margin;
  const bool white = piece > 0;

  renderer.fillRoundedRect(tx, ty, ts, ts, ts / 2, white ? White : Black);
  renderer.drawRoundedRect(tx, ty, ts, ts, 2, ts / 2, !white);

  char letter[2] = {PIECE_LETTER[pieceOf(piece)], '\0'};
  const int fontId = UI_12_FONT_ID;
  const int tw = renderer.getTextWidth(fontId, letter);
  const int th = renderer.getTextHeight(fontId);
  renderer.drawText(fontId, tx + (ts - tw) / 2, ty + (ts - th) / 2, letter, !white);
}

void ChessActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight - metrics.topPadding},
                 tr(STR_CHESS), nullptr, true);

  // Status line under the header.
  const int statusY = metrics.headerHeight + 4;
  const char* status = statusOverride;
  if (!status) {
    if (aiPending) {
      status = tr(STR_BLACK_PLAYS);
    } else {
      status = pos.whiteTurn ? tr(STR_WHITE_PLAYS) : tr(STR_BLACK_PLAYS);
    }
  }
  renderer.drawCenteredText(UI_10_FONT_ID, statusY, status);

  // Board geometry: square board centered, below the status line.
  const int top = statusY + renderer.getTextHeight(UI_10_FONT_ID) + 12;
  const int bottomReserve = metrics.buttonHintsHeight + 8;
  const int availH = pageHeight - top - bottomReserve;
  const int availW = pageWidth - 24;
  squareSize = (availW < availH ? availW : availH) / 8;
  boardX = (pageWidth - squareSize * 8) / 2;
  boardY = top + (availH - squareSize * 8) / 2;

  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      const int x = boardX + col * squareSize;
      const int y = boardY + row * squareSize;
      const bool dark = ((row + col) & 1) == 1;
      if (dark) {
        renderer.fillRectDither(x, y, squareSize, squareSize, LightGray);
      } else {
        renderer.fillRect(x, y, squareSize, squareSize, false);  // white
      }
      const int sq = row * 8 + col;
      if (pos.b[sq] != EMPTY) drawPiece(pos.b[sq], x, y, squareSize);
    }
  }
  renderer.drawRect(boardX, boardY, squareSize * 8, squareSize * 8, 2, true);

  // Legal target dots for the selected piece.
  for (const auto* m : targets) {
    const int x = boardX + (m->to % 8) * squareSize;
    const int y = boardY + (m->to / 8) * squareSize;
    const int d = squareSize / 4;
    renderer.fillRoundedRect(x + (squareSize - d) / 2, y + (squareSize - d) / 2, d, d, d / 2, DarkGray);
  }

  // Selection outline.
  if (selected >= 0) {
    renderer.drawRect(boardX + (selected % 8) * squareSize, boardY + (selected / 8) * squareSize, squareSize,
                      squareSize, 3, true);
  }

  // Cursor outline (button navigation).
  renderer.drawRect(boardX + cursorCol * squareSize + 2, boardY + cursorRow * squareSize + 2, squareSize - 4,
                    squareSize - 4, 1, true);

  const auto labels = mappedInput.mapLabels(gameOver ? tr(STR_BACK) : tr(STR_BACK),
                                            gameOver ? tr(STR_NEW_GAME) : tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Periodic half refresh to clear e-ink ghosting.
  const bool halfRefresh = (renderCount % 12) == 0;
  renderCount++;
  renderer.displayBuffer(halfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
