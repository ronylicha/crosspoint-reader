#include "CheckersActivity.h"

#include <GameSession.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <esp_random.h>

#include <algorithm>
#include <cstring>

#include "PieceArt.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int DR[4] = {-1, -1, 1, 1};
constexpr int DC[4] = {-1, 1, -1, 1};
constexpr size_t SESSION_SIZE = 150;
constexpr size_t SESSION_BOARD = 9;
constexpr size_t SESSION_PATH = SESSION_BOARD + 100;
constexpr size_t SESSION_CAPTURES = SESSION_PATH + 21;
static_assert(SESSION_SIZE <= GameSession::MAX_PAYLOAD);

inline bool onBoard(int row, int col) { return row >= 0 && row < 10 && col >= 0 && col < 10; }
}  // namespace

void CheckersActivity::onEnter() {
  Activity::onEnter();
  reset();
  if (!loadSession()) reset();
  requestUpdate();
}

void CheckersActivity::onExit() {
  saveSession();
  Activity::onExit();
}

void CheckersActivity::prepareForSleep() { saveSession(); }

bool CheckersActivity::saveSession() {
  if (!sessionDirty) return true;
  GameSession::Buffer payload{};
  payload[0] = 1;
  payload[1] = (whiteTurn ? 1 : 0) | (gameOver ? 2 : 0) | (aiPlaying ? 4 : 0);
  payload[2] = static_cast<uint8_t>(cursorRow * 10 + cursorCol);
  payload[3] = selected < 0 ? 255 : static_cast<uint8_t>(selected);
  payload[4] = capturePathCount;
  payload[5] = capturedCount;
  payload[6] = aiPathCount;
  payload[7] = aiCapturedCount;
  payload[8] = aiStepIndex;
  memcpy(payload.data() + SESSION_BOARD, turnBoard, sizeof(turnBoard));
  memcpy(payload.data() + SESSION_PATH, aiPlaying ? aiPath : capturePath, MAX_PATH);
  memcpy(payload.data() + SESSION_CAPTURES, aiPlaying ? aiCaptured : capturedSquares, MAX_CAPTURES);
  if (!GameSession::save(GameSession::Game::Checkers, vsAi, payload.data(), SESSION_SIZE)) return false;
  sessionDirty = false;
  return true;
}

bool CheckersActivity::loadSession() {
  GameSession::Buffer payload{};
  if (!GameSession::load(GameSession::Game::Checkers, vsAi, payload.data(), SESSION_SIZE)) return false;
  if (payload[0] != 1 || payload[1] > 7 || payload[2] >= 100 || (payload[3] != 255 && payload[3] >= 100) ||
      payload[4] > MAX_PATH || payload[5] > MAX_CAPTURES || payload[6] > MAX_PATH || payload[7] > MAX_CAPTURES) {
    return false;
  }
  whiteTurn = (payload[1] & 1) != 0;
  gameOver = (payload[1] & 2) != 0;
  aiPlaying = (payload[1] & 4) != 0;
  capturePathCount = payload[4];
  capturedCount = payload[5];
  aiPathCount = payload[6];
  aiCapturedCount = payload[7];
  aiStepIndex = payload[8];
  if (aiPlaying) {
    if (!vsAi || whiteTurn || gameOver || capturePathCount != 0 || capturedCount != 0 || aiPathCount < 2 ||
        aiStepIndex > aiPathCount - 1 || (aiCapturedCount != 0 && aiCapturedCount + 1 != aiPathCount)) {
      return false;
    }
  } else if (aiPathCount != 0 || aiCapturedCount != 0 || aiStepIndex != 0 ||
             (capturePathCount != 0 && (capturePathCount < 2 || capturedCount + 1 != capturePathCount)) ||
             (capturePathCount == 0 && capturedCount != 0) ||
             (capturePathCount != 0 && (gameOver || (vsAi && !whiteTurn)))) {
    return false;
  }
  memcpy(turnBoard, payload.data() + SESSION_BOARD, sizeof(turnBoard));
  int whiteCount = 0, blackCount = 0;
  for (int sq = 0; sq < 100; ++sq) {
    const int p = turnBoard[sq];
    if (p < -2 || p > 2 || (p != 0 && !playable(sq / 10, sq % 10))) return false;
    if (p > 0) ++whiteCount;
    if (p < 0) ++blackCount;
  }
  if (whiteCount > 20 || blackCount > 20) return false;
  genMovesFor(turnBoard, whiteTurn, legalMoves);
  memcpy(b, turnBoard, sizeof(b));
  if (aiPlaying || capturePathCount != 0) {
    int8_t* path = aiPlaying ? aiPath : capturePath;
    int8_t* captured = aiPlaying ? aiCaptured : capturedSquares;
    const uint8_t pathCount = aiPlaying ? aiPathCount : capturePathCount;
    const uint8_t capCount = aiPlaying ? aiCapturedCount : capturedCount;
    memcpy(path, payload.data() + SESSION_PATH, MAX_PATH);
    memcpy(captured, payload.data() + SESSION_CAPTURES, MAX_CAPTURES);
    bool valid = false;
    for (const auto& move : legalMoves) {
      if ((aiPlaying ? move.path.size() == pathCount : move.path.size() > pathCount) &&
          (aiPlaying ? move.captured.size() == capCount : move.captured.size() > capCount) &&
          std::equal(path, path + pathCount, move.path.begin()) &&
          std::equal(captured, captured + capCount, move.captured.begin())) {
        valid = true;
        break;
      }
    }
    if (!valid) return false;
    const uint8_t completed = aiPlaying ? aiStepIndex : capturedCount;
    const int piece = b[path[0]];
    for (uint8_t i = 0; i < completed; ++i) {
      b[path[i]] = 0;
      if (capCount != 0) b[captured[i]] = 0;
      b[path[i + 1]] = piece;
    }
    if (aiPlaying && completed + 1 == pathCount && !isKing(piece)) {
      const int to = path[completed];
      if (to / 10 == 0 || to / 10 == 9) b[to] = piece > 0 ? 2 : -2;
    }
  }
  cursorRow = payload[2] / 10;
  cursorCol = payload[2] % 10;
  lockedPiece = capturePathCount == 0 ? -1 : capturePath[capturePathCount - 1];
  selected = lockedPiece >= 0 ? lockedPiece : (payload[3] == 255 ? -1 : payload[3]);
  statusOverride = nullptr;
  if (aiPlaying) {
    selected = aiPath[aiStepIndex];
    targets.clear();
    aiPending = false;
  } else {
    computeLegalMoves();
    if (selected >= 0 && (b[selected] == 0 || (b[selected] > 0) != whiteTurn)) selected = -1;
    rebuildTargets();
    const bool storedOver = gameOver;
    gameOver = false;
    updateStatusAfterMove();
    if (storedOver != gameOver) return false;
    aiPending = vsAi && !whiteTurn && !gameOver;
  }
  aiNextStepAt = 0;
  sessionDirty = false;
  return true;
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
  aiPlaying = false;
  aiPathCount = 0;
  aiCapturedCount = 0;
  aiStepIndex = 0;
  aiNextStepAt = 0;
  capturePathCount = 0;
  capturedCount = 0;
  confirmRestart = false;
  sessionDirty = true;
  memcpy(turnBoard, b, sizeof(b));
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
  out.reserve(32);

  // Collect captures first (mandatory, and only the longest count).
  std::vector<Move> captures;
  captures.reserve(32);
  std::vector<int8_t> path;
  std::vector<int8_t> captured;
  path.reserve(MAX_PATH);
  captured.reserve(MAX_CAPTURES);
  for (int sq = 0; sq < 100; sq++) {
    const int piece = board[sq];
    if (piece == 0 || (piece > 0) != white) continue;
    path.clear();
    captured.clear();
    path.push_back(static_cast<int8_t>(sq));
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
  genMovesFor(capturePathCount == 0 ? b : turnBoard, whiteTurn, legalMoves);
  if (capturePathCount != 0) {
    legalMoves.erase(
        std::remove_if(legalMoves.begin(), legalMoves.end(),
                       [this](const Move& move) {
                         return move.path.size() <= capturePathCount || move.captured.size() <= capturedCount ||
                                !std::equal(capturePath, capturePath + capturePathCount, move.path.begin()) ||
                                !std::equal(capturedSquares, capturedSquares + capturedCount, move.captured.begin());
                       }),
        legalMoves.end());
  }
}

void CheckersActivity::rebuildTargets() {
  targets.clear();
  targets.reserve(legalMoves.size());
  if (selected < 0) return;
  const size_t current = capturePathCount == 0 ? 0 : capturePathCount - 1;
  for (const auto& m : legalMoves)
    if (m.path.size() > current + 1 && m.path[current] == selected) targets.push_back(&m);
}

// ---------------------------------------------------------------------------
// Game flow
// ---------------------------------------------------------------------------

void CheckersActivity::finishTurn() {
  whiteTurn = !whiteTurn;
  lockedPiece = -1;
  selected = -1;
  capturePathCount = 0;
  capturedCount = 0;
  aiPlaying = false;
  aiPending = false;
  aiPathCount = 0;
  aiCapturedCount = 0;
  aiStepIndex = 0;
  aiNextStepAt = 0;
  targets.clear();
  memcpy(turnBoard, b, sizeof(b));
  computeLegalMoves();
  updateStatusAfterMove();
  sessionDirty = true;
}

void CheckersActivity::playMove(const Move& m) {
  const size_t current = capturePathCount == 0 ? 0 : capturePathCount - 1;
  const int from = m.path[current];
  const int to = m.path.back();
  const int piece = b[from];
  b[from] = 0;
  for (size_t i = capturedCount; i < m.captured.size(); ++i) b[m.captured[i]] = 0;
  b[to] = piece;
  if (!isKing(piece) && (to / 10 == 0 || to / 10 == 9)) b[to] = piece > 0 ? 2 : -2;
  finishTurn();
}

void CheckersActivity::playStep(const Move& m) {
  if (m.captured.empty()) {
    playMove(m);
    return;
  }
  if (capturePathCount == 0) {
    capturePath[0] = m.path.front();
    capturePathCount = 1;
  }
  const int from = m.path[capturePathCount - 1];
  const int to = m.path[capturePathCount];
  const int piece = b[from];
  b[from] = 0;
  b[m.captured[capturedCount]] = 0;
  b[to] = piece;
  capturePath[capturePathCount++] = static_cast<int8_t>(to);
  capturedSquares[capturedCount] = m.captured[capturedCount];
  ++capturedCount;
  if (capturePathCount == m.path.size()) {
    if (!isKing(piece) && (to / 10 == 0 || to / 10 == 9)) b[to] = piece > 0 ? 2 : -2;
    finishTurn();
    return;
  }
  lockedPiece = selected = to;
  cursorRow = to / 10;
  cursorCol = to % 10;
  computeLegalMoves();
  rebuildTargets();
  sessionDirty = true;
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

void CheckersActivity::handleSquareChosen(const int square, const bool complete) {
  if (square < 0 || square >= 100 || gameOver || (vsAi && !whiteTurn)) return;
  const int piece = b[square];
  const size_t next = capturePathCount == 0 ? 1 : capturePathCount;
  if (selected >= 0) {
    if (complete) {
      for (const auto* move : targets) {
        if (move->path.back() == square || square == selected) {
          playMove(*move);
          requestUpdate();
          return;
        }
      }
    } else {
      for (const auto* move : targets) {
        if (move->path[next] == square) {
          playStep(*move);
          requestUpdate();
          return;
        }
      }
      for (const auto* move : targets) {
        if (move->path.back() == square) {
          playMove(*move);
          requestUpdate();
          return;
        }
      }
    }
  }
  if (lockedPiece >= 0) return;
  if (square == selected) {
    selected = -1;
    targets.clear();
  } else if (piece != 0 && (piece > 0) == whiteTurn) {
    selected = square;
    rebuildTargets();
    if (targets.empty()) selected = -1;
  } else {
    selected = -1;
    targets.clear();
  }
  sessionDirty = true;
  requestUpdate();
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

int CheckersActivity::searchBoard(const int8_t board[100], const bool white, const int depth, int alpha, const int beta,
                                  const int ply) {
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
  {
    RenderLock lock(*this);
    if (gameOver || whiteTurn || legalMoves.empty()) {
      aiPending = false;
      return;
    }
    nodes = 0;
    int bestScore = -MATE - 1;
    const Move* bestMove = nullptr;
    uint32_t ties = 0;
    for (const auto& m : legalMoves) {
      int8_t next[100];
      memcpy(next, b, sizeof(next));
      applyOnBoard(next, m);
      const int score = -searchBoard(next, true, AI_DEPTH - 1, -MATE, MATE, 1);
      if (score > bestScore) {
        bestScore = score;
        bestMove = &m;
        ties = 1;
      } else if (score == bestScore && esp_random() % ++ties == 0) {
        bestMove = &m;
      }
    }
    aiPending = false;
    if (!bestMove || bestMove->path.size() > MAX_PATH || bestMove->captured.size() > MAX_CAPTURES) {
      LOG_ERR("CHECKERS", "Invalid AI move sequence");
      return;
    }
    aiPathCount = static_cast<uint8_t>(bestMove->path.size());
    aiCapturedCount = static_cast<uint8_t>(bestMove->captured.size());
    std::copy(bestMove->path.begin(), bestMove->path.end(), aiPath);
    std::copy(bestMove->captured.begin(), bestMove->captured.end(), aiCaptured);
    aiStepIndex = 0;
    aiPlaying = true;
    selected = aiPath[0];
    cursorRow = selected / 10;
    cursorCol = selected % 10;
    targets.clear();
    sessionDirty = true;
  }
  requestUpdateAndWait();
  {
    RenderLock lock(*this);
    aiNextStepAt = millis() + AI_STEP_DELAY_MS;
  }
}

void CheckersActivity::advanceAi() {
  {
    RenderLock lock(*this);
    if (!aiPlaying) return;
    if (aiStepIndex + 1 == aiPathCount) {
      finishTurn();
    } else {
      const int from = aiPath[aiStepIndex];
      const int to = aiPath[aiStepIndex + 1];
      const int piece = b[from];
      b[from] = 0;
      if (aiCapturedCount != 0) b[aiCaptured[aiStepIndex]] = 0;
      b[to] = piece;
      ++aiStepIndex;
      selected = to;
      cursorRow = to / 10;
      cursorCol = to % 10;
      sessionDirty = true;
      if (aiStepIndex + 1 == aiPathCount && !isKing(piece) && (to / 10 == 0 || to / 10 == 9)) {
        b[to] = piece > 0 ? 2 : -2;
      }
    }
  }
  requestUpdateAndWait();
  {
    RenderLock lock(*this);
    if (aiPlaying) aiNextStepAt = millis() + AI_STEP_DELAY_MS;
  }
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
  RenderLock lock(*this);
  const bool ignoreBackRelease = backLongHandled;
  const bool ignoreConfirmRelease = confirmLongHandled;
  const bool backReleased = mappedInput.wasReleased(MappedInputManager::Button::Back) && !ignoreBackRelease;
  const bool confirmReleased = mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !ignoreConfirmRelease;
  if (!mappedInput.isPressed(MappedInputManager::Button::Back)) backLongHandled = false;
  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) confirmLongHandled = false;
  if (!confirmRestart && mappedInput.wasLongPressed(MappedInputManager::Button::Back, 700)) {
    backLongHandled = true;
    confirmRestart = true;
    requestUpdate();
    return;
  }
  int tx = 0, ty = 0;
  const bool tapped = mappedInput.wasScreenTapped(tx, ty);
  if (confirmRestart) {
    const bool cancel = backReleased;
    const bool confirm = confirmReleased;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int y = renderer.getScreenHeight() / 2 + renderer.getTextHeight(UI_10_FONT_ID) * 2;
    const int choiceHeight = std::max(metrics.buttonHintsHeight, renderer.getTextHeight(UI_10_FONT_ID) + 16);
    const bool tappedChoice = tapped && ty >= y && ty < y + choiceHeight;
    if (confirm || (tappedChoice && tx >= renderer.getScreenWidth() / 2)) {
      reset();
      saveSession();
      requestUpdate();
    } else if (cancel || (tappedChoice && tx < renderer.getScreenWidth() / 2)) {
      confirmRestart = false;
      aiNextStepAt = 0;
      requestUpdate();
    }
    return;
  }
  if (tapped && tx >= restartX && tx < restartX + restartW && ty >= restartY && ty < restartY + restartH) {
    confirmRestart = true;
    requestUpdate();
    return;
  }
  if (backReleased) {
    if (selected >= 0 && lockedPiece < 0 && !aiPlaying) {
      selected = -1;
      targets.clear();
      sessionDirty = true;
      requestUpdate();
    } else {
      saveSession();
      lock.unlock();
      finish();
    }
    return;
  }
  if (aiPending) {
    lock.unlock();
    runAi();
    return;
  }
  if (aiPlaying) {
    if (aiNextStepAt == 0) {
      lock.unlock();
      requestUpdateAndWait();
      RenderLock timingLock(*this);
      aiNextStepAt = millis() + AI_STEP_DELAY_MS;
    } else if (static_cast<int32_t>(millis() - aiNextStepAt) >= 0) {
      lock.unlock();
      advanceAi();
    }
    return;
  }
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenLeft}, [this] {
    if (cursorCol > 0) cursorCol--;
    sessionDirty = true;
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenRight}, [this] {
    if (cursorCol < 9) cursorCol++;
    sessionDirty = true;
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenUp}, [this] {
    if (cursorRow > 0) cursorRow--;
    sessionDirty = true;
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenDown}, [this] {
    if (cursorRow < 9) cursorRow++;
    sessionDirty = true;
    requestUpdate();
  });
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 700)) {
    confirmLongHandled = true;
    handleSquareChosen(cursorRow * 10 + cursorCol, true);
    return;
  }
  if (confirmReleased) {
    handleSquareChosen(cursorRow * 10 + cursorCol);
    return;
  }
  if (tapped) {
    const int sq = squareFromTouch(tx, ty);
    if (sq >= 0) {
      cursorRow = sq / 10;
      cursorCol = sq % 10;
      sessionDirty = true;
      handleSquareChosen(sq);
    }
  }
  if (mappedInput.wasScreenLongPress(tx, ty)) {
    const int sq = squareFromTouch(tx, ty);
    if (sq >= 0) handleSquareChosen(sq, true);
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
  // Contrasting outline: black rim on white tokens, white rim on black tokens.
  renderer.drawRoundedRect(tx, ty, ts, ts, 2, ts / 2, white ? Black : White);
  renderer.drawRoundedRect(tx + 2, ty + 2, ts - 4, ts - 4, 1, (ts - 4) / 2, white ? Black : White);
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

  restartW = renderer.getTextWidth(UI_10_FONT_ID, tr(STR_NEW_GAME)) + 16;
  restartH = renderer.getTextHeight(UI_10_FONT_ID) + 10;
  restartX = pageWidth - restartW - 8;
  restartY = metrics.headerHeight + 4;
  renderer.drawRoundedRect(restartX, restartY, restartW, restartH, 1, 4, true);
  renderer.drawText(UI_10_FONT_ID, restartX + 8, restartY + 5, tr(STR_NEW_GAME));

  if (confirmRestart) {
    const int y = pageHeight / 2;
    const int choiceY = y + renderer.getTextHeight(UI_10_FONT_ID) * 2;
    const int choiceHeight = std::max(metrics.buttonHintsHeight, renderer.getTextHeight(UI_10_FONT_ID) + 16);
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_NEW_GAME));
    renderer.drawRoundedRect(8, choiceY, pageWidth / 2 - 16, choiceHeight, 1, 4, true);
    renderer.drawRoundedRect(pageWidth / 2 + 8, choiceY, pageWidth / 2 - 16, choiceHeight, 1, 4, true);
    renderer.drawText(UI_10_FONT_ID, 16, choiceY + 6, tr(STR_CANCEL));
    renderer.drawText(UI_10_FONT_ID, pageWidth / 2 + 16, choiceY + 6, tr(STR_CONFIRM));
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  const int statusY = restartY + restartH + 4;
  const char* status = statusOverride;
  if (!status) status = whiteTurn ? tr(STR_WHITE_PLAYS) : tr(STR_BLACK_PLAYS);
  renderer.drawCenteredText(UI_10_FONT_ID, statusY, status);

  const int hintY = statusY + renderer.getTextHeight(UI_10_FONT_ID) + 4;
  if (selected >= 0 && !aiPlaying && !targets.empty() && !targets.front()->captured.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, hintY, tr(STR_GAME_COMPLETE_CAPTURE_HINT));
  }
  const int top = hintY + renderer.getTextHeight(UI_10_FONT_ID) + 8;
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

  const size_t next = capturePathCount == 0 ? 1 : capturePathCount;
  for (const auto* m : targets) {
    const int to = m->path[next];
    const int x = boardX + (to % 10) * squareSize;
    const int y = boardY + (to / 10) * squareSize;
    const int d = squareSize / 4;
    renderer.fillRoundedRect(x + (squareSize - d) / 2, y + (squareSize - d) / 2, d, d, d / 2, DarkGray);
    const int end = m->path.back();
    if (end != to) {
      renderer.drawRect(boardX + (end % 10) * squareSize + squareSize / 4,
                        boardY + (end / 10) * squareSize + squareSize / 4, squareSize / 2, squareSize / 2, 2, true);
    }
  }

  if (selected >= 0) {
    renderer.drawRect(boardX + (selected % 10) * squareSize, boardY + (selected / 10) * squareSize, squareSize,
                      squareSize, 3, true);
  }

  renderer.drawRect(boardX + cursorCol * squareSize + 2, boardY + cursorRow * squareSize + 2, squareSize - 4,
                    squareSize - 4, 1, true);

  const auto labels = mappedInput.mapLabels(tr(STR_GAME_NEW_GAME_HINT), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const bool halfRefresh = (renderCount % 12) == 0;
  renderCount++;
  renderer.displayBuffer(halfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
