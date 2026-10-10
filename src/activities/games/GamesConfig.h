#pragma once

// The board games are played by tapping squares, so they only ship in
// firmware that targets a touch board; button-only builds (X4, X3) leave them
// out entirely. A multi-board touch build still hides them at runtime on a
// board without touch (appnav::visibleTabs). Host tests preset the macro.
#ifndef CROSSPOINT_GAMES
#include <BoardConfig.h>
#define CROSSPOINT_GAMES FREEINK_CAP_TOUCH
#endif
