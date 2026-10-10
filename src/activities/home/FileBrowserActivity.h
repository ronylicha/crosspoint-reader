#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/AppNavigation.h"
#include "components/OptionPopup.h"

class FileBrowserActivity final : public UiListActivity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // File actions
  bool removeDirFile(const std::string& fullPath);
  void showEntryActions();
  void startRename();
  void renameSelectedFile(const std::string& oldPath, const std::string& oldEntry, const std::string& newStem,
                          const std::string& extension);
  void deleteSelected();

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;
  OptionPopup optionPopup;

  // Pull-based rows: the SDK list resolves each drawn row on demand through
  // provideRow() (fui::ListProps::rowProvider), so the only per-file
  // residency is `files` itself — no full-length rowNames/rowExtensions/
  // rowItems arrays (a 1000-file folder used to pin ~100KB of vectors plus a
  // heap copy of every display name, which aborted under -fno-exceptions
  // when the contiguous blocks no longer fit). The label/value strings for
  // the row being laid out live in these scratch buffers; the provider
  // contract only needs them valid until the next provideRow() call.
  static constexpr size_t ROW_NAME_BUF_SIZE = 512;  // NAME_BUFFER_SIZE + "[]" + terminator slack
  char rowNameBuf[ROW_NAME_BUF_SIZE]{};
  char rowExtBuf[16]{};
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);

  // CJK fallback glyphs are prewarmed for a bounded window of rows around the
  // viewport (one SD pass per list page, like the reader TOC) instead of the
  // whole folder. -1 = nothing prewarmed; reset by loadFiles().
  static constexpr int PREWARM_WINDOW = 24;
  int prewarmedStart = -1;
  void prewarmRowGlyphs(int start);

  int listCount() const override { return static_cast<int>(files.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Long-press BACK goes to root (Books mode); short Back goes up a directory
  // (home/cancel at root), and Confirm activates on release while a hold opens
  // file actions.
  bool handleCustomInput() override;
  bool handleButtons() override;
  // PickFirmware keeps the titled header; Books builds its chrome in buildScreen.
  const char* headerTitle() const override;
  // Footer labels depend on path depth and picker mode.
  void drawFooter() override;
  void activateSelected();
  // Climbs to the parent folder, selecting the folder just left.
  void goUpFolder();

  // Books mode: app tab bar + Library|Files sub-bar, then a folder row (back
  // arrow on touch boards + folder name) inside sub-folders; tapping the row
  // climbs one folder.
  static constexpr freeink::ui::ActionId ACTION_FOLDER_UP = ACTION_USER;
  static constexpr int FOLDER_ROW_HEIGHT = 48;
  static constexpr int FOLDER_ARROW_GAP = 8;
  void buildFolderRow(UiScreen& screen, int top);
  static void onFolderUpAction(const freeink::ui::ActionEvent& event, void* user);
  static void onSubBarAction(const freeink::ui::ActionEvent& event, void* user);

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books);
  void onEnter() override;
  void onExit() override;
  void render(RenderLock&& lock) override;
  bool isModalActive() const override { return optionPopup.isActive(); }
};
