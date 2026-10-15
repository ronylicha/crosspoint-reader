#pragma once

#include <ReadingLog.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "EndOfBookOptions.h"
#include "ReaderSession.h"
#include "activities/Activity.h"

class ReaderActivity : public Activity {
 protected:
  std::string bookPath;
  int pagesUntilFullRefresh = 0;
  bool forcedRefreshPending = false;

  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};
  ReaderSession readerSession;
  // Reading statistics: mutated on the render task and by notePageTurn, both
  // under the render lock; flushed to READING_STATS on the main task.
  readingstats::SessionTracker statsTracker;
  std::atomic<bool> pageRendered{false};
  bool bookRemembered = false;
  void markPageRendered() { pageRendered.store(true, std::memory_order_release); }
  void rememberBookOnceRendered();

  explicit ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string bookPath, bool allowFastInitialRefresh);

  virtual bool loadBook() = 0;
  // Called when loadBook() failed. Return true to keep the activity alive
  // (e.g. showing a dialog); false finishes it (the default).
  virtual bool handleLoadFailure() { return false; }
  virtual std::string getBookTitle() const = 0;
  virtual std::string getBookAuthor() const { return ""; }
  virtual std::string getBookThumbBmpPath() const { return ""; }
  // Whole-book progress for the reader.exit plugin event, reusing the
  // per-reader ScreenshotInfo implementations.
  //
  // Upstream added this helper in d3e55c53 with an earlier slice of this
  // feature, then removed it in c1e1fec3 as dead code once no caller remained
  // ("removes unnecessary wrapper structs"). It is reinstated here because this
  // change reintroduces the callers: ReaderActivity.cpp uses it for the progress
  // string and for the session's render-complete basis points, and
  // EpubReaderActivity overrides getProgressBasisPoints() and falls back to it.
  int getProgressPercent() const { return getScreenshotInfo().progressPercent; }
  virtual int getProgressBasisPoints() const { return getProgressPercent() * 100; }

  // Chapter of the rendered page for the statistics' chapter count; -1 when
  // the format has no chapters. Read on the render task.
  virtual int statsChapterIndex() const { return -1; }

  // Queues the current position for the automatic KOReader push when the
  // reader moved during this session. Formats KOReader cannot place (XTC) keep
  // the no-op. Runs under the render lock from onExit and prepareForSleep.
  // `preserveFrame`: the sleep screen may keep the page in the framebuffer,
  // so any use of it as scratch must give the page back intact.
  virtual void queueKOReaderProgress(bool preserveFrame) { (void)preserveFrame; }
  // Set once the reader prepares for sleep: its onExit then runs on the way
  // into the sleep screen.
  bool sleepPrepared = false;

  virtual bool handleFormatInput() { return false; }
  virtual bool pageTurn(bool isForward) = 0;
  virtual bool skipPages(int amount) { return pageTurn(amount > 0); }
  virtual bool isAtEndOfBook() const = 0;
  virtual void onReturnFromEndOfBook() {}

  virtual void renderBook() = 0;
  virtual void applyInitialOrientation();
  virtual void onEndOfBookRendered() {}

  bool handleBackNavigation();
  /** True while the end-of-book suggestion menu is on screen and owning input. */
  bool endOfBookMenuActive() const;
  bool handleEndOfBookMenu(bool suppressConfirmRelease = false);
  bool handleEndOfBookPageTurn(bool prevTriggered, bool nextTriggered);
  void clearEndOfBookOptionsIfNeeded();
  void disableFastInitialRefresh();
  void notePageTurn(bool forward, bool succeeded);
  void flushReaderSession();
  // Caller holds the render lock (onExit/prepareForSleep run under it).
  void flushReadingStatsLocked();

 public:
  ~ReaderActivity() override = default;

  static std::unique_ptr<ReaderActivity> create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                std::string path, bool allowFastInitialRefresh);

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool isReaderActivity() const final { return true; }
  bool handleForcedRefresh() final;
};
