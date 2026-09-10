#ifdef SIMULATOR

#include "SimulatorSmokeTest.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SettingsList.h"
#include "activities/ActivityManager.h"
#include "activities/reader/EpubReaderMenuActivity.h"
#include "activities/reader/ReaderMenuItems.h"
#include "activities/reader/ReaderOptionsActivity.h"
#include "activities/settings/QuickActionsActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "simulator/SimulatorHomeKeyInput.h"
#if defined(CROSSINK_ENABLE_POKEMON)
#include <Memory.h>

#include "activities/pokemon/PokemonActivity.h"
#endif

extern ActivityManager activityManager;
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace {

enum class SmokeStep : uint8_t {
  Start,
  Home,
  FileBrowser,
  FileBrowserSettings,
  RecentBooks,
  Settings,
  ReaderOptions,
  ReaderMenu,
  Sleep,
  Reader,
  Pokemon,
  InputScript,
  Done,
};

#if defined(CROSSINK_ENABLE_POKEMON)
void startPokemonActivity() {
  if (std::getenv("CROSSINK_SIMULATOR_POKEMON_LANDSCAPE") != nullptr) {
    SETTINGS.orientation = CrossPointSettings::LANDSCAPE_CCW;
    renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  }
  auto activity = makeUniqueNoThrow<PokemonActivity>(renderer, mappedInputManager);
  if (!activity) {
    LOG_ERR("SMOKE", "Could not allocate Pokemon simulator activity");
    std::_Exit(2);
  }
  activityManager.replaceActivity(std::move(activity));
}
#endif

class SimulatorSmokeTest {
 public:
  void tick() {
    if (!enabled()) return;

    try {
      tickImpl();
    } catch (const std::exception& e) {
      fail("Unhandled exception: %s", e.what());
    } catch (...) {
      fail("Unhandled non-standard exception");
    }
  }

 private:
  enum class ScriptActionType : uint8_t {
    Press,
    Release,
    HomeTap,
    HomeLongPress,
    AssertTouchscreenDisabled,
    AssertTouchscreenEnabled,
    OpenSmokeBook,
    DisableReaderTouch,
    EnableReaderTouch,
    TouchDown,
    TouchMove,
    TouchRelease,
    AssertActivity,
    Render
  };

  struct ScriptAction {
    ScriptActionType type;
    MappedInputManager::Button button;
    const char* label;
    int settleFrames;
    int x;
    int y;
  };

  SmokeStep step = SmokeStep::Start;
  int settleFrames = 0;
  const char* activeStepName = nullptr;
  std::vector<ScriptAction> inputScript;
  size_t scriptIndex = 0;
  SmokeStep inputCompletionStep = SmokeStep::Done;

  static bool enabled() { return std::getenv("CROSSINK_SIMULATOR_SMOKE_TEST") != nullptr; }

  static bool pokemonMode() { return std::getenv("CROSSINK_SIMULATOR_START_POKEMON") != nullptr; }

  static bool homeNavigationMode() { return std::getenv("CROSSINK_SIMULATOR_HOME_NAVIGATION") != nullptr; }

  static bool homeNavigationUsesMinimalInteraction() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_THEME");
    if (raw == nullptr || raw[0] == '\0') return false;

    const int theme = std::atoi(raw);
    return theme == CrossPointSettings::UI_THEME::MINIMAL || theme == CrossPointSettings::UI_THEME::DASHBOARD;
  }

  static bool homeNavigationUsesCarouselInteraction() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_THEME");
    return raw != nullptr && raw[0] != '\0' && std::atoi(raw) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  }

  static int pageTurnCount() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_PAGE_TURNS");
    if (raw == nullptr || raw[0] == '\0') {
      return 2;
    }
    return std::max(0, std::atoi(raw));
  }

  static bool landscapeReaderRequested() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_LANDSCAPE_READER");
    return raw != nullptr && raw[0] != '\0' && raw[0] != '0';
  }

  static void applyRequestedTheme() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_THEME");
    if (raw == nullptr || raw[0] == '\0') {
      return;
    }

    const int theme = std::atoi(raw);
    if (theme < 0 || theme >= CrossPointSettings::UI_THEME_COUNT) {
      fail("Invalid smoke test theme index: %d", theme);
    }

    SETTINGS.uiTheme = static_cast<uint8_t>(theme);
    UITheme::getInstance().reload();
    LOG_INF("SMOKE", "Using theme index %d", theme);
  }

  static void verifyUpDownShortcutAvailability() {
    const auto allSettings = getSettingsList();
    const auto sideButtonSettings = buildControlsSideButtonSettingsList(allSettings);
    const bool hasSideButtonChord =
        std::any_of(sideButtonSettings.begin(), sideButtonSettings.end(),
                    [](const SettingInfo& setting) { return setting.nameId == StrId::STR_SIDE_BUTTON_CHORD; });
    if (hasSideButtonChord != gpio.hasTouch()) {
      fail("Side-button chord availability does not match touch capability");
    }

    if (QuickActionsActivityTest::isTriggerAvailable(QuickActions::Trigger::UpDown) != gpio.hasTouch()) {
      fail("Quick Actions Up + Down availability does not match touch capability");
    }
  }

  [[noreturn]] static void fail(const char* message) {
    LOG_ERR("SMOKE", "%s", message);
    std::_Exit(2);
  }

  template <typename... Args>
  [[noreturn]] static void fail(const char* format, Args... args) {
    logPrintf("ERR", "SMOKE", format, args...);
    logPrintf("ERR", "SMOKE", "\n");
    std::_Exit(2);
  }

  static void renderCurrentStep(const char* name) {
    LOG_INF("SMOKE", "Rendering %s", name);
    if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) {
      fail("Render was rejected for %s", name);
    }
  }

  void queueStep(const char* name, SmokeStep nextStep, int framesToSettle = 3) {
    activeStepName = name;
    settleFrames = framesToSettle;
    step = nextStep;
  }

  void tickImpl() {
    mappedInputManager.simulatorClearInputFrame();

    if (settleFrames > 0) {
      --settleFrames;
      if (settleFrames == 0 && activeStepName != nullptr) {
        renderCurrentStep(activeStepName);
        activeStepName = nullptr;
      }
      return;
    }

    switch (step) {
      case SmokeStep::Start:
        LOG_INF("SMOKE", "Starting simulator smoke test");
        if (!CrossPointSettings::verifySleepTimeoutMigrationContract()) {
          fail("Sleep timeout migration contract failed");
        }
        if (!CrossPointSettings::verifySleepScreenMigrationContract()) {
          fail("Sleep screen migration contract failed");
        }
        if (!SimulatorHomeKeyInput::verifyTimingContract()) {
          fail("Simulator Home key timing contract failed");
        }
        verifyUpDownShortcutAvailability();
        applyRequestedTheme();
#if defined(CROSSINK_ENABLE_POKEMON)
        if (pokemonMode()) {
          startPokemonActivity();
          queueStep("Pokemon Starter", SmokeStep::Pokemon, 4);
          break;
        }
#else
        if (pokemonMode()) fail("Pokemon smoke test requested without CROSSINK_ENABLE_POKEMON");
#endif
        activityManager.goHome();
        queueStep("Home", SmokeStep::Home);
        break;

      case SmokeStep::Home:
        if (homeNavigationMode()) {
          buildHomeNavigationInputScript();
          step = SmokeStep::InputScript;
          break;
        }
        activityManager.goToFileBrowser("/books");
        queueStep("File Browser", SmokeStep::FileBrowser);
        break;

      case SmokeStep::FileBrowser:
#if CROSSINK_APP_CAP_TOUCH
        if (mappedInputManager.hasTouchHardware()) {
          buildFileBrowserInputScript();
          step = SmokeStep::InputScript;
          break;
        }
#endif
        activityManager.goToRecentBooks();
        queueStep("Recent Books", SmokeStep::RecentBooks);
        break;

      case SmokeStep::FileBrowserSettings:
        activityManager.goToRecentBooks();
        queueStep("Recent Books", SmokeStep::RecentBooks);
        break;

      case SmokeStep::RecentBooks:
        if (mappedInputManager.hasHomeKey()) {
          renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
        }
        activityManager.goToSettings();
        queueStep(mappedInputManager.hasHomeKey() ? "Settings landscape" : "Settings", SmokeStep::Settings);
        break;

      case SmokeStep::Settings:
        renderer.setOrientation(GfxRenderer::Orientation::Portrait);
        activityManager.replaceActivity(std::make_unique<ReaderOptionsActivity>(renderer, mappedInputManager));
        queueStep("Reader Options", SmokeStep::ReaderOptions);
        break;

      case SmokeStep::ReaderOptions:
        activityManager.replaceActivity(
            std::make_unique<EpubReaderMenuActivity>(renderer, mappedInputManager, "Smoke Test", 1, 1, 0,
                                                     SETTINGS.orientation, false, false, false, false, false, false));
        queueStep("Reader Menu", SmokeStep::ReaderMenu);
        break;

      case SmokeStep::ReaderMenu:
        activityManager.goToSleep();
        queueStep("Sleep", SmokeStep::Sleep);
        break;

      case SmokeStep::Sleep: {
        const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (bookPath == nullptr || bookPath[0] == '\0') {
          LOG_INF("SMOKE", "Skipping Reader step; CROSSINK_SIMULATOR_SMOKE_BOOK is not set");
          step = SmokeStep::Reader;
          break;
        }
        if (!Storage.exists(bookPath)) {
          fail("Smoke test book is missing: %s", bookPath);
        }
        if (landscapeReaderRequested()) {
          SETTINGS.orientation = CrossPointSettings::LANDSCAPE_CCW;
          LOG_INF("SMOKE", "Opening smoke reader in landscape");
        }
        activityManager.goToReader(bookPath, true);
        queueStep("Reader", SmokeStep::Reader, 8);
        break;
      }

      case SmokeStep::Reader:
        buildReaderInputScript();
        step = SmokeStep::InputScript;
        break;

      case SmokeStep::Pokemon:
#if defined(CROSSINK_ENABLE_POKEMON)
#if CROSSINK_APP_CAP_TOUCH
        if (mappedInputManager.hasTouchHardware()) {
          buildPokemonTouchInputScript();
        } else {
          buildPokemonInputScript();
        }
#else
        buildPokemonInputScript();
#endif
        step = SmokeStep::InputScript;
#else
        fail("Pokemon smoke-test step is unavailable");
#endif
        break;

      case SmokeStep::InputScript:
        runInputScript();
        break;

      case SmokeStep::Done:
        LOG_INF("SMOKE", "Simulator smoke test passed");
        std::_Exit(0);
    }
  }

  static ScriptAction press(MappedInputManager::Button button) {
    return {ScriptActionType::Press, button, nullptr, 0, 0, 0};
  }

  static ScriptAction release(MappedInputManager::Button button) {
    return {ScriptActionType::Release, button, nullptr, 0, 0, 0};
  }

  static ScriptAction homeTap() {
    return {ScriptActionType::HomeTap, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction homeLongPress() {
    return {ScriptActionType::HomeLongPress, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction assertTouchscreenDisabled() {
    return {ScriptActionType::AssertTouchscreenDisabled, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction assertTouchscreenEnabled() {
    return {ScriptActionType::AssertTouchscreenEnabled, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction openSmokeBook() {
    return {ScriptActionType::OpenSmokeBook, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction disableReaderTouch() {
    return {ScriptActionType::DisableReaderTouch, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction enableReaderTouch() {
    return {ScriptActionType::EnableReaderTouch, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction render(const char* label, int framesToSettle = 3) {
    return {ScriptActionType::Render, MappedInputManager::Button::Back, label, framesToSettle, 0, 0};
  }

#if CROSSINK_APP_CAP_TOUCH
  static ScriptAction touchDown(const int x, const int y) {
    return {ScriptActionType::TouchDown, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
  static ScriptAction touchMove(const int x, const int y) {
    return {ScriptActionType::TouchMove, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
  static ScriptAction touchRelease(const int x, const int y) {
    return {ScriptActionType::TouchRelease, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
#endif

  static ScriptAction assertActivity(const char* name) {
    return {ScriptActionType::AssertActivity, MappedInputManager::Button::Back, name, 0, 0, 0};
  }

  void addTap(MappedInputManager::Button button) {
    inputScript.push_back(press(button));
    inputScript.push_back(release(button));
  }

  void buildHomeNavigationInputScript() {
    inputScript.clear();
    scriptIndex = 0;
    inputScript.push_back(assertActivity("Home"));

    // Standard themes render Browse, Recent Books, File Transfer, Pokemon,
    // then Settings. Minimal and Dashboard expose Menu, Browse, and Settings.
    if (homeNavigationUsesCarouselInteraction()) {
      for (int index = 0; index < 4; ++index) addTap(MappedInputManager::Button::Right);
    } else {
      const int downPresses = homeNavigationUsesMinimalInteraction() ? 3 : 4;
      for (int index = 0; index < downPresses; ++index) addTap(MappedInputManager::Button::Down);
    }
    inputScript.push_back(render("Home Settings selected", 3));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Settings opened from Home", 4));
    inputScript.push_back(assertActivity("Settings"));
    LOG_INF("SMOKE", "Running Home navigation to Settings using theme index %d", static_cast<int>(SETTINGS.uiTheme));
  }

  void buildReaderInputScript() {
    inputScript.clear();
    scriptIndex = 0;
    inputCompletionStep = SmokeStep::Done;

    const int turns = pageTurnCount();
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouch()) {
      const int width = renderer.getScreenWidth();
      const int height = renderer.getScreenHeight();
      if (width <= 0 || height <= 0) fail("Touch smoke test has invalid screen dimensions");
      LOG_INF("SMOKE", "Running touch reader input script with %d page turn(s)", turns);
      for (int i = 0; i < turns; ++i) {
        inputScript.push_back(touchDown(width * 5 / 6, height / 2));
        inputScript.push_back(touchRelease(width * 5 / 6, height / 2));
        inputScript.push_back(render("Reader after touch page forward", 4));
      }
      if (mappedInputManager.hasHomeKey()) {
        // X4 Pro reserves the top-edge swipe for its frontlight overlay and
        // moves the reader menu to the bottom edge.
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight Panel opened from touch gesture", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInputManager);
        inputScript.push_back(touchDown(header.x + header.width - 32, header.y + header.height / 2));
        inputScript.push_back(touchRelease(header.x + header.width - 32, header.y + header.height / 2));
        inputScript.push_back(render("Home opened by Frontlight Panel Home button", 4));
        inputScript.push_back(assertActivity("Home"));
        inputScript.push_back(openSmokeBook());
        inputScript.push_back(render("Reader reopened after Frontlight Panel Home button", 8));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight Panel reopened after Home button", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(20, height / 3));
        inputScript.push_back(touchMove(20, 8));
        inputScript.push_back(touchRelease(20, 8));
        inputScript.push_back(render("Frontlight Panel remains open after in-drawer swipe up", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        // X4 Pro's portrait frontlight sheet ends just below mid-screen; this
        // point lands in its centered 29 px handle band.
        inputScript.push_back(touchDown(width / 2, height * 21 / 40));
        inputScript.push_back(touchMove(width / 2, 8));
        inputScript.push_back(touchRelease(width / 2, 8));
        inputScript.push_back(render("Reader restored after Frontlight Panel handle drag up", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight Panel reopened from touch gesture", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        // The fourth action-bar slot opens Global Settings through the real
        // FrontlightPanelActivity callback path.
        inputScript.push_back(touchDown(width * 7 / 10, height * 15 / 32));
        inputScript.push_back(touchRelease(width * 7 / 10, height * 15 / 32));
        inputScript.push_back(render("Global Settings opened from Frontlight Panel", 4));
        inputScript.push_back(assertActivity("Settings"));
        inputScript.push_back(touchDown(width / 2, height * 3 / 4));
        inputScript.push_back(touchMove(width / 2, height / 2));
        inputScript.push_back(touchRelease(width / 2, height / 2));
        inputScript.push_back(render("Global Settings remains open after interior swipe up", 4));
        inputScript.push_back(assertActivity("Settings"));
        inputScript.push_back(touchDown(width / 2, height - 8));
        inputScript.push_back(touchMove(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
        inputScript.push_back(render("Reader restored after Settings bottom-edge swipe", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight Panel reopened after Global Settings", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(width * 3 / 10, height * 3 / 8));
        inputScript.push_back(touchRelease(width * 3 / 10, height * 3 / 8));
        inputScript.push_back(render("Sync dialog opened from Frontlight Panel", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(width / 2, height - 60));
        inputScript.push_back(touchRelease(width / 2, height - 60));
        inputScript.push_back(render("Reader restored after dismissing Frontlight sync dialog", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(homeLongPress());
        inputScript.push_back(render("Reader Menu opened from simulated Home key hold", 4));
        inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
        inputScript.push_back(touchDown(width / 2, height / 2 + 24));
        inputScript.push_back(touchRelease(width / 2, height / 2 + 24));
        inputScript.push_back(render("Reader Font opened from touch reader menu", 4));
        inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
        inputScript.push_back(homeTap());
        inputScript.push_back(render("Reader Menu root restored by simulated Home key tap", 4));
        inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
        inputScript.push_back(homeTap());
        inputScript.push_back(render("Reader restored by simulated Home key tap at drawer root", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(homeLongPress());
        inputScript.push_back(render("Reader Menu reopened from simulated Home key hold", 4));
        inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
        inputScript.push_back(touchDown(width / 2, height * 3 / 4));
        inputScript.push_back(touchMove(width / 2, height - 8));
        inputScript.push_back(touchRelease(width / 2, height - 8));
        inputScript.push_back(render("Reader Menu remains open after in-drawer swipe down", 4));
        inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
        inputScript.push_back(touchDown(width / 2, height / 2 - 14));
        inputScript.push_back(touchMove(width / 2, height - 8));
        inputScript.push_back(touchRelease(width / 2, height - 8));
        inputScript.push_back(render("Reader restored after Reader Menu handle drag down", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(disableReaderTouch());
        inputScript.push_back(homeLongPress());
        inputScript.push_back(render("Reader Menu opened from Home key hold with touch disabled", 4));
        inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
        inputScript.push_back(touchDown(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Reader restored after Home key menu with touch disabled", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(homeTap());
        inputScript.push_back(render("Home opened from simulated Home key tap", 4));
        inputScript.push_back(assertActivity("Home"));
        inputScript.push_back(enableReaderTouch());
        inputScript.push_back(openSmokeBook());
        inputScript.push_back(render("Reader reopened after simulated Home key tap", 8));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, height - 8));
        inputScript.push_back(touchMove(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      } else {
        // Sticky uses the same vertical gesture split as X4 Pro: swipe down
        // opens reader details/actions and swipe up opens the bottom menu.
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Sticky Reader Details opened from touch gesture", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(20, height / 3));
        inputScript.push_back(touchMove(20, 8));
        inputScript.push_back(touchRelease(20, 8));
        inputScript.push_back(render("Sticky Reader Details remains open after in-drawer swipe up", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
        inputScript.push_back(render("Reader restored after Sticky details outside tap", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, height - 8));
        inputScript.push_back(touchMove(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      }
      inputScript.push_back(render("Reader Menu opened from touch gesture", 4));
      inputScript.push_back(assertActivity("EpubReaderTouchMenu"));

      // Touch every bottom-drawer tab slot, then dismiss from its handle.
      const int tabY = height - 28;
      for (int tab = 0; tab < static_cast<int>(READER_DRAWER_TAB_COUNT); ++tab) {
        const int tabX = width * (tab * 2 + 1) / (static_cast<int>(READER_DRAWER_TAB_COUNT) * 2);
        inputScript.push_back(touchDown(tabX, tabY));
        inputScript.push_back(touchRelease(tabX, tabY));
        inputScript.push_back(render("Touch Reader Menu tab", 3));
        inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
      }

      const int moreTabX = width / 2;
      const int drawerTop = height / 2;
      constexpr int rootRowStep = 60;
      constexpr int rootRowCenterOffset = 31;
      inputScript.push_back(touchDown(moreTabX, tabY));
      inputScript.push_back(touchRelease(moreTabX, tabY));
      inputScript.push_back(render("Touch Reader Menu More tab", 3));
      inputScript.push_back(touchDown(width / 2, drawerTop + rootRowStep + rootRowCenterOffset));
      inputScript.push_back(touchRelease(width / 2, drawerTop + rootRowStep + rootRowCenterOffset));
      inputScript.push_back(render("Touch Reader Go to Percent pane", 4));
      inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
      inputScript.push_back(touchDown(20, drawerTop + 26));
      inputScript.push_back(touchRelease(20, drawerTop + 26));
      inputScript.push_back(render("Touch Reader More tab restored", 3));
      inputScript.push_back(touchDown(width / 2, drawerTop + rootRowStep * 2 + rootRowCenterOffset));
      inputScript.push_back(touchRelease(width / 2, drawerTop + rootRowStep * 2 + rootRowCenterOffset));
      inputScript.push_back(render("Touch Reader Auto Page Turn pane", 4));
      inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
      inputScript.push_back(touchDown(20, drawerTop + 26));
      inputScript.push_back(touchRelease(20, drawerTop + 26));
      inputScript.push_back(render("Touch Reader More tab restored", 3));
      inputScript.push_back(touchDown(width / 2, height * 3 / 4));
      inputScript.push_back(touchMove(width / 2, height - 8));
      inputScript.push_back(touchRelease(width / 2, height - 8));
      inputScript.push_back(render("Reader Menu remains open after in-drawer swipe down", 4));
      inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
      inputScript.push_back(touchDown(width / 2, drawerTop - 14));
      inputScript.push_back(touchRelease(width / 2, drawerTop - 14));
      inputScript.push_back(render("Reader restored after drawer handle tap", 4));
      inputScript.push_back(assertActivity("EpubReader"));

      inputScript.push_back(touchDown(width / 2, height - 8));
      inputScript.push_back(touchMove(width / 2, height * 3 / 4));
      inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      inputScript.push_back(render("Reader Menu reopened for bottom-edge Home gesture", 4));
      inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
      inputScript.push_back(touchDown(width / 2, height * 3 / 4));
      inputScript.push_back(touchMove(width / 2, height / 2 + 8));
      inputScript.push_back(touchRelease(width / 2, height / 2 + 8));
      inputScript.push_back(render("Reader Menu remains open after interior swipe up", 4));
      inputScript.push_back(assertActivity("EpubReaderTouchMenu"));
      inputScript.push_back(touchDown(width / 2, height - 8));
      inputScript.push_back(touchMove(width / 2, height * 3 / 4));
      inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      inputScript.push_back(render("Home opened from Reader Menu bottom-edge swipe", 6));
      inputScript.push_back(assertActivity("Home"));
      return;
    }
#endif
    for (int i = 0; i < turns; i++) {
      addTap(MappedInputManager::Button::PageForward);
      inputScript.push_back(render("Reader after page forward", 4));
    }

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Menu opened from EPUB", 4));
    inputScript.push_back(assertActivity("EpubReaderMenu"));

    const ReaderMenuTabs menuItems = buildReaderMenuItems({});
    const int readerOptionsIndex = menuItems.main.indexOf(ReaderMenuAction::READER_OPTIONS);
    if (readerOptionsIndex < 0) {
      LOG_ERR("SMOKE", "Reader Options is missing from the reader menu model");
      std::_Exit(2);
    }
    // The menu opens with its tab strip focused. One Down enters row zero;
    // the remaining presses follow the shared menu model to Reader Options.
    for (int index = 0; index <= readerOptionsIndex; ++index) {
      addTap(MappedInputManager::Button::Down);
    }
    inputScript.push_back(render("Reader Menu Reader Options selection", 3));

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Options opened from Reader Menu", 4));
    inputScript.push_back(assertActivity("ReaderOptions"));

    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("Reader Options after navigation", 3));

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Options after toggle", 3));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader Options after closing option submenu", 4));
    inputScript.push_back(assertActivity("ReaderOptions"));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader Menu after closing Reader Options", 4));
    inputScript.push_back(assertActivity("EpubReaderMenu"));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader Menu tab focus restored", 4));
    inputScript.push_back(assertActivity("EpubReaderMenu"));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader after closing Reader Menu", 4));
    inputScript.push_back(assertActivity("EpubReader"));

    LOG_INF("SMOKE", "Running reader input script with %d page turn(s)", turns);
  }

#if defined(CROSSINK_ENABLE_POKEMON)
  void buildPokemonInputScript() {
    inputScript.clear();
    scriptIndex = 0;

    // Exercise the X3 front navigation rocker on its press edge. Side-button
    // Down is a separate hardware path and cannot validate the front controls.
    inputScript.push_back(press(MappedInputManager::Button::Right));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Gender", 4));
    inputScript.push_back(assertActivity("Pokemon"));
    inputScript.push_back(release(MappedInputManager::Button::Right));

    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("Pokemon Gender Female", 3));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Nickname Question", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("Pokemon Nickname No", 3));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Menu", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Party", 4));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Actions", 4));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Summary", 4));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Actions Restored", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Pokemon Party Restored", 4));
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Pokemon Menu Restored", 4));

    addTap(MappedInputManager::Button::Down);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Pokedex", 4));
    // Right/Left step one row at a time; Down/Up jump a full page (Stage 10 -
    // see PokemonActivity::loop()). Pokedex has 151 rows, far more than fit
    // on one page, so Down here would jump by a whole page each press and
    // land on an unseen (still "???") species a long way past the starter -
    // activate() correctly no-ops on those, silently leaving every following
    // step operating on the wrong screen. Use Right to land exactly on
    // index 3 (species 4, Charmander - the caught/seen starter), which
    // activate() actually opens.
    for (int i = 0; i < 3; ++i) addTap(MappedInputManager::Button::Right);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Pokedex Detail", 4));
    addTap(MappedInputManager::Button::Back);
    // A single Down here (unlike above) is intentional and safe: it's the
    // real page-jump this button performs, landing on some unseen species a
    // page forward - fine since nothing here calls Confirm on it, this step
    // only checks that paging through Pokedex doesn't crash.
    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("Pokemon Pokedex Second Page", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    addTap(MappedInputManager::Button::Back);
    for (int i = 0; i < 2; ++i) addTap(MappedInputManager::Button::Down);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Empty PC", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    LOG_INF("SMOKE", "Running Pokemon input script");
  }

#if CROSSINK_APP_CAP_TOUCH
  // Phase 3 (X4 Pro touch support) audit: the button-driven script above
  // proves the Pokemon activity still works with no touch input at all, but
  // never exercises a single tap - X4 Pro has no physical d-pad, so every
  // list screen must be genuinely reachable by touch. Reruns the same
  // Menu -> Party -> Actions -> Summary -> Pokedex -> PC path, but selects
  // and activates every row with a tap at that row's actual on-screen
  // position (computed the same way PokemonActivity::listTop()/rowHeightFor
  // Screen() do, from the same public metrics) instead of Down+Confirm, and
  // leaves screens via a tap on the header's Back button instead of the
  // physical Back button.
  void buildPokemonTouchInputScript() {
    inputScript.clear();
    scriptIndex = 0;

    // Onboarding (Starter/Gender/Nickname) isn't part of Phase 3's
    // list-screen audit - drive it the same way as the button script so both
    // scripts reach an identical starting save state.
    inputScript.push_back(press(MappedInputManager::Button::Right));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Gender (touch script)", 4));
    inputScript.push_back(assertActivity("Pokemon"));
    inputScript.push_back(release(MappedInputManager::Button::Right));

    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("Pokemon Gender Female (touch script)", 3));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Nickname Question (touch script)", 4));

    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("Pokemon Nickname No (touch script)", 3));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Pokemon Menu (touch script)", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    const auto& metrics = UITheme::getInstance().getMetrics();
    const int listTop =
        metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInputManager) + metrics.verticalSpacing;
    const int centerX = renderer.getScreenWidth() / 2;
    const auto tapRow = [&](const int index, const int rowHeight) {
      const int y = listTop + index * rowHeight + rowHeight / 2;
      inputScript.push_back(touchDown(centerX, y));
      inputScript.push_back(touchRelease(centerX, y));
    };
    const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInputManager);
    const auto backLayout = TouchHeaderBackButton::layout(header);
    const int backX = header.x + header.width - backLayout.iconRect.width / 2;
    const int backY = backLayout.iconRect.y + backLayout.iconRect.height / 2;
    const auto tapBack = [&] {
      inputScript.push_back(touchDown(backX, backY));
      inputScript.push_back(touchRelease(backX, backY));
    };
    // Screen::Message (e.g. the BagBalls info popup below) has no header
    // Back button of its own reason to leave via - it's dismissed by tapping
    // the message body itself (wasScreenTapped() in loop()), so use a plain
    // center-of-screen tap for those instead of tapBack().
    const int centerY = renderer.getScreenHeight() / 2;
    const auto tapCenter = [&] {
      inputScript.push_back(touchDown(centerX, centerY));
      inputScript.push_back(touchRelease(centerX, centerY));
    };

    // Menu row 0 = Party.
    tapRow(0, 64);
    inputScript.push_back(render("Pokemon Party via touch", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    // Party row 0 = the starter (96px rows - reserves space for the HP bar
    // strip renderPartyRowHealth() draws under the name/level line).
    tapRow(0, 96);
    inputScript.push_back(render("Pokemon Actions via touch", 4));

    // Actions row 0 = Summary (collectionActions() always appends it first).
    tapRow(0, 64);
    inputScript.push_back(render("Pokemon Summary via touch", 4));

    tapBack();
    inputScript.push_back(render("Pokemon Actions Restored via touch", 4));

    // Actions row 1 = Moveset (collectionActions() always appends it
    // second, right after Summary).
    tapRow(1, 64);
    inputScript.push_back(render("Pokemon Moveset via touch", 4));
    tapBack();
    inputScript.push_back(render("Pokemon Actions Restored 2 via touch", 4));

    tapBack();
    inputScript.push_back(render("Pokemon Party Restored via touch", 4));
    tapBack();
    inputScript.push_back(render("Pokemon Menu Restored via touch", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    // Menu row 1 = Pokedex.
    tapRow(1, 64);
    inputScript.push_back(render("Pokemon Pokedex via touch", 4));

    // Row 3 - matches the button script's "3x Down from the top" entry, a
    // species already seen from starter selection.
    tapRow(3, 64);
    inputScript.push_back(render("Pokemon Pokedex Detail via touch", 4));

    tapBack();
    inputScript.push_back(render("Pokemon Pokedex Restored via touch", 4));
    tapBack();
    inputScript.push_back(render("Pokemon Menu Restored 2 via touch", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    // Menu row 2 = PC Box (empty on a fresh save - still a real fui::list()
    // screen showing the empty-state message, per buildUi()).
    tapRow(2, 64);
    inputScript.push_back(render("Pokemon Empty PC via touch", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    tapBack();
    inputScript.push_back(render("Pokemon Menu Restored 3 via touch", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    // Menu row 4 = Bag (a category-select screen: Evolution/Medicine/
    // Balls/Machine, in that row order - see activate()'s Screen::Bag case).
    tapRow(4, 64);
    inputScript.push_back(render("Pokemon Bag via touch", 4));

    // Row 0 = Evolution stones (empty on a fresh save).
    tapRow(0, 64);
    inputScript.push_back(render("Pokemon Bag Evolution via touch", 4));
    tapBack();
    inputScript.push_back(render("Pokemon Bag Restored via touch", 4));

    // Row 1 = Medicine (empty on a fresh save).
    tapRow(1, 64);
    inputScript.push_back(render("Pokemon Bag Medicine via touch", 4));
    tapBack();
    inputScript.push_back(render("Pokemon Bag Restored 2 via touch", 4));

    // Row 2 = Balls - createStarter() grants 10 Poke Balls, so this is
    // non-empty; tapping the one owned row opens a view-only info message
    // (Screen::Message) instead of doing anything, which doubles as
    // coverage for the Message tap-to-dismiss path added alongside the
    // Battle grid touch support.
    tapRow(2, 64);
    inputScript.push_back(render("Pokemon Bag Balls via touch", 4));
    tapRow(0, 64);
    inputScript.push_back(render("Pokemon Bag Balls Info via touch", 4));
    tapCenter();
    inputScript.push_back(render("Pokemon Bag Balls Restored via touch", 4));
    tapBack();
    inputScript.push_back(render("Pokemon Bag Restored 3 via touch", 4));

    // Row 3 = Machine (TM/HM, empty on a fresh save).
    tapRow(3, 64);
    inputScript.push_back(render("Pokemon Bag Machine via touch", 4));
    tapBack();
    inputScript.push_back(render("Pokemon Bag Restored 4 via touch", 4));

    tapBack();
    inputScript.push_back(render("Pokemon Menu Restored 4 via touch", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    // Menu row 5 = Gym Battle (GymList) - view only, deliberately not
    // tapping a gym row since that would launch a real battle.
    tapRow(5, 64);
    inputScript.push_back(render("Pokemon Gym List via touch", 4));
    tapBack();
    inputScript.push_back(render("Pokemon Menu Restored 5 via touch", 4));

    // Menu row 6 = Badges - view only, same reasoning.
    tapRow(6, 64);
    inputScript.push_back(render("Pokemon Badges via touch", 4));
    tapBack();
    inputScript.push_back(render("Pokemon Menu Restored 6 via touch", 4));
    inputScript.push_back(assertActivity("Pokemon"));

    LOG_INF("SMOKE", "Running Pokemon touch input script");
  }
#endif

  static void verifyPokemonSmokeState() {
    pokemon::PokemonSnapshot snapshot{};
    if (pokemon::devicePokemonService().loadSnapshot(snapshot) != pokemon::ServiceStatus::Ok) {
      fail("Pokemon smoke-test save could not be loaded");
    }
    if (snapshot.ownedCount != 1 || snapshot.partyCount != 1 || snapshot.party[0].speciesId != 4 ||
        snapshot.party[0].gender != pokemon::Gender::Female) {
      fail("Pokemon smoke-test onboarding state was not persisted as expected");
    }
    LOG_INF("SMOKE", "Pokemon onboarding state verified");
  }
#endif

#if CROSSINK_APP_CAP_TOUCH
  void buildFileBrowserInputScript() {
    inputScript.clear();
    scriptIndex = 0;
    inputCompletionStep = SmokeStep::FileBrowserSettings;

    if (mappedInputManager.hasHomeKey()) {
      const int width = renderer.getScreenWidth();
      const int height = renderer.getScreenHeight();
      inputScript.push_back(touchDown(width / 2, 8));
      inputScript.push_back(touchMove(width / 2, height / 4));
      inputScript.push_back(touchRelease(width / 2, height / 4));
      inputScript.push_back(render("Frontlight Panel opened outside Reader", 4));
      inputScript.push_back(assertActivity("FrontlightPanel"));
      inputScript.push_back(touchDown(width * 9 / 10, height / 2));
      inputScript.push_back(touchRelease(width * 9 / 10, height / 2));
      inputScript.push_back(render("Reader touchscreen disabled from Frontlight Panel outside Reader", 4));
      inputScript.push_back(assertActivity("FrontlightPanel"));
      inputScript.push_back(touchDown(width / 2, height - 60));
      inputScript.push_back(touchRelease(width / 2, height - 60));
      inputScript.push_back(render("File Browser restored after disabling reader touchscreen", 4));
      inputScript.push_back(assertActivity("FileBrowser"));
      inputScript.push_back(assertTouchscreenDisabled());
      inputScript.push_back(touchDown(width / 2, 8));
      inputScript.push_back(touchMove(width / 2, height / 4));
      inputScript.push_back(touchRelease(width / 2, height / 4));
      inputScript.push_back(render("Frontlight Panel reopened outside Reader", 4));
      inputScript.push_back(assertActivity("FrontlightPanel"));
      inputScript.push_back(touchDown(width * 9 / 10, height / 2));
      inputScript.push_back(touchRelease(width * 9 / 10, height / 2));
      inputScript.push_back(render("Reader touchscreen enabled from Frontlight Panel outside Reader", 4));
      inputScript.push_back(assertActivity("FrontlightPanel"));
      inputScript.push_back(touchDown(width / 2, height - 60));
      inputScript.push_back(touchRelease(width / 2, height - 60));
      inputScript.push_back(render("File Browser restored after enabling reader touchscreen", 4));
      inputScript.push_back(assertActivity("FileBrowser"));
      inputScript.push_back(assertTouchscreenEnabled());
    }

    const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInputManager);
    const auto backLayout = TouchHeaderBackButton::layout(header);
    const int x = header.x + header.width - backLayout.iconRect.width / 2;
    const int y = backLayout.iconRect.y + backLayout.iconRect.height / 2;
    inputScript.push_back(touchDown(x, y));
    inputScript.push_back(touchRelease(x, y));
    inputScript.push_back(render("File Browser Settings opened from header shortcut", 4));
    inputScript.push_back(assertActivity("FileBrowserSettings"));
    const int rowY = header.y + header.height + 32;
    inputScript.push_back(touchDown(renderer.getScreenWidth() / 2, rowY));
    inputScript.push_back(touchRelease(renderer.getScreenWidth() / 2, rowY));
    inputScript.push_back(render("File Browser Settings toggle without row highlight", 4));
    inputScript.push_back(assertActivity("FileBrowserSettings"));
  }
#endif

  void runInputScript() {
    if (scriptIndex >= inputScript.size()) {
#if defined(CROSSINK_ENABLE_POKEMON)
      if (pokemonMode()) verifyPokemonSmokeState();
#endif
      step = inputCompletionStep;
      return;
    }

    const auto& action = inputScript[scriptIndex++];
    switch (action.type) {
      case ScriptActionType::Press:
        mappedInputManager.simulatorInjectPress(action.button);
        break;
      case ScriptActionType::Release:
        mappedInputManager.simulatorInjectRelease(action.button);
        break;
      case ScriptActionType::HomeTap:
        simulatorHomeKeyInput.injectTap();
        break;
      case ScriptActionType::HomeLongPress:
        simulatorHomeKeyInput.injectLongPress();
        break;
      case ScriptActionType::AssertTouchscreenDisabled:
        if (!SETTINGS.disableReaderTouchscreen) fail("Expected reader touchscreen to be disabled");
        break;
      case ScriptActionType::AssertTouchscreenEnabled:
        if (SETTINGS.disableReaderTouchscreen) fail("Expected reader touchscreen to be enabled");
        break;
      case ScriptActionType::OpenSmokeBook: {
        const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (bookPath == nullptr || bookPath[0] == '\0') fail("Smoke test book path is missing");
        activityManager.goToReader(bookPath, true);
        break;
      }
      case ScriptActionType::DisableReaderTouch:
        SETTINGS.disableReaderTouchscreen = true;
        break;
      case ScriptActionType::EnableReaderTouch:
        SETTINGS.disableReaderTouchscreen = false;
        break;
      case ScriptActionType::TouchDown:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchDown(action.x, action.y);
#endif
        break;
      case ScriptActionType::TouchMove:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchMove(action.x, action.y);
#endif
        break;
      case ScriptActionType::TouchRelease:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchRelease(action.x, action.y);
#endif
        break;
      case ScriptActionType::AssertActivity:
        if (!activityManager.isCurrentActivityNamed(action.label)) fail("Expected current activity: %s", action.label);
        break;
      case ScriptActionType::Render:
        queueStep(action.label, SmokeStep::InputScript, action.settleFrames);
        break;
    }
  }
};

SimulatorSmokeTest smokeTest;

}  // namespace

void applySimulatorSmokeTestTheme() {
  const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_THEME");
  if (raw == nullptr || raw[0] == '\0') {
    return;
  }

  const int theme = std::atoi(raw);
  if (theme < 0 || theme >= CrossPointSettings::UI_THEME_COUNT) {
    LOG_ERR("SMOKE", "Invalid smoke test theme index: %d", theme);
    std::_Exit(2);
  }

  SETTINGS.uiTheme = static_cast<uint8_t>(theme);
  LOG_INF("SMOKE", "Using theme index %d", theme);
}

void runSimulatorSmokeTestTick() {
#if defined(CROSSINK_ENABLE_POKEMON)
  static bool manualPokemonStarted = false;
  if (!manualPokemonStarted && std::getenv("CROSSINK_SIMULATOR_SMOKE_TEST") == nullptr &&
      std::getenv("CROSSINK_SIMULATOR_START_POKEMON") != nullptr) {
    manualPokemonStarted = true;
    startPokemonActivity();
  }
#endif
  smokeTest.tick();
}

#endif
