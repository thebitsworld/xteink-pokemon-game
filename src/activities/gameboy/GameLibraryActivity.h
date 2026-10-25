#pragma once

#if CROSSINK_GAMEBOY

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <string>
#include <vector>

#include "GameLibraryStore.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

// Applications > Game Boy: the Game Boy ROMs on the SD card, all of them,
// favourites or recently played (an idea from Ink-boy's game library). A game
// opens a menu: Play, favourite on/off, delete its saved game. "Browse
// folders" keeps the old file picker for a ROM the scan does not reach.
// Finishes with the ROM to play as a FilePathResult, like that picker.
class GameLibraryActivity final : public Activity {
 public:
  explicit GameLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  using UiApp = freeink::ui::FreeInkApp<40, 4>;
  enum class Filter : uint8_t { All, Favorites, Recent, Count };

  struct Game {
    std::string path;
    std::string title;
  };

  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;
  std::atomic<bool> uiReady{false};
  int selectedIndex = 0;
  int visibleRows = 1;
  int topIndex = 0;

  Filter filter = Filter::All;
  std::vector<Game> games;          // every ROM found, by title
  std::vector<size_t> shown;        // indexes into games for the current filter
  std::vector<std::string> subtitles;
  GameLibraryStore::Lists lists;
  bool scanned = false;

  static constexpr int FIXED_ROWS = 2;  // the filter row, Browse folders

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
  void scan();
  void scanFolder(const std::string& folder, int depth);
  void addGame(const std::string& path);
  void rebuildShown();
  void activate(int row);
  void openGameMenu(size_t game);
  void play(const std::string& path);
  void browseFolders();
};

#endif
