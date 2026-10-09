#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "../../freeink-sdk/libs/ui/FreeInkUI/include/components/lists/list.h"
#include "../../freeink-sdk/libs/ui/FreeInkUI/include/components/media/cover-grid.h"
#include "../../freeink-sdk/libs/ui/FreeInkUI/include/components/overlays/option-dialog.h"
#include "LibraryShelfLayout/LibraryShelfLayout.h"

namespace {
using namespace freeink::ui;
namespace shelf = library::shelf;

constexpr ActionId BOOK_ACTION = 41;

class ShelfDrawTarget : public DrawTarget {
 public:
  int titles = 0;
  Rect covers[shelf::PAGE_SIZE]{};
  int coverCount = 0;

  Size measureText(FontId, const char*, TextStyle) const override { return {20, 16}; }
  int16_t lineHeight(FontId) const override { return 16; }
  void fill(Rect, Paint, uint8_t, uint8_t) override {}
  void stroke(Rect, Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(Point, Point, uint8_t, Paint) override {}
  void triangle(Point, Point, Point, Paint) override {}
  void text(Rect, const char*, TextStyle) override { ++titles; }
  void bitmap(Rect, BitmapRef, BitmapMode, Paint, Rotation) override {}
};

// The host framebuffer records painted regions without requiring device fonts.
class PopupFramebuffer final : public ShelfDrawTarget {
 public:
  static constexpr int WIDTH = 480;
  static constexpr int HEIGHT = 800;
  std::vector<uint8_t> pixels = std::vector<uint8_t>(WIDTH * HEIGHT);

  Size measureText(FontId, const char* text, TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text) * 6), 16};
  }
  void fill(const Rect rect, const Paint paint, uint8_t, uint8_t) override {
    if (paint.kind == PaintKind::None) return;
    const uint8_t value = paint.color == Color::White ? 0 : paint.color == Color::Black ? 1 : 2;
    for (int y = rect.y; y < rect.bottom(); ++y) {
      if (y < 0 || y >= HEIGHT) continue;
      for (int x = rect.x; x < rect.right(); ++x) {
        if (x >= 0 && x < WIDTH) pixels[y * WIDTH + x] = value;
      }
    }
  }
  void stroke(const Rect rect, const Paint paint, const uint8_t width, uint8_t, uint8_t) override {
    fill({rect.x, rect.y, rect.width, width}, paint, 0, 0);
    fill({rect.x, static_cast<int16_t>(rect.bottom() - width), rect.width, width}, paint, 0, 0);
    fill({rect.x, rect.y, width, rect.height}, paint, 0, 0);
    fill({static_cast<int16_t>(rect.right() - width), rect.y, width, rect.height}, paint, 0, 0);
  }
  void text(Rect rect, const char* text, const TextStyle style) override {
    const int width = measureText(style.font, text, style).width;
    if (width < rect.width) rect.width = width;
    fill(rect, Paint::solid(style.color), 0, 0);
  }
};

struct ProviderState {
  int indexes[shelf::PAGE_SIZE]{};
  int calls = 0;
};

CoverGridItem provideBook(const uint16_t index, void* context) {
  auto& state = *static_cast<ProviderState*>(context);
  if (state.calls < shelf::PAGE_SIZE) state.indexes[state.calls++] = index;
  return coverGridItem("Book title", index);
}

bool paintCover(DrawTarget& target, const Rect rect, const CoverGridItem&, uint16_t, void*) {
  auto& shelfTarget = static_cast<ShelfDrawTarget&>(target);
  if (shelfTarget.coverCount < shelf::PAGE_SIZE) shelfTarget.covers[shelfTarget.coverCount++] = rect;
  return true;
}

CoverGridProps makeGridProps(const Rect body, const int count, const int top, ProviderState& provider) {
  const auto layout = shelf::makeLayout(body.width - (shelf::COLUMNS - 1) * 8, body.height, 16);
  CoverGridProps props;
  props.count = count;
  props.topIndex = top;
  props.columns = shelf::COLUMNS;
  props.rowHeight = layout.rowHeight;
  props.rowGap = layout.rowGap;
  props.gap = 8;
  props.coverSize = {static_cast<int16_t>(layout.coverWidth), static_cast<int16_t>(layout.coverHeight)};
  props.labelHeight = layout.labelHeight;
  props.cellInset = {shelf::CELL_PADDING, shelf::CELL_PADDING, shelf::CELL_PADDING, shelf::CELL_PADDING};
  props.itemProvider = provideBook;
  props.itemProviderUserData = &provider;
  props.coverPainter = paintCover;
  props.action = BOOK_ACTION;
  props.inputMask = InputTouch | InputLongPress;
  props.scrollIndicator = false;
  return props;
}

bool overlaps(const Rect first, const Rect second) {
  return first.x < second.right() && second.x < first.right() && first.y < second.bottom() && second.y < first.bottom();
}

TEST(LibraryShelfSdk, RealGridProvidesTwelveDistinctBooksInBothOrientations) {
  for (const Rect body :
       {Rect{12, 80, 456, 620}, Rect{12, 80, 776, 300}, Rect{12, 80, 456, 621}, Rect{12, 80, 776, 301}}) {
    DeviceContext device;
    device.width = body.width + 24;
    device.height = body.height + 160;
    InputSnapshot input;
    InteractionBuffer<16> interactions;
    ShelfDrawTarget target;
    ProviderState provider;
    Frame<16> frame(target, device, input, interactions);
    CoverGridProps props = makeGridProps(body, 25, 12, provider);
    coverGrid(frame, body, props);
    ASSERT_EQ(provider.calls, shelf::PAGE_SIZE);
    ASSERT_EQ(interactions.count(), static_cast<size_t>(shelf::PAGE_SIZE));
    EXPECT_FALSE(interactions.overflowed());
    EXPECT_EQ(target.titles, shelf::PAGE_SIZE);
    ASSERT_EQ(target.coverCount, shelf::PAGE_SIZE);
    for (int i = 0; i < shelf::PAGE_SIZE; ++i) {
      const Interaction hit = interactions.data()[i];
      EXPECT_EQ(provider.indexes[i], 12 + i);
      EXPECT_EQ(hit.value, 12 + i);
      EXPECT_EQ(hit.action, BOOK_ACTION);
      EXPECT_GE(hit.rect.x, body.x);
      EXPECT_LE(hit.rect.right(), body.right());
      EXPECT_GE(hit.rect.y, body.y);
      EXPECT_LE(hit.rect.bottom(), body.bottom());
      EXPECT_GE(target.covers[i].x, hit.rect.x);
      EXPECT_LE(target.covers[i].right(), hit.rect.right());
      EXPECT_GE(target.covers[i].y, hit.rect.y);
      EXPECT_LE(target.covers[i].bottom(), hit.rect.bottom());
      EXPECT_EQ(hit.rect.x, body.x + (i % shelf::COLUMNS) * (hit.rect.width + props.gap));
      EXPECT_EQ(hit.rect.y, body.y + (i / shelf::COLUMNS) * (props.rowHeight + props.rowGap));
      for (int j = 0; j < i; ++j) EXPECT_FALSE(overlaps(hit.rect, interactions.data()[j].rect));
    }
    for (int row = 0; row < shelf::ROWS; ++row) {
      const Rect rail{body.x, static_cast<int16_t>(body.y + (row + 1) * props.rowHeight + row * props.rowGap - 2),
                      body.width, 2};
      EXPECT_GE(rail.y, body.y);
      EXPECT_LE(rail.bottom(), body.bottom());
      for (int column = 0; column < shelf::COLUMNS; ++column) {
        const Rect cover = target.covers[row * shelf::COLUMNS + column];
        EXPECT_LE(cover.bottom(), rail.y);
      }
    }
  }
}

TEST(LibraryShelfSdk, ActualTouchRouterDistinguishesShortAndLongPressForEveryBook) {
  const Rect body{12, 80, 456, 620};
  DeviceContext device;
  device.width = 480;
  device.height = 800;
  InputSnapshot input;
  InteractionBuffer<16> interactions;
  ShelfDrawTarget target;
  ProviderState provider;
  Frame<16> frame(target, device, input, interactions);
  coverGrid(frame, body, makeGridProps(body, 25, 12, provider));
  ASSERT_EQ(interactions.count(), static_cast<size_t>(shelf::PAGE_SIZE));
  interactions.publish();
  for (int i = 0; i < shelf::PAGE_SIZE; ++i) {
    const Rect hit = interactions.publishedData()[i].rect;
    InputSnapshot tap;
    tap.touchReleased = true;
    tap.touchX = hit.x + hit.width / 2;
    tap.touchY = hit.y + hit.height / 2;
    ActionEvent action = interactions.routePublished(tap);
    ASSERT_TRUE(action);
    EXPECT_EQ(action.action, BOOK_ACTION);
    EXPECT_EQ(action.value, 12 + i);
    EXPECT_FALSE(action.longPress);
    tap.longPress = true;
    action = interactions.routePublished(tap);
    ASSERT_TRUE(action);
    EXPECT_EQ(action.action, BOOK_ACTION);
    EXPECT_EQ(action.value, 12 + i);
    EXPECT_TRUE(action.longPress);
  }
  InputSnapshot gapTap;
  gapTap.touchReleased = true;
  gapTap.touchX = body.x + 10;
  gapTap.touchY = body.y + makeGridProps(body, 25, 12, provider).rowHeight + 1;
  EXPECT_FALSE(interactions.routePublished(gapTap));
}

TEST(LibraryShelfSdk, PartialFinalPagesExposeOnlyTheirActualBook) {
  for (const int count : {13, 25}) {
    const Rect body{12, 80, 776, 300};
    DeviceContext device;
    device.width = 800;
    device.height = 480;
    InputSnapshot input;
    InteractionBuffer<16> interactions;
    ShelfDrawTarget target;
    ProviderState provider;
    Frame<16> frame(target, device, input, interactions);
    coverGrid(frame, body, makeGridProps(body, count, count - 1, provider));
    ASSERT_EQ(interactions.count(), 1U);
    ASSERT_EQ(provider.calls, 1);
    EXPECT_EQ(provider.indexes[0], count - 1);
    const Rect hit = interactions.data()[0].rect;
    InputSnapshot tap;
    tap.touchReleased = true;
    tap.longPress = true;
    tap.touchX = hit.x + hit.width / 2;
    tap.touchY = hit.y + hit.height / 2;
    const ActionEvent action = interactions.route(tap);
    ASSERT_TRUE(action);
    EXPECT_EQ(action.value, count - 1);
    EXPECT_TRUE(action.longPress);
  }
}

TEST(LibraryShelfSdk, PaddedListNavigationReachesAndLeavesIncompleteFinalShelf) {
  for (const int count : {13, 25}) {
    const int paddedCount = ((count + shelf::PAGE_SIZE - 1) / shelf::PAGE_SIZE) * shelf::PAGE_SIZE;
    const Rect navBody{0, 0, 120, 120};
    ListNav nav;
    ListProps props;
    nav.syncToProps(navBody, 10, 0, paddedCount, props, 1);
    nav.onListRendered(0, shelf::PAGE_SIZE, false);
    EXPECT_EQ(nav.inputPageRows(), shelf::PAGE_SIZE);
    for (int top = shelf::PAGE_SIZE; top < count; top += shelf::PAGE_SIZE) {
      nav.requestScroll(shelf::PAGE_SIZE);
      nav.syncToProps(navBody, 10, 0, paddedCount, props, 1);
      const int effectiveTop = shelf::pageStart(nav.top, count);
      ASSERT_EQ(effectiveTop, top);
      EXPECT_EQ(nav.selected.load(), 0);
      nav.onListRendered(effectiveTop, shelf::visibleCount(effectiveTop, count), false);
      EXPECT_FALSE(nav.consumeRebuildNeeded());
    }
    EXPECT_EQ(nav.top, count - 1);
    EXPECT_EQ(nav.inputPageRows(), 1);
    nav.requestScroll(-shelf::PAGE_SIZE);
    nav.syncToProps(navBody, 10, 0, paddedCount, props, 1);
    EXPECT_EQ(shelf::pageStart(nav.top, count), count - 1 - shelf::PAGE_SIZE);
  }
}

TEST(LibraryShelfSdk, RingSelectionAndRenderFeedbackKeepBookTwelveVisible) {
  ListNav nav;
  ListProps props;
  nav.requestSelection(13);
  nav.syncToProps(Rect{0, 0, 120, 120}, 10, 0, 24, props, 1);
  EXPECT_EQ(props.selectedIndex, 12);
  EXPECT_EQ(nav.selected.load(), 13);
  // ListNav follows minimally; the shelf selects the containing whole page.
  EXPECT_EQ(props.topIndex, 1);
  const int top = shelf::pageStart(props.selectedIndex, 13);
  EXPECT_EQ(top, 12);
  nav.onListRendered(top, shelf::visibleCount(top, 13), true);
  EXPECT_EQ(nav.top, 12);
  EXPECT_FALSE(nav.followPending);
  EXPECT_FALSE(nav.consumeRebuildNeeded());
  nav.requestSelection(1);
  nav.syncToProps(Rect{0, 0, 120, 120}, 10, 0, 24, props, 1);
  EXPECT_EQ(props.selectedIndex, 0);
  nav.onListRendered(0, shelf::PAGE_SIZE, true);
  EXPECT_EQ(nav.top, 0);
  EXPECT_FALSE(nav.followPending);
}

TEST(LibraryShelfSdk, GlobalBookActionConvertsToRingSelectionAtTheHandlerBoundary) {
  const Rect body{12, 80, 456, 620};
  DeviceContext device;
  device.width = 480;
  device.height = 800;
  InputSnapshot input;
  InteractionBuffer<16> interactions;
  ShelfDrawTarget target;
  ProviderState provider;
  Frame<16> frame(target, device, input, interactions);
  coverGrid(frame, body, makeGridProps(body, 25, 12, provider));
  const Rect hit = interactions.data()[0].rect;
  InputSnapshot tap;
  tap.touchReleased = true;
  tap.touchX = hit.x + hit.width / 2;
  tap.touchY = hit.y + hit.height / 2;
  const ActionEvent action = interactions.route(tap);
  ASSERT_TRUE(action);
  ASSERT_EQ(action.value, 12);
  ListNav nav;
  ListProps props;
  nav.requestSelection(action.value + 1);
  nav.syncToProps(Rect{0, 0, 120, 120}, 10, 0, 36, props, 1);
  EXPECT_EQ(nav.selected.load(), 13);
  EXPECT_EQ(props.selectedIndex, 12);
  EXPECT_EQ(shelf::pageStart(props.selectedIndex, 25), 12);
}

TEST(LibraryShelfSdk, RepaintingShelfBeforeSmallerDialogRestoresAllOutsidePixels) {
  const Rect body{12, 80, 456, 620};
  DeviceContext device;
  device.width = PopupFramebuffer::WIDTH;
  device.height = PopupFramebuffer::HEIGHT;
  InputSnapshot input;
  InteractionBuffer<16> interactions;
  PopupFramebuffer target;
  ProviderState provider;
  const auto renderShelf = [&] {
    Frame<16> frame(target, device, input, interactions);
    target.fill(frame.screen(), Paint::solid(Color::White), 0, 0);
    auto props = makeGridProps(body, 25, 12, provider);
    props.coverPainter = [](DrawTarget& draw, const Rect cover, const CoverGridItem&, uint16_t, void*) {
      draw.fill(cover, Paint::solid(Color::Black));
      return true;
    };
    coverGrid(frame, body, props);
  };
  renderShelf();
  const std::vector<uint8_t> background = target.pixels;
  OptionDialogProps dialog;
  dialog.title = "Book metadata";
  dialog.headline = "A title with several words";
  dialog.message = "File: book.epub";
  dialog.contentHeight = 160;
  const int16_t largeHeight = optionDialogHeight(target, dialog, 320);
  const Rect large{80, static_cast<int16_t>((device.height - largeHeight) / 2), 320, largeHeight};
  Frame<16> largeFrame(target, device, input, interactions);
  optionDialog(largeFrame, large, dialog);
  dialog.contentHeight = 0;
  const int16_t smallHeight = optionDialogHeight(target, dialog, 320);
  const Rect small{80, static_cast<int16_t>((device.height - smallHeight) / 2), 320, smallHeight};
  ASSERT_LT(small.height, large.height);
  Frame<16> staleFrame(target, device, input, interactions);
  optionDialog(staleFrame, small, dialog);
  int staleOutsidePixels = 0;
  for (int y = 0; y < device.height; ++y) {
    for (int x = 0; x < device.width; ++x) {
      const int pixel = y * device.width + x;
      if (!small.contains(x, y) && target.pixels[pixel] != background[pixel]) ++staleOutsidePixels;
    }
  }
  ASSERT_GT(staleOutsidePixels, 0);
  renderShelf();
  Frame<16> restoredFrame(target, device, input, interactions);
  optionDialog(restoredFrame, small, dialog);
  int mismatchesOutside = 0;
  int dialogPixels = 0;
  for (int y = 0; y < device.height; ++y) {
    for (int x = 0; x < device.width; ++x) {
      const int pixel = y * device.width + x;
      if (target.pixels[pixel] == background[pixel]) continue;
      if (small.contains(x, y))
        ++dialogPixels;
      else
        ++mismatchesOutside;
    }
  }
  EXPECT_EQ(mismatchesOutside, 0);
  EXPECT_GT(dialogPixels, 0);
}
}  // namespace
