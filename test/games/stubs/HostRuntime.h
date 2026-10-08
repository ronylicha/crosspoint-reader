#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace host {
inline uint32_t now = 0;
inline uint32_t renderDuration = 0;
inline uint32_t randomValue = 0;
inline int randomCalls = 0;
inline int lockDepth = 0;
inline int lockViolations = 0;
inline int finishCalls = 0;
inline int waitedRenders = 0;
inline bool hasTouch = true;
inline void reset() {
  now = renderDuration = randomValue = 0;
  randomCalls = lockDepth = lockViolations = finishCalls = waitedRenders = 0;
  hasTouch = true;
}
}  // namespace host
inline uint32_t millis() { return host::now; }
inline void vTaskDelay(uint32_t ticks) { host::now += ticks; }
inline uint32_t esp_random() {
  ++host::randomCalls;
  return host::randomValue;
}

enum Color { White, Black, LightGray, DarkGray };
struct Rect {
  int x, y, width, height;
};
struct HalDisplay {
  enum { FAST_REFRESH, HALF_REFRESH };
};
class GfxRenderer {
 public:
  struct Border {
    int width, height, thickness;
    int x, y;
  };
  int displays = 0;
  int screenWidth = 480;
  int screenHeight = 800;
  std::vector<Border> borders;
  int getScreenWidth() const { return screenWidth; }
  int getScreenHeight() const { return screenHeight; }
  int getTextWidth(int, const char* text) const { return static_cast<int>(strlen(text)) * 6; }
  int getTextHeight(int) const { return 14; }
  void clearScreen() {}
  void displayBuffer(int) { ++displays; }
  template <class... T>
  void fillPolygon(T...) {}
  template <class... T>
  void drawLine(T...) {}
  template <class... T>
  void drawText(T...) {}
  template <class... T>
  void drawCenteredText(T...) {}
  template <class... T>
  void fillRect(T...) {}
  template <class... T>
  void fillRectDither(T...) {}
  template <class... T>
  void fillRoundedRect(T...) {}
  template <class... T>
  void drawRect(T...) {}
  template <class C>
  void drawRoundedRect(int x, int y, int width, int height, int thickness, int, C) {
    borders.push_back({width, height, thickness, x, y});
  }
};
class MappedInputManager {
 public:
  enum class Button { Back, Confirm, ScreenLeft, ScreenRight, ScreenUp, ScreenDown, NavNext, NavPrevious };
  struct Labels {
    const char *btn1, *btn2, *btn3, *btn4;
  };
  std::array<bool, 8> pressed{}, released{}, held{}, longPressed{};
  bool tapped = false, longTapped = false;
  int tapX = 0, tapY = 0;
  uint32_t heldTime = 0;
  bool wasPressed(Button b) const { return pressed[static_cast<int>(b)]; }
  bool wasReleased(Button b) const { return released[static_cast<int>(b)]; }
  bool isPressed(Button b) const { return held[static_cast<int>(b)]; }
  bool wasLongPressed(Button b, uint32_t) const { return longPressed[static_cast<int>(b)]; }
  uint32_t getHeldTime() const { return heldTime; }
  bool wasScreenTapped(int& x, int& y) const {
    x = tapX;
    y = tapY;
    return tapped;
  }
  bool wasScreenLongPress(int& x, int& y) const {
    x = tapX;
    y = tapY;
    return longTapped;
  }
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) const { return {a, b, c, d}; }
  void clear() {
    pressed.fill(false);
    released.fill(false);
    held.fill(false);
    longPressed.fill(false);
    tapped = longTapped = false;
    heldTime = 0;
  }
};
class ButtonNavigator {
 public:
  template <class F>
  void onPressAndContinuous(std::initializer_list<MappedInputManager::Button>, F) {}
};
class Activity;
class RenderLock {
  bool locked = true;

 public:
  RenderLock() {
    if (host::lockDepth++ != 0) ++host::lockViolations;
  }
  explicit RenderLock(Activity&) : RenderLock() {}
  ~RenderLock() { unlock(); }
  void unlock() {
    if (locked) {
      --host::lockDepth;
      locked = false;
    }
  }
  bool ownsLock() const { return locked; }
};
class Activity {
 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

 public:
  Activity(const char*, GfxRenderer& r, MappedInputManager& input) : renderer(r), mappedInput(input) {}
  virtual ~Activity() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void prepareForSleep() {}
  virtual void loop() {}
  virtual void render(RenderLock&&) {}
  void requestUpdate(bool = false) {}
  void requestUpdateAndWait() {
    if (host::lockDepth != 0) ++host::lockViolations;
    ++host::waitedRenders;
    host::now += host::renderDuration;
    RenderLock lock;
    render(std::move(lock));
  }
  // ActivityManager queues Pop; destruction occurs later in processPendingAction.
  static void finish() { ++host::finishCalls; }
};
class BaseTheme {
 public:
  void drawHeader(const GfxRenderer&, Rect, const char*, const char* = nullptr, bool = true) const {}
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) const {}
};
struct UITheme {
  struct Metrics {
    int topPadding = 8, headerHeight = 48, buttonHintsHeight = 36;
  } metrics;
  mutable Metrics adjustedMetrics;
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  const Metrics& getMetrics() const {
    adjustedMetrics = metrics;
    if (host::hasTouch) adjustedMetrics.buttonHintsHeight = 0;
    return adjustedMetrics;
  }
  const BaseTheme& getTheme() const {
    static const BaseTheme theme;
    return theme;
  }
};
#define GUI UITheme::getInstance().getTheme()
#define LOG_ERR(...) \
  do {               \
  } while (false)
#define LOG_DBG(...) \
  do {               \
  } while (false)
#define LOG_INF(...) \
  do {               \
  } while (false)
#define tr(key) #key
inline constexpr int UI_10_FONT_ID = 10, UI_12_FONT_ID = 12;
