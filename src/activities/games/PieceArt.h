#pragma once

#include <GfxRenderer.h>

#include <cstdint>

// Vector-drawn game piece artwork. Shapes are defined in a 100x100 unit box
// and scaled to the target square, so pieces render cleanly at any board size.
// Chess silhouettes follow the classic Staunton shapes; the crown is used as
// the king marker in checkers.
namespace gameart {

constexpr int PAWN = 1;
constexpr int KNIGHT = 2;
constexpr int BISHOP = 3;
constexpr int ROOK = 4;
constexpr int QUEEN = 5;
constexpr int KING = 6;

inline void scalePoly(const int* xs, const int* ys, const int n, const int x, const int y, const int s, int* px,
                      int* py) {
  for (int i = 0; i < n; i++) {
    px[i] = x + xs[i] * s / 100;
    py[i] = y + ys[i] * s / 100;
  }
}

inline void poly(GfxRenderer& r, const int* xs, const int* ys, const int n, const int x, const int y, const int s,
                 const bool black) {
  int px[16];
  int py[16];
  scalePoly(xs, ys, n, x, y, s, px, py);
  r.fillPolygon(px, py, n, black);
}

inline void polyOutline(GfxRenderer& r, const int* xs, const int* ys, const int n, const int x, const int y,
                        const int s, const bool black) {
  int px[16];
  int py[16];
  scalePoly(xs, ys, n, x, y, s, px, py);
  for (int i = 0; i < n; i++) {
    const int j = (i + 1) % n;
    r.drawLine(px[i], py[i], px[j], py[j], 2, black);
  }
}

// 12-gon approximation of a filled circle, in unit-box coordinates.
inline void disk(GfxRenderer& r, const int cx, const int cy, const int rad, const int x, const int y, const int s,
                 const bool black) {
  static const int DX[12] = {100, 87, 50, 0, -50, -87, -100, -87, -50, 0, 50, 87};
  static const int DY[12] = {0, 50, 87, 100, 87, 50, 0, -50, -87, -100, -87, -50};
  int xs[12];
  int ys[12];
  for (int i = 0; i < 12; i++) {
    xs[i] = cx + rad * DX[i] / 100;
    ys[i] = cy + rad * DY[i] / 100;
  }
  poly(r, xs, ys, 12, x, y, s, black);
}

inline void diskOutline(GfxRenderer& r, const int cx, const int cy, const int rad, const int x, const int y,
                        const int s, const bool black) {
  static const int DX[12] = {100, 87, 50, 0, -50, -87, -100, -87, -50, 0, 50, 87};
  static const int DY[12] = {0, 50, 87, 100, 87, 50, 0, -50, -87, -100, -87, -50};
  int xs[12];
  int ys[12];
  for (int i = 0; i < 12; i++) {
    xs[i] = cx + rad * DX[i] / 100;
    ys[i] = cy + rad * DY[i] / 100;
  }
  polyOutline(r, xs, ys, 12, x, y, s, black);
}

namespace {

struct Shape {
  const int* xs;
  const int* ys;
  int n;
};

// Draw a set of filled shapes; for white pieces each shape is filled white
// and outlined in black so it reads on both light and dark squares.
void drawShapes(GfxRenderer& r, const Shape* shapes, const int count, const int x, const int y, const int s,
                const bool white) {
  for (int i = 0; i < count; i++) {
    poly(r, shapes[i].xs, shapes[i].ys, shapes[i].n, x, y, s, !white);
  }
  if (white) {
    for (int i = 0; i < count; i++) {
      polyOutline(r, shapes[i].xs, shapes[i].ys, shapes[i].n, x, y, s, true);
    }
  }
}

// --- Shared part geometry ---------------------------------------------------

const int PLINTH_X[4] = {24, 76, 76, 24};
const int PLINTH_Y[4] = {86, 86, 94, 94};
const Shape PLINTH{PLINTH_X, PLINTH_Y, 4};

const int BODY_WIDE_X[4] = {32, 68, 58, 42};
const int BODY_WIDE_Y[4] = {86, 86, 52, 52};
const Shape BODY_WIDE{BODY_WIDE_X, BODY_WIDE_Y, 4};

const int COLLAR_X[4] = {36, 64, 64, 36};
const int COLLAR_Y[4] = {46, 46, 54, 54};
const Shape COLLAR{COLLAR_X, COLLAR_Y, 4};

const int QBODY_X[4] = {34, 66, 62, 38};
const int QBODY_Y[4] = {86, 86, 56, 56};
const Shape QBODY{QBODY_X, QBODY_Y, 4};

const int QCOLLAR_X[4] = {36, 64, 64, 36};
const int QCOLLAR_Y[4] = {48, 48, 56, 56};
const Shape QCOLLAR{QCOLLAR_X, QCOLLAR_Y, 4};

// --- Pawn -------------------------------------------------------------------

const int PAWN_NECK_X[4] = {42, 58, 54, 46};
const int PAWN_NECK_Y[4] = {46, 46, 38, 38};

// --- Rook -------------------------------------------------------------------

const int ROOK_BODY_X[4] = {30, 70, 62, 38};
const int ROOK_BODY_Y[4] = {80, 80, 32, 32};
const int ROOK_BASE_X[4] = {26, 74, 74, 26};
const int ROOK_BASE_Y[4] = {80, 80, 90, 90};
const int ROOK_BAND_X[4] = {24, 76, 76, 24};
const int ROOK_BAND_Y[4] = {26, 26, 34, 34};
const int MERLON1_X[4] = {24, 36, 36, 24};
const int MERLON2_X[4] = {44, 56, 56, 44};
const int MERLON3_X[4] = {64, 76, 76, 64};
const int MERLON_Y[4] = {12, 12, 26, 26};

// --- Knight (facing left) ---------------------------------------------------

const int KNIGHT_X[18] = {17, 23, 31, 29, 33, 30, 26, 26, 74, 74, 66, 60, 50, 42, 38, 32, 27, 19};
const int KNIGHT_Y[18] = {46, 50, 52, 62, 72, 84, 86, 92, 92, 84, 62, 44, 28, 18, 8, 14, 20, 38};

// --- Bishop -----------------------------------------------------------------

const int BISHOP_BODY_X[4] = {36, 64, 58, 42};
const int BISHOP_BODY_Y[4] = {86, 86, 62, 62};
const int BISHOP_COLLAR_X[4] = {38, 62, 62, 38};
const int BISHOP_COLLAR_Y[4] = {54, 54, 62, 62};
const int MITRE_X[5] = {50, 64, 60, 40, 36};
const int MITRE_Y[5] = {18, 34, 52, 52, 34};

// --- Queen ------------------------------------------------------------------

const int QCROWN_X[11] = {30, 30, 37, 43, 47, 50, 53, 57, 63, 70, 70};
const int QCROWN_Y[11] = {48, 28, 42, 24, 42, 20, 42, 24, 42, 28, 48};

// --- King -------------------------------------------------------------------

const int KCROWN_X[7] = {32, 32, 42, 50, 58, 68, 68};
const int KCROWN_Y[7] = {48, 30, 40, 28, 40, 30, 48};
const int CROSS_V_X[4] = {47, 53, 53, 47};
const int CROSS_V_Y[4] = {8, 8, 24, 24};
const int CROSS_H_X[4] = {41, 59, 59, 41};
const int CROSS_H_Y[4] = {13, 13, 19, 19};

}  // namespace

// Draw a chess piece silhouette. piece: PAWN..KING, white: piece color.
inline void drawChessPiece(GfxRenderer& r, const int piece, const bool white, const int x, const int y,
                           const int size) {
  // Shrink inside the square a touch for breathing room.
  const int pad = size / 12;
  const int px = x + pad;
  const int py = y + pad;
  const int ps = size - 2 * pad;

  switch (piece) {
    case PAWN: {
      const Shape shapes[] = {PLINTH, BODY_WIDE, COLLAR, Shape{PAWN_NECK_X, PAWN_NECK_Y, 4}};
      drawShapes(r, shapes, 4, px, py, ps, white);
      disk(r, 50, 28, 14, px, py, ps, !white);
      if (white) diskOutline(r, 50, 28, 14, px, py, ps, true);
      break;
    }
    case ROOK: {
      const Shape shapes[] = {PLINTH,
                              Shape{ROOK_BASE_X, ROOK_BASE_Y, 4},
                              Shape{ROOK_BODY_X, ROOK_BODY_Y, 4},
                              Shape{ROOK_BAND_X, ROOK_BAND_Y, 4},
                              Shape{MERLON1_X, MERLON_Y, 4},
                              Shape{MERLON2_X, MERLON_Y, 4},
                              Shape{MERLON3_X, MERLON_Y, 4}};
      drawShapes(r, shapes, 7, px, py, ps, white);
      break;
    }
    case KNIGHT: {
      const Shape shapes[] = {Shape{KNIGHT_X, KNIGHT_Y, 18}};
      drawShapes(r, shapes, 1, px, py, ps, white);
      // Eye and nostril in the opposite tone.
      disk(r, 35, 30, 4, px, py, ps, white);
      disk(r, 21, 43, 3, px, py, ps, white);
      break;
    }
    case BISHOP: {
      const Shape shapes[] = {PLINTH, Shape{BISHOP_BODY_X, BISHOP_BODY_Y, 4}, Shape{BISHOP_COLLAR_X, BISHOP_COLLAR_Y, 4},
                              Shape{MITRE_X, MITRE_Y, 5}};
      drawShapes(r, shapes, 4, px, py, ps, white);
      disk(r, 50, 13, 6, px, py, ps, !white);
      if (white) diskOutline(r, 50, 13, 6, px, py, ps, true);
      // Mitre slit in the opposite tone.
      r.drawLine(px + 44 * ps / 100, py + 30 * ps / 100, px + 56 * ps / 100, py + 44 * ps / 100, 2, white);
      break;
    }
    case QUEEN: {
      const Shape shapes[] = {PLINTH, QBODY, QCOLLAR, Shape{QCROWN_X, QCROWN_Y, 11}};
      drawShapes(r, shapes, 4, px, py, ps, white);
      // Balls on the five spikes.
      static const int TIP_X[5] = {30, 43, 50, 57, 70};
      static const int TIP_Y[5] = {26, 22, 18, 22, 26};
      for (int i = 0; i < 5; i++) {
        disk(r, TIP_X[i], TIP_Y[i], 4, px, py, ps, !white);
        if (white) diskOutline(r, TIP_X[i], TIP_Y[i], 4, px, py, ps, true);
      }
      break;
    }
    case KING: {
      const Shape shapes[] = {PLINTH, QBODY, QCOLLAR, Shape{KCROWN_X, KCROWN_Y, 7}, Shape{CROSS_V_X, CROSS_V_Y, 4},
                              Shape{CROSS_H_X, CROSS_H_Y, 4}};
      drawShapes(r, shapes, 6, px, py, ps, white);
      break;
    }
    default:
      break;
  }
}

// Crown marker drawn on a checkers king, in the token's opposite color.
// (x, y, size) is the token's own box, not the board square.
inline void drawCrown(GfxRenderer& r, const int x, const int y, const int size, const bool onWhiteToken) {
  const bool color = onWhiteToken;  // black crown on white token and vice versa
  const int xs[9] = {28, 28, 38, 44, 50, 56, 62, 72, 72};
  const int ys[9] = {60, 38, 52, 44, 32, 44, 52, 38, 60};
  poly(r, xs, ys, 9, x, y, size, color);
  const int bandX[4] = {28, 72, 72, 28};
  const int bandY[4] = {60, 60, 68, 68};
  poly(r, bandX, bandY, 4, x, y, size, color);
}

}  // namespace gameart
