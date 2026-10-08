#include "BackgammonActivity.h"

#include <GameSession.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "PieceArt.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t SESSION_SIZE = 65;
constexpr uint32_t AI_STEP_PAUSE_MS = 800;
constexpr unsigned long RESTART_HOLD_MS = 700;
// Standard starting position, index = point - 1.
constexpr int8_t INITIAL_POINTS[24] = {
    -2, 0, 0, 0, 0,  5,   // 1..6
    0,  3, 0, 0, 0,  -5,  // 7..12
    5,  0, 0, 0, -3, 0,   // 13..18
    -5, 0, 0, 0, 0,  2,   // 19..24
};

inline bool owns(const int8_t v, const bool white) { return white ? v > 0 : v < 0; }
}  // namespace

void BackgammonActivity::onEnter() {
  Activity::onEnter();
  RenderLock lock;
  reset();
  loadSession();
  requestUpdate();
}

void BackgammonActivity::onExit() {
  saveSession();
  Activity::onExit();
}

void BackgammonActivity::prepareForSleep() { saveSession(); }

bool BackgammonActivity::saveSession() {
  if (!sessionDirty) return true;
  uint8_t data[SESSION_SIZE]{};
  data[0] = 1;
  for (int i = 0; i < 24; ++i) data[1 + i] = st.pts[i] + 15;
  for (int i = 0; i < 2; ++i) {
    data[25 + i] = st.bar[i];
    data[27 + i] = st.off[i];
  }
  data[29] = whiteTurn;
  data[30] = phase;
  data[31] = diceCount;
  for (int i = 0; i < diceCount; ++i) data[32 + i] = diceLeft[i];
  data[36] = diceRolledCount;
  for (int i = 0; i < diceRolledCount; ++i) data[37 + i] = diceRolled[i];
  data[41] = gameOver;
  data[42] = whiteWon;
  data[43] = aiPending;
  data[44] = aiPlaying;
  data[45] = aiPlaying ? aiStepCount : 0;
  data[46] = aiPlaying ? aiStepIndex : 0;
  for (int i = 0; i < data[45]; ++i) {
    data[47 + 4 * i] = aiSteps[i].from;
    data[48 + 4 * i] = aiSteps[i].to;
    data[49 + 4 * i] = aiSteps[i].die;
    data[50 + 4 * i] = aiSteps[i].hit;
  }
  data[63] = selected + 1;
  data[64] = cursorPoint;
  if (!GameSession::save(GameSession::Game::Backgammon, vsAi, data, sizeof(data))) return false;
  sessionDirty = false;
  return true;
}

bool BackgammonActivity::loadSession() {
  uint8_t data[SESSION_SIZE]{};
  if (!GameSession::load(GameSession::Game::Backgammon, vsAi, data, sizeof(data))) return false;
  auto invalid = [] {
    LOG_ERR("BACKGAMMON", "Invalid saved session");
    return false;
  };
  if (data[0] != 1 || data[29] > 1 || data[30] > OVER || data[31] > 4 || data[36] > 4 || data[41] > 1 || data[42] > 1 ||
      data[43] > 1 || data[44] > 1 || data[45] > 4 || data[46] > data[45] || data[63] > 25 || data[64] >= 24)
    return invalid();
  State loaded;
  int totals[2]{};
  for (int i = 0; i < 24; ++i) {
    if (data[1 + i] > 30) return invalid();
    loaded.pts[i] = static_cast<int>(data[1 + i]) - 15;
    totals[loaded.pts[i] > 0 ? 0 : 1] += std::abs(static_cast<int>(loaded.pts[i]));
  }
  for (int i = 0; i < 2; ++i) {
    if (data[25 + i] > 15 || data[27 + i] > 15) return invalid();
    loaded.bar[i] = data[25 + i];
    loaded.off[i] = data[27 + i];
    if (totals[i] + loaded.bar[i] + loaded.off[i] != 15) return invalid();
  }
  if ((data[30] == OVER) != static_cast<bool>(data[41]) ||
      (loaded.off[0] == 15 || loaded.off[1] == 15) != static_cast<bool>(data[41]) ||
      (data[41] && (loaded.off[data[42] ? 0 : 1] != 15 || data[29] != data[42])) ||
      (loaded.off[0] == 15 && loaded.off[1] == 15))
    return invalid();
  if (data[30] == ROLL) {
    if (data[31] != 0 || data[36] != 0 || data[44]) return invalid();
  } else {
    if ((data[36] != 2 && data[36] != 4) || data[31] > data[36]) return invalid();
    uint8_t available[7]{};
    for (int i = 0; i < data[36]; ++i) {
      const uint8_t die = data[37 + i];
      if (die < 1 || die > 6) return invalid();
      if (data[36] == 4 && die != data[37]) return invalid();
      available[die]++;
    }
    if (data[36] == 2 && data[37] == data[38]) return invalid();
    for (int i = 0; i < data[31]; ++i) {
      const uint8_t die = data[32 + i];
      if (die < 1 || die > 6 || available[die] == 0) return invalid();
      available[die]--;
    }
    if (data[30] == MOVE && data[31] == 0 && !(data[44] && data[46] == data[45])) return invalid();
  }
  if ((data[43] || data[44]) && (!vsAi || data[29] || data[41])) return invalid();
  if (data[43] && data[44]) return invalid();
  if (vsAi && !data[29] && !data[41] && !data[43] && !data[44]) return invalid();
  if (!data[44] && (data[45] != 0 || data[46] != 0)) return invalid();
  if (data[44] && (data[30] != MOVE || data[45] - data[46] > data[31])) return invalid();
  if (data[63] != 0) {
    const int source = data[63] - 1;
    if (data[30] != MOVE || data[44] ||
        (source == SRC_BAR ? loaded.bar[data[29] ? 0 : 1] == 0 : !owns(loaded.pts[source], data[29])))
      return invalid();
  }
  // A saved playback must still be a maximal legal continuation of this board.
  if (data[44]) {
    State replay = loaded;
    uint8_t rest[4]{};
    int restCount = data[31];
    for (int i = 0; i < restCount; ++i) rest[i] = data[32 + i];
    for (int i = 0; i < data[45]; ++i) {
      if (data[47 + 4 * i] > SRC_BAR || data[48 + 4 * i] > DST_OFF || data[49 + 4 * i] < 1 || data[49 + 4 * i] > 6 ||
          data[50 + 4 * i] > 1)
        return invalid();
      if (i < data[46]) continue;
      std::vector<Step> legal;
      legalFirstSteps(replay, false, rest, restCount, legal);
      bool found = false;
      for (const auto& step : legal) {
        if (step.from != data[47 + 4 * i] || step.to != data[48 + 4 * i] || step.die != data[49 + 4 * i] ||
            step.hit != static_cast<bool>(data[50 + 4 * i]))
          continue;
        applyStep(replay, false, step);
        for (int d = 0; d < restCount; ++d) {
          if (rest[d] != step.die) continue;
          for (int j = d; j + 1 < restCount; ++j) rest[j] = rest[j + 1];
          --restCount;
          break;
        }
        found = true;
        break;
      }
      if (!found) return invalid();
    }
    if (replay.off[1] != 15 && maxUsable(replay, false, rest, restCount) != 0) return invalid();
  }
  st = loaded;
  whiteTurn = data[29];
  phase = static_cast<Phase>(data[30]);
  diceCount = data[31];
  std::memcpy(diceLeft, data + 32, sizeof(diceLeft));
  diceRolledCount = data[36];
  std::memcpy(diceRolled, data + 37, sizeof(diceRolled));
  gameOver = data[41];
  whiteWon = data[42];
  aiPending = data[43];
  aiPlaying = data[44];
  aiStepCount = data[45];
  aiStepIndex = data[46];
  for (int i = 0; i < aiStepCount; ++i)
    aiSteps[i] = Step{static_cast<int8_t>(data[47 + 4 * i]), static_cast<int8_t>(data[48 + 4 * i]), data[49 + 4 * i],
                      static_cast<bool>(data[50 + 4 * i])};
  selected = static_cast<int>(data[63]) - 1;
  cursorPoint = data[64];
  aiNextStepAt = 0;
  sessionDirty = false;
  return true;
}

void BackgammonActivity::reset() {
  for (int i = 0; i < 24; i++) st.pts[i] = INITIAL_POINTS[i];
  st.bar[0] = st.bar[1] = 0;
  st.off[0] = st.off[1] = 0;
  whiteTurn = true;
  phase = ROLL;
  diceCount = 0;
  diceRolledCount = 0;
  selected = -1;
  cursorPoint = 12;
  gameOver = false;
  whiteWon = false;
  statusOverride = nullptr;
  aiPending = false;
  aiPlaying = false;
  aiStepCount = aiStepIndex = 0;
  aiNextStepAt = 0;
  confirmRestart = false;
  sessionDirty = true;
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

bool BackgammonActivity::canBearOff(const State& s, const bool white) {
  if (s.bar[white ? 0 : 1] > 0) return false;
  // White home = points 1..6 (idx 0..5), black home = 19..24 (idx 18..23).
  const int lo = white ? 6 : 0;
  const int hi = white ? 23 : 17;
  for (int i = lo; i <= hi; i++)
    if (owns(s.pts[i], white)) return false;
  return true;
}

void BackgammonActivity::genSteps(const State& s, const bool white, const uint8_t die, std::vector<Step>& out) {
  out.clear();
  out.reserve(24);
  const int8_t enemyBlot = white ? -1 : 1;

  auto landingOk = [&](int idx) {
    const int8_t v = s.pts[idx];
    return v == 0 || owns(v, white) || v == enemyBlot;
  };
  auto push = [&](int from, int to) {
    Step m;
    m.from = from;
    m.to = to;
    m.die = die;
    m.hit = to < 24 && s.pts[to] == enemyBlot;
    out.push_back(m);
  };

  // Bar entry first: mandatory.
  if (s.bar[white ? 0 : 1] > 0) {
    const int idx = white ? (24 - die) : (die - 1);  // white enters 24..19, black 1..6
    if (landingOk(idx)) push(SRC_BAR, idx);
    return;
  }

  for (int i = 0; i < 24; i++) {
    if (!owns(s.pts[i], white)) continue;
    if (white) {
      const int t = i - die;
      if (t >= 0) {
        if (landingOk(t)) push(i, t);
      } else if (canBearOff(s, true)) {
        const int p = i + 1;  // point number; exact hit when die == p
        if (static_cast<int>(die) == p) {
          push(i, DST_OFF);
        } else if (static_cast<int>(die) > p) {
          // Higher die allowed only if nothing sits on a higher point.
          bool highest = true;
          for (int j = i + 1; j < 6; j++)
            if (s.pts[j] > 0) highest = false;
          if (highest) push(i, DST_OFF);
        }
      }
    } else {
      const int t = i + die;
      if (t <= 23) {
        if (landingOk(t)) push(i, t);
      } else if (canBearOff(s, false)) {
        const int dist = 24 - i;  // distance to bear off for black
        if (static_cast<int>(die) == dist) {
          push(i, DST_OFF);
        } else if (static_cast<int>(die) > dist) {
          bool farthest = true;
          for (int j = 18; j < i; j++)
            if (s.pts[j] < 0) farthest = false;
          if (farthest) push(i, DST_OFF);
        }
      }
    }
  }
}

void BackgammonActivity::applyStep(State& s, const bool white, const Step& m) {
  const int me = white ? 0 : 1;
  const int opp = 1 - me;
  if (m.from == SRC_BAR) {
    s.bar[me]--;
  } else {
    s.pts[m.from] += white ? -1 : 1;
  }
  if (m.to == DST_OFF) {
    s.off[me]++;
    return;
  }
  if (m.hit) {
    s.pts[m.to] = 0;
    s.bar[opp]++;
  }
  s.pts[m.to] += white ? 1 : -1;
}

int BackgammonActivity::pipCount(const State& s, const bool white) {
  int pip = 25 * s.bar[white ? 0 : 1];
  for (int i = 0; i < 24; i++) {
    const int v = s.pts[i];
    if (white && v > 0) pip += v * (i + 1);
    if (!white && v < 0) pip += -v * (24 - i);
  }
  return pip;
}

int BackgammonActivity::maxUsable(const State& s, const bool white, const uint8_t* dice, const int count) {
  if (count <= 0) return 0;
  int best = 0;
  for (int i = 0; i < count; i++) {
    const uint8_t d = dice[i];
    bool seen = false;
    for (int j = 0; j < i; j++)
      if (dice[j] == d) seen = true;
    if (seen) continue;
    std::vector<Step> steps;
    genSteps(s, white, d, steps);
    for (const auto& m : steps) {
      State next = s;
      applyStep(next, white, m);
      uint8_t rest[4];
      int n = 0;
      for (int j = 0; j < count; j++)
        if (j != i) rest[n++] = dice[j];
      const int total = 1 + maxUsable(next, white, rest, n);
      if (total > best) best = total;
      if (best == count) return best;
    }
  }
  return best;
}

void BackgammonActivity::legalFirstSteps(const State& s, const bool white, const uint8_t* dice, const int count,
                                         std::vector<Step>& out) {
  out.clear();
  out.reserve(48);
  if (count <= 0) return;
  const int global = maxUsable(s, white, dice, count);
  if (global == 0) return;

  for (int i = 0; i < count; i++) {
    const uint8_t d = dice[i];
    bool seen = false;
    for (int j = 0; j < i; j++)
      if (dice[j] == d) seen = true;
    if (seen) continue;
    std::vector<Step> steps;
    genSteps(s, white, d, steps);
    for (const auto& m : steps) {
      State next = s;
      applyStep(next, white, m);
      uint8_t rest[4];
      int n = 0;
      for (int j = 0; j < count; j++)
        if (j != i) rest[n++] = dice[j];
      if (1 + maxUsable(next, white, rest, n) == global) out.push_back(m);
    }
  }

  // Mixed roll where only one die can be played: the higher die is forced.
  if (global == 1 && count == 2 && dice[0] != dice[1]) {
    const uint8_t hi = dice[0] > dice[1] ? dice[0] : dice[1];
    if (std::any_of(out.begin(), out.end(), [hi](const Step& m) { return m.die == hi; }))
      out.erase(std::remove_if(out.begin(), out.end(), [hi](const Step& m) { return m.die != hi; }), out.end());
  }
}

// ---------------------------------------------------------------------------
// Flow
// ---------------------------------------------------------------------------

void BackgammonActivity::rollDice() {
  statusOverride = nullptr;
  const uint8_t d1 = 1 + esp_random() % 6;
  const uint8_t d2 = 1 + esp_random() % 6;
  diceCount = 0;
  if (d1 == d2) {
    for (int i = 0; i < 4; i++) diceLeft[diceCount++] = d1;
  } else {
    diceLeft[diceCount++] = d1;
    diceLeft[diceCount++] = d2;
  }
  for (int i = 0; i < diceCount; i++) diceRolled[i] = diceLeft[i];
  diceRolledCount = diceCount;
  phase = MOVE;
  sessionDirty = true;
  selected = -1;
  if (!anyStepAvailable()) {
    statusOverride = tr(STR_NO_MOVE);
    diceCount = 0;
    endTurn();
  }
  saveSession();
  requestUpdate();
}

void BackgammonActivity::consumeDie(const uint8_t die) {
  for (int i = 0; i < diceCount; i++) {
    if (diceLeft[i] == die) {
      for (int j = i; j < diceCount - 1; j++) diceLeft[j] = diceLeft[j + 1];
      diceCount--;
      return;
    }
  }
}

bool BackgammonActivity::anyStepAvailable() const {
  std::vector<Step> steps;
  legalFirstSteps(st, whiteTurn, diceLeft, diceCount, steps);
  return !steps.empty();
}

void BackgammonActivity::endTurn() {
  selected = -1;
  whiteTurn = !whiteTurn;
  phase = ROLL;
  diceCount = 0;
  diceRolledCount = 0;
  aiPlaying = false;
  aiStepCount = aiStepIndex = 0;
  aiNextStepAt = 0;
  aiPending = vsAi && !whiteTurn && !gameOver;
  sessionDirty = true;
}

void BackgammonActivity::handlePointChosen(const int point) {
  if (gameOver || (vsAi && !whiteTurn)) return;
  if (phase == ROLL) {
    rollDice();
    return;
  }
  if (phase != MOVE || gameOver || (vsAi && !whiteTurn)) return;

  const int me = whiteTurn ? 0 : 1;

  if (selected < 0) {
    // Bar checkers must enter first: force the bar as source.
    if (st.bar[me] > 0) {
      selected = SRC_BAR;
      sessionDirty = true;
      requestUpdate();
      return;
    }
    if (point >= 0 && point < 24 && owns(st.pts[point], whiteTurn)) {
      // Only allow selecting a checker that actually has a legal step.
      std::vector<Step> steps;
      legalFirstSteps(st, whiteTurn, diceLeft, diceCount, steps);
      for (const auto& m : steps) {
        if (m.from == point) {
          selected = point;
          sessionDirty = true;
          break;
        }
      }
      requestUpdate();
    }
    return;
  }

  if (point == selected) {
    selected = -1;
    sessionDirty = true;
    requestUpdate();
    return;
  }

  Step path[4]{};
  int count = 0;
  if (findSequence(selected, point, path, count)) {
    for (int i = 0; i < count; ++i) {
      applyStep(st, whiteTurn, path[i]);
      consumeDie(path[i].die);
    }
    sessionDirty = true;
    selected = -1;
    if (st.off[me] == 15) {
      gameOver = true;
      whiteWon = whiteTurn;
      phase = OVER;
      aiPending = false;
    } else if (diceCount == 0) {
      endTurn();
    } else if (!anyStepAvailable()) {
      statusOverride = tr(STR_NO_MOVE);
      diceCount = 0;
      endTurn();
    }
    saveSession();
    requestUpdate();
    return;
  }

  // Reselect another own checker, else cancel.
  if (st.bar[me] == 0 && point >= 0 && point < 24 && owns(st.pts[point], whiteTurn)) {
    selected = point;
  } else {
    selected = -1;
  }
  sessionDirty = true;
  requestUpdate();
}

bool BackgammonActivity::findSequence(const int source, const int destination, Step out[4], int& count) const {
  count = 0;
  if (source < 0 || source > SRC_BAR || destination < 0 || destination > DST_OFF || phase != MOVE) return false;
  return findSequenceFrom(st, whiteTurn, diceLeft, diceCount, source, destination, out, count, 0);
}

bool BackgammonActivity::findSequenceFrom(const State& s, const bool white, const uint8_t* dice, const int count,
                                          const int source, const int destination, Step out[4], int& outCount,
                                          const int depth) {
  if (count <= 0 || depth >= 4) return false;
  std::vector<Step> legal;
  legalFirstSteps(s, white, dice, count, legal);
  for (const auto& step : legal) {
    if (step.from == source && step.to == destination) {
      out[depth] = step;
      outCount = depth + 1;
      return true;
    }
  }
  for (const auto& step : legal) {
    if (step.from != source || step.to == DST_OFF) continue;
    State next = s;
    applyStep(next, white, step);
    uint8_t rest[4]{};
    int restCount = 0;
    bool removed = false;
    for (int i = 0; i < count; ++i) {
      if (!removed && dice[i] == step.die) {
        removed = true;
      } else {
        rest[restCount++] = dice[i];
      }
    }
    out[depth] = step;
    if (findSequenceFrom(next, white, rest, restCount, step.to, destination, out, outCount, depth + 1)) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// AI: enumerate every fully legal move sequence for the roll, score the ends.
// ---------------------------------------------------------------------------

int BackgammonActivity::evaluate(const State& s) {
  int score = (pipCount(s, false) - pipCount(s, true)) * 8;
  for (int i = 0; i < 24; i++) {
    const int v = s.pts[i];
    if (v == 1) score -= 12;                      // white blot
    if (v >= 2) score += 15 + (i < 6 ? 8 : 0);    // white made point
    if (v == -1) score += 12;                     // black blot
    if (v <= -2) score -= 15 + (i > 17 ? 8 : 0);  // black made point
  }
  score += 25 * (s.bar[1] - s.bar[0]);
  score += 30 * (s.off[0] - s.off[1]);
  return score;
}

void BackgammonActivity::dfsSequences(const State& s, const bool white, const uint8_t* dice, const int count,
                                      AiCtx& ctx, const int depth) {
  if (ctx.nodes > 20000) return;
  ctx.nodes++;
  if ((ctx.nodes & 511) == 0) vTaskDelay(1);
  std::vector<Step> steps;
  legalFirstSteps(s, white, dice, count, steps);
  if (steps.empty() || count == 0 || s.off[white ? 0 : 1] == 15) {
    const int score = evaluate(s);
    if (!ctx.hasBest || (white ? score > ctx.bestScore : score < ctx.bestScore)) {
      ctx.hasBest = true;
      ctx.bestScore = score;
      ctx.bestCount = depth;
      for (int i = 0; i < depth; ++i) ctx.bestSteps[i] = ctx.path[i];
    }
    return;
  }
  if (depth >= 4) return;
  for (const auto& m : steps) {
    State next = s;
    applyStep(next, white, m);
    uint8_t rest[4];
    int n = 0;
    bool removed = false;
    for (int j = 0; j < count; j++) {
      if (!removed && dice[j] == m.die) {
        removed = true;
        continue;
      }
      rest[n++] = dice[j];
    }
    ctx.path[depth] = m;
    dfsSequences(next, white, rest, n, ctx, depth + 1);
  }
}

void BackgammonActivity::runAi() {
  RenderLock lock;
  if (gameOver || whiteTurn) {
    aiPending = false;
    return;
  }
  statusOverride = nullptr;
  if (phase == ROLL) {
    const uint8_t d1 = 1 + esp_random() % 6;
    const uint8_t d2 = 1 + esp_random() % 6;
    diceCount = d1 == d2 ? 4 : 2;
    diceLeft[0] = d1;
    diceLeft[1] = d2;
    if (diceCount == 4) diceLeft[2] = diceLeft[3] = d1;
    for (int i = 0; i < diceCount; ++i) diceRolled[i] = diceLeft[i];
    diceRolledCount = diceCount;
    phase = MOVE;
  }
  AiCtx ctx;
  dfsSequences(st, false, diceLeft, diceCount, ctx);
  if (!ctx.hasBest) {
    LOG_ERR("BACKGAMMON", "AI search found no legal continuation");
    return;
  }
  aiStepCount = ctx.bestCount;
  aiStepIndex = 0;
  for (int i = 0; i < aiStepCount; ++i) aiSteps[i] = ctx.bestSteps[i];
  aiPending = false;
  aiPlaying = true;
  selected = -1;
  if (aiStepCount == 0) statusOverride = tr(STR_NO_MOVE);
  sessionDirty = true;
  saveSession();
  lock.unlock();
  requestUpdateAndWait();
  aiNextStepAt = millis() + AI_STEP_PAUSE_MS;
}

void BackgammonActivity::advanceAi() {
  if (!aiPlaying) return;
  if (aiNextStepAt == 0) {
    requestUpdateAndWait();
    aiNextStepAt = millis() + AI_STEP_PAUSE_MS;
    return;
  }
  if (static_cast<int32_t>(millis() - aiNextStepAt) < 0) return;
  RenderLock lock;
  if (aiStepIndex == aiStepCount) {
    endTurn();
    saveSession();
    requestUpdate();
    return;
  }
  const Step step = aiSteps[aiStepIndex++];
  applyStep(st, false, step);
  consumeDie(step.die);
  if (st.off[1] == 15) {
    gameOver = true;
    whiteWon = false;
    phase = OVER;
    aiPlaying = false;
    aiStepCount = aiStepIndex = 0;
  }
  sessionDirty = true;
  saveSession();
  lock.unlock();
  requestUpdateAndWait();
  aiNextStepAt = millis() + AI_STEP_PAUSE_MS;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

int BackgammonActivity::pointFromTouch(const int x, const int y) const {
  if (boardW <= 0 || x < boardX || x >= boardX + boardW || y < boardY || y >= boardY + boardH) return -1;
  const int relX = x - boardX;
  int col;
  if (relX < 6 * pointW) {
    col = relX / pointW;
  } else if (relX < 6 * pointW + barW) {
    return SRC_BAR;
  } else {
    col = 6 + (relX - 6 * pointW - barW) / pointW;
  }
  if (col < 0 || col > 11) return -1;
  const bool top = (y - boardY) < boardH / 2;
  return top ? 12 + col : 11 - col;
}

void BackgammonActivity::loop() {
  RenderLock lock;
  int tx = 0, ty = 0;
  const bool tapped = mappedInput.wasScreenTapped(tx, ty);
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backLongHandled = false;
  if (!backLongHandled && mappedInput.isPressed(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= RESTART_HOLD_MS) {
    backLongHandled = true;
    confirmRestart = true;
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && backLongHandled) {
    backLongHandled = false;
    return;
  }

  if (confirmRestart) {
    const int width = renderer.getScreenWidth();
    const int height = renderer.getScreenHeight();
    const bool dialogTap = tapped && tx >= 24 && tx < width - 24 && ty >= height / 2 + 8 && ty < height / 2 + 48;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || (dialogTap && tx >= width / 2)) {
      reset();
      saveSession();
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back) || (dialogTap && tx < width / 2)) {
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (selected >= 0) {
      selected = -1;
      sessionDirty = true;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }

  if (aiPlaying) {
    lock.unlock();
    advanceAi();
    return;
  }
  if (aiPending) {
    lock.unlock();
    runAi();
    return;
  }

  auto colOf = [](int idx) { return idx >= 12 ? idx - 12 : 11 - idx; };
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenLeft}, [this, colOf] {
    int col = colOf(cursorPoint);
    if (col > 0) col--;
    cursorPoint = cursorPoint >= 12 ? 12 + col : 11 - col;
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenRight}, [this, colOf] {
    int col = colOf(cursorPoint);
    if (col < 11) col++;
    cursorPoint = cursorPoint >= 12 ? 12 + col : 11 - col;
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenUp}, [this, colOf] {
    if (cursorPoint < 12) cursorPoint = 12 + colOf(cursorPoint);
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::ScreenDown}, [this, colOf] {
    if (cursorPoint >= 12) cursorPoint = 11 - colOf(cursorPoint);
    requestUpdate();
  });

  if (gameOver) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Confirm the selected source again to bear off when a legal route exists.
    Step path[4]{};
    int count = 0;
    if (selected >= 0 && cursorPoint == selected && findSequence(selected, DST_OFF, path, count)) {
      handlePointChosen(DST_OFF);
    } else {
      handlePointChosen(cursorPoint);
    }
    return;
  }

  if (tapped) {
    const int p = pointFromTouch(tx, ty);
    if (phase == MOVE && selected >= 0 && selected != SRC_BAR && p == SRC_BAR) {
      const bool ownHalf = whiteTurn ? ty > boardY + boardH / 2 : ty < boardY + boardH / 2;
      if (ownHalf) {
        handlePointChosen(DST_OFF);
        return;
      }
    }
    if (p >= 0) {
      if (p != SRC_BAR) cursorPoint = p;
      handlePointChosen(p);
    } else if (phase == ROLL) {
      rollDice();
    }
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void BackgammonActivity::drawChecker(const int cx, const int cy, const int r, const bool white) const {
  // 12-gon disk with a contrasting rim: black on white checkers, white on black.
  static constexpr int DX[12] = {100, 87, 50, 0, -50, -87, -100, -87, -50, 0, 50, 87};
  static constexpr int DY[12] = {0, 50, 87, 100, 87, 50, 0, -50, -87, -100, -87, -50};
  int xs[12], ys[12];
  for (int i = 0; i < 12; i++) {
    xs[i] = cx + r * DX[i] / 100;
    ys[i] = cy + r * DY[i] / 100;
  }
  renderer.fillPolygon(xs, ys, 12, !white);
  for (int i = 0; i < 12; i++) {
    const int j = (i + 1) % 12;
    renderer.drawLine(xs[i], ys[i], xs[j], ys[j], 2, white);
  }
}

void BackgammonActivity::drawDice() const {
  if (diceRolledCount == 0) return;
  // The full roll is shown; dice already played are greyed out, dice still to
  // play are solid with crisp pips.
  const int box = 40;
  const int gap = 10;
  const int totalW = diceRolledCount * box + (diceRolledCount - 1) * gap;
  const int x0 = boardX + (boardW - totalW) / 2;
  const int y0 = boardY + boardH / 2 - box / 2;
  static constexpr int PIP[6][6] = {
      {4, -1, -1, -1, -1, -1},  // 1
      {0, 8, -1, -1, -1, -1},   // 2
      {0, 4, 8, -1, -1, -1},    // 3
      {0, 2, 6, 8, -1, -1},     // 4
      {0, 2, 4, 6, 8, -1},      // 5
      {0, 2, 3, 5, 6, 8},       // 6
  };
  // Count how many of each value remain to play.
  int left[7] = {};
  for (int i = 0; i < diceCount; i++) left[diceLeft[i]]++;

  for (int d = 0; d < diceRolledCount; d++) {
    const int v = diceRolled[d];
    const int x = x0 + d * (box + gap);
    const bool spent = left[v] > 0 ? (left[v]--, false) : true;
    renderer.fillRoundedRect(x, y0, box, box, 6, spent ? LightGray : White);
    renderer.drawRoundedRect(x, y0, box, box, spent ? 2 : 3, 6, Black);
    for (int i = 0; i < 6 && PIP[v - 1][i] >= 0; i++) {
      const int gx = PIP[v - 1][i] % 3;
      const int gy = PIP[v - 1][i] / 3;
      gameart::disk(renderer, gx * 50, gy * 50, spent ? 16 : 20, x + box / 10, y0 + box / 10, box * 8 / 10, true);
    }
    if (spent) renderer.drawLine(x + 8, y0 + box + 3, x + box - 8, y0 + box + 3, 2, true);
  }
}

void BackgammonActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight - metrics.topPadding},
                 tr(STR_BACKGAMMON), nullptr, true);

  const int statusY = metrics.headerHeight + 4;
  const char* status = statusOverride;
  if (!status) {
    if (gameOver) {
      status = whiteWon ? tr(STR_WHITE_WINS) : tr(STR_BLACK_WINS);
    } else if (phase == ROLL) {
      status = tr(STR_ROLL_DICE);
    } else {
      status = whiteTurn ? tr(STR_WHITE_PLAYS) : tr(STR_BLACK_PLAYS);
    }
  }
  renderer.drawCenteredText(UI_10_FONT_ID, statusY, status);

  restartW = renderer.getTextWidth(UI_10_FONT_ID, tr(STR_NEW_GAME)) + 24;
  restartH = renderer.getTextHeight(UI_10_FONT_ID) + 12;
  restartX = (pageWidth - restartW) / 2;
  restartY = statusY + renderer.getTextHeight(UI_10_FONT_ID) + 4;
  renderer.drawRoundedRect(restartX, restartY, restartW, restartH, 2, 4, Black);
  renderer.drawCenteredText(UI_10_FONT_ID, restartY + 6, tr(STR_NEW_GAME));

  const int top = restartY + restartH + 8;
  const int bottomReserve = metrics.buttonHintsHeight + renderer.getTextHeight(UI_10_FONT_ID) + 12;
  boardX = 8;
  boardW = pageWidth - 16;
  boardY = top;
  boardH = pageHeight - top - bottomReserve;
  barW = boardW / 10;
  pointW = (boardW - barW) / 12;
  const int triH = (boardH - 64) / 2;
  const int r = pointW / 2 - 1;
  diceZoneY = boardY + triH;
  diceZoneH = boardH - 2 * triH;

  // Frame and bar.
  renderer.drawRect(boardX, boardY, boardW, boardH, 2, true);
  renderer.fillRect(boardX + 6 * pointW, boardY, barW, boardH, true);

  // Points: top row idx 12..23 (col = idx-12), bottom row idx 0..11 (col = 11-idx).
  for (int idx = 0; idx < 24; idx++) {
    const bool topRow = idx >= 12;
    const int col = topRow ? idx - 12 : 11 - idx;
    const int x = boardX + col * pointW + (col >= 6 ? barW : 0);
    const bool dark = (col & 1) == 0;
    int xs[3], ys[3];
    if (topRow) {
      xs[0] = x;
      ys[0] = boardY;
      xs[1] = x + pointW;
      ys[1] = boardY;
      xs[2] = x + pointW / 2;
      ys[2] = boardY + triH;
    } else {
      xs[0] = x;
      ys[0] = boardY + boardH;
      xs[1] = x + pointW;
      ys[1] = boardY + boardH;
      xs[2] = x + pointW / 2;
      ys[2] = boardY + boardH - triH;
    }
    if (dark) {
      renderer.fillPolygon(xs, ys, 3, true);
    } else {
      renderer.drawLine(xs[0], ys[0], xs[2], ys[2], 2, true);
      renderer.drawLine(xs[1], ys[1], xs[2], ys[2], 2, true);
    }

    // Checkers on the point: stacked inside the triangle height; when there
    // are too many, they overlap (straddled) instead of spilling out.
    const int v = st.pts[idx];
    const int count = v > 0 ? v : -v;
    const bool white = v > 0;
    const int cx = x + pointW / 2;
    const int avail = triH - 2 * r - 6;
    const int step = count > 1 ? std::min(2 * r + 1, avail / (count - 1)) : 0;
    for (int k = count - 1; k >= 0; k--) {
      const int cy = topRow ? boardY + r + 2 + k * step : boardY + boardH - r - 2 - k * step;
      drawChecker(cx, cy, r, white);
    }

    // Selection / cursor outlines on the column.
    if (idx == selected) {
      renderer.drawRect(x + 1, topRow ? boardY + 1 : boardY + boardH - triH - 1, pointW - 2, triH - 2, 3, true);
    }
    if (idx == cursorPoint) {
      renderer.drawRect(x + 3, topRow ? boardY + 3 : boardY + boardH - triH + 1, pointW - 6, triH - 6, 1, true);
    }
  }

  // Bar checkers (stacked from each end, compressed if many).
  const int barAvail = triH - 2 * r - 6;
  const int wStep = st.bar[0] > 1 ? std::min(2 * r + 1, barAvail / (st.bar[0] - 1)) : 0;
  for (int k = st.bar[0] - 1; k >= 0; k--) {
    drawChecker(boardX + 6 * pointW + barW / 2, boardY + boardH - r - 2 - k * wStep, r, true);
  }
  const int bStep = st.bar[1] > 1 ? std::min(2 * r + 1, barAvail / (st.bar[1] - 1)) : 0;
  for (int k = st.bar[1] - 1; k >= 0; k--) {
    drawChecker(boardX + 6 * pointW + barW / 2, boardY + r + 2 + k * bStep, r, false);
  }
  if (selected == SRC_BAR) {
    renderer.drawRect(boardX + 6 * pointW + 2, boardY + boardH / 2 - triH / 2, barW - 4, triH, 3, true);
  }

  // Borne-off counts in the center band.
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", st.off[0]);
    renderer.drawCenteredText(UI_10_FONT_ID, diceZoneY + diceZoneH - renderer.getTextHeight(UI_10_FONT_ID) - 4, buf);
    snprintf(buf, sizeof(buf), "%d", st.off[1]);
    renderer.drawCenteredText(UI_10_FONT_ID, diceZoneY + 4, buf);
  }

  // Each marker represents a legal route of one or more dice for this checker.
  if (selected >= 0 && phase == MOVE) {
    for (int destination = 0; destination <= DST_OFF; ++destination) {
      Step route[4]{};
      int count = 0;
      if (!findSequence(selected, destination, route, count)) continue;
      if (destination == DST_OFF) {
        // Dot on the bar column, own half.
        const int cy = whiteTurn ? boardY + boardH / 2 + 20 : boardY + boardH / 2 - 20;
        gameart::disk(renderer, 0, 0, 10, boardX + 6 * pointW + barW / 2 - 10, cy - 10, 20, false);
      } else {
        const bool topRow = destination >= 12;
        const int col = topRow ? destination - 12 : 11 - destination;
        const int x = boardX + col * pointW + (col >= 6 ? barW : 0);
        const int cx = x + pointW / 2;
        const int cy = topRow ? boardY + triH - 10 : boardY + boardH - triH + 10;
        renderer.fillRect(cx - 10, cy - 10, 20, 20, false);
        gameart::disk(renderer, 0, 0, 8, cx - 10, cy - 10, 20, true);
      }
    }
  }

  drawDice();

  renderer.drawCenteredText(UI_10_FONT_ID,
                            pageHeight - metrics.buttonHintsHeight - renderer.getTextHeight(UI_10_FONT_ID) - 4,
                            tr(STR_GAME_NEW_GAME_HINT));
  if (confirmRestart) {
    const int y = pageHeight / 2 - 56;
    renderer.fillRoundedRect(16, y, pageWidth - 32, 112, 8, White);
    renderer.drawRoundedRect(16, y, pageWidth - 32, 112, 3, 8, Black);
    renderer.drawCenteredText(UI_10_FONT_ID, y + 20, tr(STR_NEW_GAME));
    renderer.drawRect(24, y + 64, pageWidth / 2 - 24, 40, 2, true);
    renderer.drawRect(pageWidth / 2, y + 64, pageWidth / 2 - 24, 40, 2, true);
    renderer.drawText(UI_10_FONT_ID, pageWidth / 4 - renderer.getTextWidth(UI_10_FONT_ID, tr(STR_CANCEL)) / 2, y + 74,
                      tr(STR_CANCEL));
    renderer.drawText(UI_10_FONT_ID, 3 * pageWidth / 4 - renderer.getTextWidth(UI_10_FONT_ID, tr(STR_CONFIRM)) / 2,
                      y + 74, tr(STR_CONFIRM));
  }
  const auto labels = mappedInput.mapLabels(confirmRestart ? tr(STR_CANCEL) : tr(STR_BACK),
                                            confirmRestart ? tr(STR_CONFIRM) : tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const bool halfRefresh = (renderCount % 12) == 0;
  renderCount++;
  renderer.displayBuffer(halfRefresh ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
