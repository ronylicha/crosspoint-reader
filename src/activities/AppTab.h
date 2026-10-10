#pragma once

#include <cstdint>

// Root screens reachable from the app tab bar (components/AppNavigation.h),
// in bar order.
enum class AppTab : uint8_t { Home, Library, Games, Transfer, Stats, Settings };
