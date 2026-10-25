#include "GameLibraryActivity.h"

#if CROSSINK_GAMEBOY

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <memory>

#include "GameBoyActivity.h"
#include "MappedInputManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
constexpr size_t MAX_GAMES = 200;
constexpr int MAX_SCAN_DEPTH = 3;

bool isRom(const std::string& name) {
  return FsHelpers::checkFileExtension(std::string_view{name}, ".gb") ||
         FsHelpers::checkFileExtension(std::string_view{name}, ".gbc");
}

// Folders the scan never enters: hidden ones, the art packs (thousands of
// files) and the system's own.
bool skipFolder(const std::string& name) {
  return name.empty() || name[0] == '.' || name == "pokemon" || name == "System Volume Information" ||
         name == "sleep" || name == "fonts" || name == "dictionaries";
}

std::string titleFor(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string title = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = title.find_last_of('.');
  if (dot != std::string::npos && dot > 0) title.erase(dot);
  return title;
}

std::string folderOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos || slash == 0 ? std::string("/") : path.substr(0, slash);
}

bool lessTitle(const std::string& a, const std::string& b) {
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](const char x, const char y) {
    return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
  });
}

StrId filterName(const uint8_t filter) {
  switch (filter) {
    case 1: return StrId::STR_GAME_FILTER_FAVORITES;
    case 2: return StrId::STR_GAME_FILTER_RECENT;
    default: return StrId::STR_GAME_FILTER_ALL;
  }
}
}  // namespace

GameLibraryActivity::GameLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("GameLibrary", renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void GameLibraryActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  topIndex = 0;
  visibleRows = 1;
  uiReady = false;
  scanned = false;
  lists = GameLibraryStore::load();
  // Open on the recently played games when there are any: the next game is
  // usually the last one.
  filter = lists.recents.empty() ? Filter::All : Filter::Recent;
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_ROW, &GameLibraryActivity::onRowEvent, this);
  app.setScreen(&GameLibraryActivity::listScreen, this);
  // The first frame says "Searching"; loop() scans after it is drawn.
  requestUpdate();
}

void GameLibraryActivity::addGame(const std::string& path) {
  if (games.size() >= MAX_GAMES) return;
  for (const auto& game : games) {
    if (game.path == path) return;
  }
  games.push_back({path, titleFor(path)});
}

void GameLibraryActivity::scanFolder(const std::string& folder, const int depth) {
  if (games.size() >= MAX_GAMES) return;
  auto dir = Storage.open(folder.c_str());
  if (!dir || !dir.isDirectory()) return;
  char name[256];
  std::vector<std::string> subfolders;
  for (auto file = dir.openNextFile(); file && games.size() < MAX_GAMES; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    const std::string entry = name;
    const std::string path = (folder == "/" ? std::string("/") : folder + "/") + entry;
    if (file.isDirectory()) {
      if (depth < MAX_SCAN_DEPTH && !skipFolder(entry)) subfolders.push_back(path);
    } else if (entry[0] != '.' && isRom(entry)) {
      addGame(path);
    }
    file.close();
  }
  dir.close();
  for (const auto& sub : subfolders) scanFolder(sub, depth + 1);
}

void GameLibraryActivity::scan() {
  games.clear();
  scanFolder("/", 0);
  // Favourites and recents found elsewhere (deeper, or picked by folder).
  for (const auto& path : lists.favorites) {
    if (Storage.exists(path.c_str())) addGame(path);
  }
  for (const auto& path : lists.recents) {
    if (Storage.exists(path.c_str())) addGame(path);
  }
  std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) { return lessTitle(a.title, b.title); });
  scanned = true;
  rebuildShown();
}

void GameLibraryActivity::rebuildShown() {
  shown.clear();
  subtitles.clear();
  auto indexOf = [this](const std::string& path) -> int {
    for (size_t i = 0; i < games.size(); ++i) {
      if (games[i].path == path) return static_cast<int>(i);
    }
    return -1;
  };
  if (filter == Filter::Recent) {
    for (const auto& path : lists.recents) {
      const int i = indexOf(path);
      if (i >= 0) shown.push_back(static_cast<size_t>(i));
    }
  } else {
    for (size_t i = 0; i < games.size(); ++i) {
      if (filter == Filter::All || GameLibraryStore::isFavorite(lists, games[i].path)) shown.push_back(i);
    }
  }
  for (const size_t i : shown) {
    std::string subtitle;
    if (GameLibraryStore::isFavorite(lists, games[i].path)) subtitle = std::string(tr(STR_GAME_FAVORITE)) + " · ";
    if (GameLibraryStore::hasSave(games[i].path)) subtitle += std::string(tr(STR_GAME_SAVED)) + " · ";
    subtitle += folderOf(games[i].path);
    subtitles.push_back(std::move(subtitle));
  }
  const int count = FIXED_ROWS + static_cast<int>(shown.size());
  if (selectedIndex >= count) selectedIndex = count - 1;
}

void GameLibraryActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<GameLibraryActivity*>(user);
  self->app.clearTapFlash();
  self->selectedIndex = event.value;
  self->activate(event.value);
  self->requestUpdate();
}

void GameLibraryActivity::play(const std::string& path) {
  ActivityResult result{FilePathResult{path}};
  result.isCancelled = false;
  setResult(std::move(result));
  finish();
}

void GameLibraryActivity::browseFolders() {
  startActivityForResult(std::make_unique<FileBrowserActivity>(renderer, mappedInput, GameBoyActivity::lastRomFolder(),
                                                               FileBrowserActivity::Mode::PickGameBoyRom),
                         [this](const ActivityResult& result) {
                           const auto* picked = std::get_if<FilePathResult>(&result.data);
                           if (!result.isCancelled && picked && !picked->path.empty()) {
                             play(picked->path);
                             return;
                           }
                           requestUpdate();
                         });
}

void GameLibraryActivity::activate(const int row) {
  if (row == 0) {
    filter = static_cast<Filter>((static_cast<uint8_t>(filter) + 1) % static_cast<uint8_t>(Filter::Count));
    topIndex = 0;
    rebuildShown();
    return;
  }
  if (row == 1) {
    browseFolders();
    return;
  }
  const int index = row - FIXED_ROWS;
  if (index < 0 || index >= static_cast<int>(shown.size())) return;
  openGameMenu(shown[static_cast<size_t>(index)]);
}

void GameLibraryActivity::openGameMenu(const size_t gameIndex) {
  const std::string path = games[gameIndex].path;
  const bool favorite = GameLibraryStore::isFavorite(lists, path);
  const bool saved = GameLibraryStore::hasSave(path);
  std::vector<std::string> options = {tr(STR_GAME_PLAY),
                                      favorite ? tr(STR_GAME_REMOVE_FAVORITE) : tr(STR_GAME_ADD_FAVORITE)};
  if (saved) options.push_back(tr(STR_GAME_DELETE_SAVE));
  optionPopup.show(games[gameIndex].title.c_str(), options, 0,
                   [this, path](const int choice) {
                     if (choice == 0) {
                       play(path);
                     } else if (choice == 1) {
                       GameLibraryStore::toggleFavorite(lists, path);
                       rebuildShown();
                       requestUpdate();
                     } else if (choice == 2) {
                       startActivityForResult(
                           std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_GAME_DELETE_SAVE),
                                                                  tr(STR_GAME_DELETE_SAVE_CONFIRM)),
                           [this, path](const ActivityResult& result) {
                             if (!result.isCancelled) GameLibraryStore::deleteSaves(path);
                             rebuildShown();
                             requestUpdate();
                           });
                     }
                   });
}

void GameLibraryActivity::loop() {
  if (!scanned) {
    requestUpdateAndWait();  // show "Searching" while the SD card is read
    scan();
    requestUpdate();
    return;
  }
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(selectedIndex);
    requestUpdate();
    return;
  }
  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }
  const int itemCount = FIXED_ROWS + static_cast<int>(shown.size());
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows;
    const int next = scrollListBy(topIndex, delta, visibleRows, itemCount);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }
  const auto moveSelection = [this, itemCount](const int index) {
    selectedIndex = index;
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, itemCount);
    requestUpdate();
  };
  buttonNavigator.onNext(
      [this, itemCount, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex, itemCount)); });
  buttonNavigator.onPrevious(
      [this, itemCount, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectedIndex, itemCount)); });
}

void GameLibraryActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<GameLibraryActivity*>(user)->buildListScreen(screen);
}

void GameLibraryActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput)), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (!scanned) {
    screen.centeredText(tr(STR_GAME_SEARCHING), screen.theme().bodyText);
    return;
  }

  const std::string filterLabel = std::string(tr(STR_GAME_SHOW)) + ": " + I18N.get(filterName(static_cast<uint8_t>(filter)));
  std::vector<fui::ListItem> items;
  items.reserve(FIXED_ROWS + shown.size());
  fui::ListItem filterRow;
  filterRow.label = filterLabel.c_str();
  filterRow.subtitle = shown.empty() ? tr(STR_NO_GAME_ROMS) : nullptr;
  filterRow.actionValue = 0;
  items.push_back(filterRow);
  fui::ListItem browse;
  browse.label = tr(STR_GAME_BROWSE_FOLDERS);
  browse.actionValue = 1;
  items.push_back(browse);
  for (size_t i = 0; i < shown.size(); ++i) {
    fui::ListItem item;
    item.label = games[shown[i]].title.c_str();
    item.subtitle = subtitles[i].c_str();
    item.actionValue = static_cast<int16_t>(FIXED_ROWS + i);
    items.push_back(item);
  }
  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  const auto rows = configureUiList(props, screen.theme(), screen.body(), UiListRowType::WithSubtitle);
  visibleRows = rows > 0 ? rows : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(items.size()));
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void GameLibraryActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  renderer.clearScreen();
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, uiTarget, header, tr(STR_GAME_LIBRARY), false);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_GAME_LIBRARY));
  }
  uiReady = false;
  app.render();
  uiReady = true;
  const auto hints =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}

#endif
