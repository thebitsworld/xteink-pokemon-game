#include "GameBoyActivity.h"

#if CROSSINK_GAMEBOY

#include <AppVersion.h>
#include <Arduino.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>
#ifndef SIMULATOR
#include <BoardConfig.h>
#include <esp_memory_utils.h>
#include <esp_system.h>
#include <esp_timer.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

#include "CrossPointState.h"
#include "components/TouchRegistry.h"
#include "fontIds.h"

namespace {
constexpr char TAG[] = "GAMEBOY";
constexpr size_t ROM_HEADER_SIZE = 0x150;
constexpr size_t PEANUT_ROM_BANK_BYTES = 0x4000;
constexpr int TOUCH_TARGET_GAP = 4;

bool isBatteryBackedCartType(const uint8_t type) {
  switch (type) {
    case 0x03:  // MBC1 + RAM + BATTERY
    case 0x06:  // MBC2 + BATTERY
    case 0x09:  // ROM + RAM + BATTERY
    case 0x0F:  // MBC3 + TIMER + BATTERY
    case 0x10:  // MBC3 + TIMER + RAM + BATTERY
    case 0x13:  // MBC3 + RAM + BATTERY
    case 0x1B:  // MBC5 + RAM + BATTERY
    case 0x1E:  // MBC7 + RAM + BATTERY (rejected by Peanut-GB)
      return true;
    default:
      return false;
  }
}

bool hitRect(const int x, const int y, const int rx, const int ry, const int rw, const int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

uint64_t nowMicros() {
#ifdef SIMULATOR
  return static_cast<uint64_t>(micros());
#else
  return static_cast<uint64_t>(esp_timer_get_time());
#endif
}

// The screen refresh a game uses (GameBoyActivity::refreshPreset). The panel's
// built-in Fast waveform takes ~540 ms on the X4 Pro's UC8279, about 1.3
// pictures a second. Measured on a UC8279 (variant 0x68) unit across the
// 1.8.1-1.8.3 test builds, "fast" runs a host-loaded DU waveform (the stock X3
// UC8279 bank) at 30% of its frames, the PLL frame rate at 0x0F and the
// display SPI at 20 MHz (the controller's rating; 10 MHz otherwise), and sends
// and drives only the rectangle that changed since the last picture (an
// unchanged picture is skipped): ~155 ms of waveform, about 5 pictures a
// second, with the darkness of stock and a little ghosting that the cleaning
// refresh every minute clears. 40% looked the same and 25% runs the same
// frames (phases of one frame do not shrink); PLL 0x1F and 0x3F were faster
// but faint, 0x10 no different. SETTINGS offers 50/40/30/25% (50% slower but
// sharper, with less ghosting) with a cleaning refresh every 30 or 60 s; D40
// with a cleaning refresh every minute is the default. Drive levels stay the
// bank's and voltages the panel's. Other controllers and variants keep stock.
struct RefreshPreset {
  const char* label;
  const char* key;  // how gameboy.cfg names it (speed=)
  uint8_t tsset;
  uint8_t pll;
  uint8_t duPct;
  uint32_t spiHz;    // display SPI clock, 0 = the driver's (10 MHz on the X4 Pro)
  bool diffWindow;   // only the changed rectangle (HalDisplay::setFastRefreshDiffWindow)
  uint8_t cleanSec;  // seconds between cleaning refreshes (0 = none)
  // Pulse drive instead of whole refreshes: waveform frames per pulse (0 =
  // not a pulse preset) and the pulses a pixel gets toward black and white.
  uint8_t pulseFrames;
  uint8_t pulseBlack;
  uint8_t pulseWhite;
  // Pulses every pixel of the area that changed gets once every pixel is
  // done with its pulses and the picture has stood still (PulsePlanner::
  // polish; 0 = none), against ghosting; and after how long (ms) without a
  // polish one comes as soon as every pixel is done, still or not (0 = only
  // when still).
  uint8_t pulsePolish;
  uint16_t pulsePolishEveryMs;
};
constexpr RefreshPreset kRefreshPresets[] = {
    {"Standard", "standard", 0, 0, 0, 0, false, 0},
    {"D50, clean 30 s", "D50-30", 0, 0x0F, 50, 20000000, true, 30},
    {"D50, clean 60 s", "D50-60", 0, 0x0F, 50, 20000000, true, 60},
    {"D40, clean 30 s", "D40-30", 0, 0x0F, 40, 20000000, true, 30},
    {"D40, clean 60 s", "D40-60", 0, 0x0F, 40, 20000000, true, 60},
    {"D30, clean 30 s", "D30-30", 0, 0x0F, 30, 20000000, true, 30},
    {"D30, clean 60 s", "D30-60", 0, 0x0F, 30, 20000000, true, 60},
    {"D25, clean 30 s", "D25-30", 0, 0x0F, 25, 20000000, true, 30},
    {"D25, clean 60 s", "D25-60", 0, 0x0F, 25, 20000000, true, 60},
    // Pulse drive: 8 pulses of 3 frames per change (the 1.8.5 betas: fewer
    // frames blurred, frame rate 0x1F ghosted more). polish adds 4 pulses to
    // the area that changed once every pixel is done and the picture stands
    // still; polish+ stops waiting for it to stand still once 1.5 s have gone
    // by without a polish. Polishing while some pixels were still changing
    // left the screen patchy, so neither does.
    {"Pulse 8x3, clean 60 s", "P8x3-60", 0, 0x0F, 0, 20000000, false, 60, 3, 8, 8, 0, 0},
    {"Pulse polish, clean 60 s", "P8x3P-60", 0, 0x0F, 0, 20000000, false, 60, 3, 8, 8, 4, 0},
    {"Pulse polish+, clean 60 s", "P8x3PP-60", 0, 0x0F, 0, 20000000, false, 60, 3, 8, 8, 4, 1500},
};
constexpr uint8_t kRefreshPresetCount = sizeof(kRefreshPresets) / sizeof(kRefreshPresets[0]);
constexpr uint8_t kDefaultFastPreset = 4;  // D40, clean 60 s
// gameboy.cfg from 1.8.3-beta.3/4 stored speed= as an index into the list
// before D50 was added: D40/30, D40/60, D30/30, D30/60, D25/30, D25/60.
constexpr const char* kOldSpeedKeys[] = {"", "D40-30", "D40-60", "D30-30", "D30-60", "D25-30", "D25-60"};

// SETTINGS > Colours, in the order of WALNUT_COLOURS_* (walnut_cgb_port.h).
struct ColourModeChoice {
  const char* label;
  const char* key;  // how gameboy.cfg names it (colours=)
};
constexpr ColourModeChoice kColourModes[] = {
    {"Smooth", "smooth"},
    {"Contrast", "contrast"},
    {"Brightness", "brightness"},
};
constexpr uint8_t kColourModeCount = sizeof(kColourModes) / sizeof(kColourModes[0]);

// The orientations SETTINGS offers, in its order.
struct OrientationChoice {
  const char* label;
  GfxRenderer::Orientation orientation;
  bool portrait;
};
constexpr OrientationChoice kOrientations[] = {
    {"Portrait", GfxRenderer::Portrait, true},
    {"Landscape", GfxRenderer::LandscapeCounterClockwise, false},
    {"Portrait, flipped", GfxRenderer::PortraitInverted, true},
    {"Landscape, flipped", GfxRenderer::LandscapeClockwise, false},
};
constexpr uint8_t kOrientationCount = sizeof(kOrientations) / sizeof(kOrientations[0]);

// The settings every game shares.
constexpr char kSettingsPath[] = "/.crosspoint/gameboy.cfg";

// A quick-save or resume file: this header, Peanut-GB's whole gb_s (its
// callback pointers are put back on load) and the cartridge RAM.
struct StateHeader {
  char magic[4];  // "GBQS"
  uint16_t version;
  uint16_t reserved;
  uint32_t gbSize;
  uint32_t cartRamSize;
  uint32_t romSize;
  char title[16];
};
constexpr uint16_t kStateVersion = 1;

// Whether the status line (screen updates per second, emulator speed) shows
// under the title; a tap on the title strip or SETTINGS toggles it. Saved
// with the other settings.
std::atomic<bool> statusLineShown{false};

// Whether this reader's screen is one the fast refresh was validated on.
bool fastRefreshValidated() {
#ifdef SIMULATOR
  return false;
#else
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279 &&
         BoardConfig::ACTIVE.displayControllerVariant == 0x68;
#endif
}

// The e-ink controller the reader detected at boot (X4 Pro batches differ:
// SSD1677, UC8179 or UC8279) and its variant byte, for the speed log:
// how fast a refresh can be depends on it.
void panelName(char* out, const size_t size) {
#ifdef SIMULATOR
  snprintf(out, size, "simulator");
#else
  const char* name = "other";
  switch (BoardConfig::ACTIVE.displayController) {
    case BoardConfig::DisplayController::SSD1677: name = "SSD1677"; break;
    case BoardConfig::DisplayController::UC8179: name = "UC8179"; break;
    case BoardConfig::DisplayController::UC8279: name = "UC8279"; break;
    case BoardConfig::DisplayController::UC8253: name = "UC8253"; break;
    default: break;
  }
  snprintf(out, size, "%s-%02X", name, BoardConfig::ACTIVE.displayControllerVariant);
#endif
}

// Whether panel pixel (x, y) is black for Game Boy shade 0 (white, the
// lightest) to 3 (black). The panel here is black and white only, so the two
// greys are dithered: one dot in four for light grey, three in four for dark.
// The picture's greys: 17 levels of darkness, 0 white to 16 black, drawn as an
// ordered (4x4 Bayer) dither - a dot is black where its threshold is below the
// level. Levels 4, 8 and 12 are the quarter, half (checkerboard) and
// three-quarter patterns; a Game Boy's four shades use 0, 4, 12 and 16
// (kDmgLevel), and Game Boy Color colours any level (src/walnut_cgb_impl.c).
constexpr int kGreyLevels = 17;
constexpr uint8_t kDmgLevel[4] = {0, 4, 12, 16};
constexpr uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

bool shadeIsBlack(const uint8_t level, const int x, const int y) { return kBayer4[y & 3][x & 3] < level; }

}  // namespace

GameBoyActivity::GameBoyActivity(GfxRenderer& rendererIn, MappedInputManager& mappedInputIn, std::string romPathIn,
                                 const bool resume)
    : Activity("GameBoy", rendererIn, mappedInputIn), romPath(std::move(romPathIn)), resumeRequested(resume) {}

GameBoyActivity::~GameBoyActivity() { cleanup(); }

void GameBoyActivity::onEnter() {
  Activity::onEnter();
  cleanup();

  exitRequested = false;
  saveDirty = false;
  physicalJoypadMask = 0;
  touchJoypadMask = 0;
  activeTouch = TouchControl::None;
  nextFrameUs = 0;
  framesSinceDraw = DRAW_EVERY;
  nextDisplayUs = 0;
  displayDue.store(true, std::memory_order_release);
  error[0] = '\0';
  title[0] = '\0';

  menuOpen.store(false, std::memory_order_release);
  menuSel = 0;
  loadArmed = false;
  menuMessage[0] = '\0';
  settingsDirty = false;
  loadSettings();
  {
    const size_t slash = romPath.find_last_of('/');
    const std::string folder = slash == std::string::npos || slash == 0 ? "/" : romPath.substr(0, slash);
    if (!romPath.empty() && folder != romFolder && folder.size() < 600) {
      romFolder = folder;
      settingsDirty = true;  // saved on exit, for the ROM picker
    }
  }
  portrait = kOrientations[orientation].portrait;
  renderer.setOrientation(kOrientations[orientation].orientation);
  setLayout();
  renderer.clearScreen(0xFF);
  chromeKey = CHROME_NONE;

  if (romPath.empty()) {
    showError("No Game Boy ROM selected");
    return;
  }

  if (!loadRom() || !buildSavePaths() || !initEmulator() || !loadSave()) {
    return;
  }

  // Woken in this game: pick up where the reader went to sleep. A resume file
  // is used once; one left from an earlier sleep is stale on a normal launch.
  if (resumeRequested && Storage.exists(resumePath.c_str())) {
    if (!readState(resumePath)) LOG_ERR(TAG, "Could not resume from %s", resumePath.c_str());
  }
  if (Storage.exists(resumePath.c_str())) Storage.remove(resumePath.c_str());

  // Peanut-GB keeps the RTC registers internally. Seed them from the wall
  // clock on every launch; the emulator advances them during frame execution.
  time_t rawTime = 0;
  time(&rawTime);
  struct tm timeInfo {};
  localtime_r(&rawTime, &timeInfo);
  setCoreRtc(timeInfo);

  char gameTitle[17] = {};
  if (cgb) {
    walnut_cgb_rom_name(cgb, gameTitle);
  } else {
    gb_get_rom_name(gb, gameTitle);
  }
  std::strncpy(title, gameTitle, sizeof(title) - 1);
  if (title[0] == '\0') std::strncpy(title, "GAME BOY", sizeof(title) - 1);

  initialized.store(true, std::memory_order_release);
  applyJoypad();
#ifndef SIMULATOR
  {
    uint32_t phases[8];
    if (display.lastRefreshTimings(phases)) {
      perfSeenRefreshes = phases[6];
      perfSeenSkipped = phases[7];
    }
  }
#endif
  requestUpdate(true);

  const uint64_t nowUs = nowMicros();
  nextFrameUs = nowUs;
  nextDisplayUs = nowUs + DISPLAY_PERIOD_US;
  nextAutosaveUs = nowUs + AUTOSAVE_PERIOD_US;
  perfWindowStartUs = perfSessionStartUs = nowUs;
  if (fastRefreshValidated()) {
    refreshPreset = fastPreset;
    applyRefreshPreset();
  }
  startEmuTask();
  LOG_INF(TAG, "Started %s (%u bytes, save=%u bytes, core %s)", title, static_cast<unsigned>(romSize),
          static_cast<unsigned>(cartRamSize), coreName());
}

void GameBoyActivity::onExit() {
  stopEmuTask();  // before the save and cleanup touch its state
  if (initialized.load(std::memory_order_acquire)) {
    if (!saveCartRam()) LOG_ERR(TAG, "Save failed during Game Boy exit: %s", savePath.c_str());
    // Going to sleep in the game (enterDeepSleep() sets the flag first): save
    // the whole state so waking reopens the game where it was.
    if (APP_STATE.lastSleepFromGameBoy) {
      if (writeState(resumePath)) {
        APP_STATE.gameBoyResumePath = romPath;
      } else {
        APP_STATE.gameBoyResumePath.clear();
        LOG_ERR(TAG, "Could not save the state for wake: %s", resumePath.c_str());
      }
    }
    writePerfLog();
  }
  if (settingsDirty) saveSettings();
  TouchRegistry::getInstance().clear();
  cleanup();
  if (refreshPreset != 0) {
    refreshPreset = 0;
#ifndef SIMULATOR
    if (pulseActive) display.endPulseDrive();  // the next refresh redraws the whole screen
    pulseActive = false;
    display.finishOverlapped();  // the last picture's refresh may still run
    display.setFastRefreshTuning(0, 0);  // the reader's own refreshes again
    display.setFastRefreshDuScale(0);
    display.setFastRefreshDiffWindow(false);
    display.setDisplaySpiHz(0);
#endif
  }
  releasePulse();
  renderer.setOrientation(GfxRenderer::Portrait);
  Activity::onExit();
}

void GameBoyActivity::loop() {
  handleInput();
  if (exitRequested) {
    mappedInput.suppressCurrentTouchContact();
    finish();
    return;
  }

  if (!initialized.load(std::memory_order_acquire)) {
    // Global shortcuts are intentionally blocked while the Game Boy activity
    // owns input, so provide a physical Back escape from the error screen too.
    using Button = MappedInputManager::Button;
    if (mappedInput.wasPressed(Button::Back)) exitRequested = true;
#if CROSSINK_APP_CAP_TOUCH
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasScreenTouchDown(tx, ty)) exitRequested = true;
#endif
    if (exitRequested) {
      mappedInput.suppressCurrentTouchContact();
      finish();
    }
    return;
  }

  // With SETTINGS open, the screen changes only when the menu does.
  if (menuOpen.load(std::memory_order_acquire)) {
    delay(2);
    return;
  }
  const uint64_t nowUs = nowMicros();
  if (nowUs >= nextDisplayUs) {
    displayDue.store(true, std::memory_order_release);
    // Do not accumulate an unbounded display backlog when an e-ink refresh or
    // a long emulation stretch takes longer than one target period.
    nextDisplayUs = nowUs + (refreshPreset == 0                            ? DISPLAY_PERIOD_US
                             : kRefreshPresets[refreshPreset].pulseFrames ? PULSE_DISPLAY_PERIOD_US
                                                                          : FAST_DISPLAY_PERIOD_US);
    requestUpdate();
  }

  // The emulator has its own task; leave CPU 1 to the render task between
  // input polls (this activity skips the main loop's own delay).
  delay(2);
}

void GameBoyActivity::startEmuTask() {
  emuRun.store(true, std::memory_order_release);
  emuStopped.store(false, std::memory_order_release);
  if (xTaskCreatePinnedToCore(&GameBoyActivity::emuTaskEntry, "GameBoyEmu", 12288, this, 1, &emuTask, 0) != pdPASS) {
    LOG_ERR(TAG, "Could not start the emulator task");
    emuTask = nullptr;
    emuRun.store(false, std::memory_order_release);
    emuStopped.store(true, std::memory_order_release);
    showError("Could not start the emulator");
  }
}

// Asks the emulator task to finish its frame and waits for it (bounded).
void GameBoyActivity::stopEmuTask() {
  if (!emuTask) return;
  emuRun.store(false, std::memory_order_release);
  const unsigned long start = millis();
  while (!emuStopped.load(std::memory_order_acquire) && millis() - start < 1000) delay(1);
  if (!emuStopped.load(std::memory_order_acquire)) LOG_ERR(TAG, "Emulator task did not stop in time");
#ifdef SIMULATOR
  vTaskDelete(emuTask);  // the host thread has returned; release its handle
#endif
  emuTask = nullptr;
}

void GameBoyActivity::emuTaskEntry(void* arg) {
  auto* self = static_cast<GameBoyActivity*>(arg);
  self->emuTaskLoop();
  self->emuStopped.store(true, std::memory_order_release);
#ifndef SIMULATOR
  vTaskDelete(nullptr);
#endif
}

void GameBoyActivity::emuTaskLoop() {
  while (emuRun.load(std::memory_order_acquire)) {
    if (menuOpen.load(std::memory_order_acquire)) {
      // Paused for SETTINGS; the frame clock restarts when it closes.
      if (pauseStartUs == 0) pauseStartUs = nowMicros();
      emuIdle.store(true, std::memory_order_release);
      nextFrameUs = 0;
      delay(5);
      continue;
    }
    const uint64_t nowUs = nowMicros();
    if (pauseStartUs != 0) {
      // Leave the pause out of the speed figures and the cleaning period.
      const uint64_t pausedUs = nowUs - pauseStartUs;
      pauseStartUs = 0;
      perfSessionStartUs += pausedUs;
      perfWindowStartUs += pausedUs;
      nextCleanUs += pausedUs;
      perfLastLoopUs = 0;
    }
    if (perfLastLoopUs) perfLoopMaxGapUs = std::max(perfLoopMaxGapUs, static_cast<uint32_t>(nowUs - perfLastLoopUs));
    perfLastLoopUs = nowUs;
    ++perfLoops;
    runEmulation(nowUs);
    updatePerf(nowUs);

    if (refreshPreset != 0 && nowUs >= nextCleanUs) {
      nextCleanUs = nowUs + kRefreshPresets[refreshPreset].cleanSec * 1000000ULL;
      fullRefreshDue.store(true, std::memory_order_release);
    }

    if (nowUs >= nextAutosaveUs) {
      nextAutosaveUs = nowUs + AUTOSAVE_PERIOD_US;
      if (saveDirty && !saveCartRam()) LOG_ERR(TAG, "Autosave failed: %s", savePath.c_str());
    }

    // Let CPU 0's idle task (and its watchdog) run between frames.
    delay(1);
  }
}

void GameBoyActivity::render(RenderLock&&) {
  // Taken at the start: a request made while this refresh runs then starts the
  // next one right away. (Cleared at the end, it was lost, and the screen
  // waited up to a display period for the next request.)
  if (!displayDue.exchange(false, std::memory_order_acq_rel)) return;

  if (initialized.load(std::memory_order_acquire) && menuOpen.load(std::memory_order_acquire)) {
    chromeKey = CHROME_NONE;  // the game screen is redrawn whole on return
    // The game's touch targets share ids with the front-button hints, so a
    // menu tap over an old one would also read as a button (Back): the menu
    // reads its own taps and registers none.
    TouchRegistry::getInstance().clear();
    drawMenu();
#ifndef SIMULATOR
    // The menu takes an ordinary refresh (a full one after pulse drive); the
    // game screen comes back with a cleaning refresh.
    if (pulseActive) display.endPulseDrive();
    pulseActive = false;
#endif
    // Blocking (the menu changes rarely) and left out of the speed figures. A
    // cleaning refresh asked for meanwhile waits for the game screen.
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
#ifndef SIMULATOR
    uint32_t phases[8];
    if (display.lastRefreshTimings(phases)) {
      perfSeenRefreshes = phases[6];
      perfSeenSkipped = phases[7];
    }
#endif
    return;
  }

  if (!initialized.load(std::memory_order_acquire)) {
    chromeKey = CHROME_NONE;
    renderer.clearScreen(0xFF);
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2 - 32, "GAME BOY", true, EpdFontFamily::BOLD);
    if (error[0]) renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 + 8, error, true);
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 + 70, "Touch screen to exit", true);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  const uint64_t drawStartUs = nowMicros();
  // A pulse preset drives the panel from the planner, which needs the screen
  // to show the framebuffer first: a cleaning refresh starts it.
  const RefreshPreset& preset = kRefreshPresets[refreshPreset];
#ifdef SIMULATOR
  const bool pulse = false;
#else
  const bool pulse = preset.pulseFrames != 0 && pulseStore != nullptr;
#endif
  bool cleaning = fullRefreshDue.exchange(false, std::memory_order_acq_rel);
  if (pulse && !pulseActive) cleaning = true;
  // The framebuffer keeps the title and buttons from the last picture (the
  // display is single-buffered): clear and redraw them only when they change,
  // and with every cleaning refresh in case anything else drew on the screen.
  const uint32_t chrome = static_cast<uint32_t>(physicalJoypadMask | touchJoypadMask) |
                          (static_cast<uint32_t>(activeTouch) << 8) | (static_cast<uint32_t>(orientation) << 22) |
                          (statusLineShown.load(std::memory_order_acquire) ? (0x20000u | (static_cast<uint32_t>(
                                                             perfTextIdx.load(std::memory_order_acquire))
                                                         << 18))
                                           : 0u);
  const bool redrawChrome = cleaning || chrome != chromeKey;
  if (redrawChrome) renderer.clearScreen(0xFF);
  drawGameFrame();
  // The picture is in the display's framebuffer now: hand the Game Boy frame
  // back before the e-ink refresh (hundreds of ms), or the emulator, with
  // both of its buffers held, would stand still for the whole refresh.
  releaseRenderedFrame();
  if (redrawChrome) {
    drawControls();
    chromeKey = chrome;
  }
  registerTouchTargets();
  const uint64_t refreshStartUs = nowMicros();
  // On the fast refresh the call returns while the waveform runs, and the next
  // picture is drawn meanwhile: the driver keeps its own copy of the frame for
  // the rest of the refresh (setFastRefreshDiffWindow). The call first
  // finishes the refresh before. The stock refresh blocks until it is done.
#ifdef SIMULATOR
  const bool overlap = false;
#else
  const bool overlap = !cleaning && refreshPreset != 0 && preset.pulseFrames == 0 && renderer.supportsAsyncRefresh();
#endif
  if (pulse && !cleaning) {
#ifndef SIMULATOR
    // One pulse scan of whatever still has pulses left, started without
    // waiting: the next render plans while its waveform runs.
    uint8_t* fb = renderer.getFrameBuffer();
    const uint64_t nowUs = nowMicros();
    if (pulsePlanner.retarget(fb)) pulseLastChangeUs = nowUs;
    PulsePlanner::Window w;
    bool any = pulsePlanner.plan(pulseDrive, w);
    // Every pixel done, and the picture still for a moment (or, with
    // polishEvery, no polish for that long): what moved gets a few more
    // pulses, all of it alike, which finishes pixels a reversal cut short.
    if (!any && preset.pulsePolish != 0) {
      const bool still = nowUs - pulseLastChangeUs >= PULSE_POLISH_DELAY_US;
      const bool due =
          preset.pulsePolishEveryMs != 0 && nowUs - pulseLastPolishUs >= preset.pulsePolishEveryMs * 1000ULL;
      if ((still || due) && pulsePlanner.polish(preset.pulsePolish)) {
        pulseLastPolishUs = nowUs;
        any = pulsePlanner.plan(pulseDrive, w);
      }
    }
    if (!any) {
      perfUnchanged.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    display.pulseScan(fb, pulseDrive, w.row0, w.row1, w.byte0, w.byte1, preset.pulseFrames, false);
    pulsePlanner.delivered(pulseDrive, w);
#endif
  } else if (overlap) {
#ifndef SIMULATOR
    display.displayBufferOverlapped(HalDisplay::FAST_REFRESH);
#endif
  } else {
#ifndef SIMULATOR
    if (pulseActive) display.endPulseDrive();
    pulseActive = false;
#endif
    renderer.displayBuffer(cleaning ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
#ifndef SIMULATOR
    if (pulse) {
      // The screen shows the framebuffer: the planner starts from it.
      pulsePlanner.init(pulseStore, display.getDisplayWidthBytes(), display.getDisplayHeight(),
                        renderer.getFrameBuffer());
      pulsePlanner.setBudgets(preset.pulseBlack, preset.pulseWhite);
      pulseActive = true;
    }
#endif
  }
  const uint64_t doneUs = nowMicros();
#ifndef SIMULATOR
  uint32_t phases[8];
  if (display.lastRefreshTimings(phases)) {
    const uint32_t completed = phases[6] - perfSeenRefreshes;
    const uint32_t skipped = phases[7] - perfSeenSkipped;
    perfSeenRefreshes = phases[6];
    perfSeenSkipped = phases[7];
    if (!cleaning) {
      // Pictures that had not changed, which the driver did not refresh.
      if (skipped != 0) perfUnchanged.fetch_add(skipped, std::memory_order_relaxed);
      // The figures below are those of the refresh this call completed.
      if (completed == 0) return;
      for (int i = 0; i < 6; ++i) perfPhaseUs[i].fetch_add(phases[i], std::memory_order_relaxed);
    }
  }
#endif
  if (cleaning) {
    // A cleaning refresh takes ~1.6 s: kept out of the Fast refresh figures.
    perfCleanRefreshes.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  perfDisplays.fetch_add(1, std::memory_order_relaxed);
  perfDrawUs.fetch_add(static_cast<uint32_t>(refreshStartUs - drawStartUs), std::memory_order_relaxed);
  const uint32_t refreshUs = static_cast<uint32_t>(doneUs - refreshStartUs);
  perfRefreshUs.fetch_add(refreshUs, std::memory_order_relaxed);
  uint32_t seenMax = perfRefreshMaxUs.load(std::memory_order_relaxed);
  while (refreshUs > seenMax && !perfRefreshMaxUs.compare_exchange_weak(seenMax, refreshUs)) {
  }
}

// Marks the frame drawGameFrame() copied as consumed and lets the emulator
// write into it again.
void GameBoyActivity::releaseRenderedFrame() {
  const uint8_t claimedFrame = renderFrame.load(std::memory_order_acquire);
  if (claimedFrame > 1) return;
  uint8_t expected = claimedFrame;
  readyFrame.compare_exchange_strong(expected, static_cast<uint8_t>(0xFF), std::memory_order_acq_rel,
                                     std::memory_order_acquire);
  renderFrame.store(0xFF, std::memory_order_release);
}

// Every PERF_WINDOW_US: logs the counters and adds them to the session totals.
void GameBoyActivity::updatePerf(const uint64_t nowUs) {
  const uint64_t elapsedUs = nowUs - perfWindowStartUs;
  if (elapsedUs < PERF_WINDOW_US) return;
  const uint32_t displays = perfDisplays.exchange(0, std::memory_order_relaxed);
  const uint32_t drawUs = perfDrawUs.exchange(0, std::memory_order_relaxed);
  const uint32_t refreshUs = perfRefreshUs.exchange(0, std::memory_order_relaxed);
  const uint32_t refreshMaxUs = perfRefreshMaxUs.exchange(0, std::memory_order_relaxed);
  const float seconds = static_cast<float>(elapsedUs) / 1e6f;
  const float fps = static_cast<float>(perfFrames) / seconds;
  const unsigned speed = static_cast<unsigned>(fps * 100.0f / 59.73f + 0.5f);
  const float emuMs = perfFrames ? static_cast<float>(perfEmuUs) / perfFrames / 1000.0f : 0.0f;
  const float refreshMs = displays ? static_cast<float>(refreshUs) / displays / 1000.0f : 0.0f;

  uint32_t phaseUs[6];
  for (int i = 0; i < 6; ++i) phaseUs[i] = perfPhaseUs[i].exchange(0, std::memory_order_relaxed);
  const uint32_t cleanRefreshes = perfCleanRefreshes.exchange(0, std::memory_order_relaxed);
  const uint32_t unchanged = perfUnchanged.exchange(0, std::memory_order_relaxed);
  const float perDisplayMs = displays ? 1.0f / displays / 1000.0f : 0.0f;
  // The status line under the title (drawControls()): "screen|game" parts.
  const uint8_t nextText = perfTextIdx.load(std::memory_order_relaxed) ^ 1u;
  snprintf(perfText[nextText], sizeof(perfText[nextText]), "Screen %.1f/s|Game %u%%", displays / seconds, speed);
  perfTextIdx.store(nextText, std::memory_order_release);

  LOG_INF(TAG, "perf: emu %.1f fps (%u%%), frame %.1f ms avg %.1f max, waits %u; lcd %.1f/s, draw %.1f ms, "
          "refresh %.0f ms avg %.0f max; loop %.0f/s, longest gap %u ms",
          fps, speed, emuMs, perfEmuMaxUs / 1000.0f, static_cast<unsigned>(perfWaits), displays / seconds,
          displays ? drawUs / 1000.0f / displays : 0.0f, refreshMs, refreshMaxUs / 1000.0f, perfLoops / seconds,
          static_cast<unsigned>(perfLoopMaxGapUs / 1000));
  perfTotalLoopMaxGapUs = std::max(perfTotalLoopMaxGapUs, perfLoopMaxGapUs);
  perfTotalLoops += perfLoops;
  perfLoops = 0;
  perfLoopMaxGapUs = 0;

  perfTotalFrames += perfFrames;
  perfTotalWaits += perfWaits;
  perfTotalEmuUs += perfEmuUs;
  perfTotalEmuMaxUs = std::max(perfTotalEmuMaxUs, perfEmuMaxUs);
  perfTotalDisplays += displays;
  perfTotalDrawUs += drawUs;
  perfTotalRefreshUs += refreshUs;
  perfTotalRefreshMaxUs = std::max(perfTotalRefreshMaxUs, refreshMaxUs);
  perfTotalCleanRefreshes += cleanRefreshes;
  perfTotalUnchanged += unchanged;
  for (int i = 0; i < 6; ++i) perfTotalPhaseUs[i] += phaseUs[i];
  perfFrames = perfWaits = perfEmuUs = perfEmuMaxUs = 0;
  perfWindowStartUs = nowUs;
}

// Starts a new stretch of figures for gameboy-perf.txt (a new preset).
void GameBoyActivity::resetPerfTotals(const uint64_t nowUs) {
  perfSessionStartUs = perfWindowStartUs = nowUs;
  perfFrames = perfWaits = perfEmuUs = perfEmuMaxUs = 0;
  perfLoops = perfLoopMaxGapUs = 0;
  perfDisplays.store(0);
  perfDrawUs.store(0);
  perfRefreshUs.store(0);
  perfRefreshMaxUs.store(0);
  perfTotalFrames = perfTotalWaits = perfTotalEmuUs = 0;
  perfTotalEmuMaxUs = 0;
  perfTotalDisplays = perfTotalDrawUs = perfTotalRefreshUs = 0;
  perfTotalRefreshMaxUs = 0;
  perfTotalLoops = 0;
  perfTotalLoopMaxGapUs = 0;
  perfCleanRefreshes.store(0);
  perfTotalCleanRefreshes = 0;
  perfUnchanged.store(0);
  perfTotalUnchanged = 0;
  for (int i = 0; i < 6; ++i) {
    perfPhaseUs[i].store(0);
    perfTotalPhaseUs[i] = 0;
  }
}

// The emulator core, and for Walnut-CGB where its context landed (internal
// RAM is several times faster than PSRAM), for the speed log.
const char* GameBoyActivity::coreName() const {
  if (!cgb) return "peanut";
#ifdef SIMULATOR
  return "walnut";
#else
  return esp_ptr_external_ram(cgb) ? "walnut-psram" : "walnut-internal";
#endif
}

// One line per game session (and per refresh preset tried in it), for
// comparing readers, builds and presets.
void GameBoyActivity::writePerfLog() {
  const float seconds = static_cast<float>(perfWindowStartUs - perfSessionStartUs) / 1e6f;
  if (seconds < 4.0f || perfTotalFrames == 0) return;
  const float fps = perfTotalFrames / seconds;
  char panel[24];
  panelName(panel, sizeof(panel));
  const float phaseDiv = perfTotalDisplays ? 1000.0f * perfTotalDisplays : 1.0f;
  // The preset's label is for SETTINGS ("D30, clean 60 s"): no commas in a
  // CSV field.
  char preset[32];
  std::strncpy(preset, kRefreshPresets[refreshPreset].label, sizeof(preset) - 1);
  preset[sizeof(preset) - 1] = '\0';
  for (char* c = preset; *c; ++c) {
    if (*c == ',') *c = ' ';
  }
  char line[360];
  const int len = snprintf(
      line, sizeof(line), "%s,%s,%.0f,%.1f,%.0f,%.2f,%.1f,%llu,%.2f,%.1f,%.0f,%.0f,%.0f,%u,%s,%s,%s,%llu,%.1f,%.1f,%.1f,%.1f,%.1f,%llu,%.1f,%s\n", AppVersion::version(), title,
      seconds, fps, fps * 100.0f / 59.73f, static_cast<float>(perfTotalEmuUs) / perfTotalFrames / 1000.0f,
      perfTotalEmuMaxUs / 1000.0f, static_cast<unsigned long long>(perfTotalWaits), perfTotalDisplays / seconds,
      perfTotalDisplays ? perfTotalDrawUs / 1000.0f / perfTotalDisplays : 0.0f,
      perfTotalDisplays ? perfTotalRefreshUs / 1000.0f / perfTotalDisplays : 0.0f, perfTotalRefreshMaxUs / 1000.0f,
      perfTotalLoops / seconds, static_cast<unsigned>(perfTotalLoopMaxGapUs / 1000), portrait ? "portrait" : "landscape",
      panel, preset, static_cast<unsigned long long>(perfTotalCleanRefreshes),
      perfTotalPhaseUs[0] / phaseDiv, perfTotalPhaseUs[1] / phaseDiv, perfTotalPhaseUs[2] / phaseDiv,
      perfTotalPhaseUs[3] / phaseDiv, perfTotalPhaseUs[4] / phaseDiv,
      static_cast<unsigned long long>(perfTotalUnchanged), perfTotalPhaseUs[5] / 1.024f / phaseDiv, coreName());
  if (len <= 0 || static_cast<size_t>(len) >= sizeof(line)) return;
  static constexpr char kPerfLog[] = "/.crosspoint/gameboy-perf.txt";
  static constexpr char kHeader[] =
      "# version,rom,seconds,emu_fps,speed_pct,frame_ms_avg,frame_ms_max,waits,displays_per_s,draw_ms_avg,"
      "refresh_ms_avg,refresh_ms_max,loops_per_s,loop_gap_ms_max,orientation,panel,preset,clean_refreshes,copy_ms,new_ms,setup_ms,wave_ms,old_ms,unchanged,plane_kb,core\n";
  HalFile file = Storage.open(kPerfLog, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return;
  if (file.size() == 0) file.write(kHeader, sizeof(kHeader) - 1);
  if (file.size() < 64 * 1024) file.write(line, static_cast<size_t>(len));
  file.close();
}

void GameBoyActivity::setLayout() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  controlCount = 0;

  if (portrait) {
    // 480x800: the game fills the width on top (y a multiple of 8 for
    // drawGameFramePortrait()), the D-pad and B/A below it as on the handheld,
    // then a row of EXIT, SELECT, START and ROTATE.
    scale = std::max(1, std::min(MAX_GAME_SCALE, screenW / GAMEBOY_WIDTH));
    gameW = GAMEBOY_WIDTH * scale;
    gameH = GAMEBOY_HEIGHT * scale;
    gameX = (screenW - gameW) / 2;
    gameY = 40;
    titleX = 8;
    titleY = 10;

    const int padY = gameY + gameH + 24;
    const int pad = 64;
    const int padX = 14;
    addControl(TouchControl::Up, padX + pad, padY, pad, pad, "U");
    addControl(TouchControl::Left, padX, padY + pad, pad, pad, "L");
    addControl(TouchControl::Right, padX + 2 * pad, padY + pad, pad, pad, "R");
    addControl(TouchControl::Down, padX + pad, padY + 2 * pad, pad, pad, "D");
    const int ab = 80;
    addControl(TouchControl::A, screenW - 20 - ab, padY + 24, ab, ab, "A");
    addControl(TouchControl::B, screenW - 36 - 2 * ab, padY + 24 + ab * 3 / 4, ab, ab, "B");

    const int rowH = 52;
    const int rowY = screenH - 12 - rowH;
    const int rowW = (screenW - 8 - 3 * TOUCH_TARGET_GAP) / 4;
    const TouchControl row[] = {TouchControl::Exit, TouchControl::Select, TouchControl::Start, TouchControl::Settings};
    const char* rowLabels[] = {"EXIT", "SEL", "START", "SETTINGS"};
    for (int i = 0; i < 4; ++i) {
      addControl(row[i], 4 + i * (rowW + TOUCH_TARGET_GAP), rowY, rowW, rowH, rowLabels[i]);
    }
    return;
  }

  // 800x480: the game in the middle, the D-pad and SETTINGS on the left, B/A,
  // SELECT/START and EXIT on the right.
  const int availableH = std::max(GAMEBOY_HEIGHT, screenH - 48);
  scale = std::min({MAX_GAME_SCALE, std::max(GAMEBOY_WIDTH, screenW) / GAMEBOY_WIDTH, availableH / GAMEBOY_HEIGHT});
  scale = std::max(1, scale);
  gameW = GAMEBOY_WIDTH * scale;
  gameH = GAMEBOY_HEIGHT * scale;
  gameX = (screenW - gameW) / 2;
  gameY = std::max(0, (screenH - gameH) / 2);
  titleX = 6;
  titleY = 18;

  const int leftW = gameX;
  const int panelY = gameY + 120;
  const int centerX = leftW / 2;
  addControl(TouchControl::Up, centerX - 35, panelY, 70, 58, "U");
  addControl(TouchControl::Down, centerX - 35, panelY + 140, 70, 58, "D");
  addControl(TouchControl::Left, 10, panelY + 58, 60, 82, "L");
  addControl(TouchControl::Right, leftW - 70, panelY + 58, 60, 82, "R");
  addControl(TouchControl::Settings, 4, gameY + gameH - 52, leftW - 8, 48, "SETTINGS");

  const int rightX = gameX + gameW;
  const int rightW = screenW - rightX;
  const int buttonW = std::min(70, std::max(48, (rightW - TOUCH_TARGET_GAP) / 2));
  const int firstX = rightX + 4;
  const int secondX = firstX + buttonW + TOUCH_TARGET_GAP;
  addControl(TouchControl::B, firstX, panelY, buttonW, 70, "B");
  addControl(TouchControl::A, secondX, panelY, buttonW, 70, "A");
  addControl(TouchControl::Select, firstX, gameY + 275, buttonW, 52, "SEL");
  addControl(TouchControl::Start, secondX, gameY + 275, buttonW, 52, "START");
  addControl(TouchControl::Exit, rightX + 4, gameY + gameH - 52, rightW - 8, 48, "EXIT");
}

// Above the game in portrait; the left panel above the D-pad in landscape.
bool GameBoyActivity::inTitleStrip(const int x, const int y) const {
  if (portrait) return y < gameY;
  return x < gameX && y < gameY + 120;
}

void GameBoyActivity::addControl(const TouchControl control, const int x, const int y, const int w, const int h,
                                 const char* label) {
  if (controlCount >= MAX_CONTROLS) return;
  controls[controlCount++] = {control, x, y, w, h, label};
}

// Hands the refresh preset to the display driver (under the render lock: a
// refresh may be running) and asks for a cleaning refresh next.
void GameBoyActivity::applyRefreshPreset() {
  const RefreshPreset& p = kRefreshPresets[refreshPreset];
  {
    RenderLock lock(*this);
#ifndef SIMULATOR
    if (pulseActive) display.endPulseDrive();
    pulseActive = false;
    if (p.pulseFrames != 0 && !allocatePulse()) LOG_ERR(TAG, "No memory for pulse drive: stock refreshes");
    display.finishOverlapped();
    display.setFastRefreshTuning(p.tsset, p.pll);
    display.setFastRefreshDuScale(p.duPct);
    display.setFastRefreshDiffWindow(p.diffWindow);
    display.setDisplaySpiHz(p.spiHz);
#else
    (void)p;
#endif
  }
  nextCleanUs = nowMicros() + p.cleanSec * 1000000ULL;
  fullRefreshDue.store(true, std::memory_order_release);
  displayDue.store(true, std::memory_order_release);
  requestUpdate();
}

// The pulse planner's storage and the drive mask, once per game.
bool GameBoyActivity::allocatePulse() {
  if (pulseStore != nullptr) return true;
#ifdef SIMULATOR
  return false;
#else
  const size_t size = renderer.getBufferSize();
  pulseStore = static_cast<uint8_t*>(heap_caps_malloc(5 * size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  pulseDrive = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (pulseStore == nullptr || pulseDrive == nullptr) {
    releasePulse();
    return false;
  }
  return true;
#endif
}

void GameBoyActivity::releasePulse() {
#ifndef SIMULATOR
  heap_caps_free(pulseStore);
  heap_caps_free(pulseDrive);
#endif
  pulseStore = nullptr;
  pulseDrive = nullptr;
  pulseActive = false;
}

void GameBoyActivity::openMenu() {
  // The finger on SETTINGS must not press a menu row, and the game must not
  // see a button held while it is paused.
  mappedInput.suppressCurrentTouchContact();
  activeTouch = TouchControl::None;
  touchJoypadMask = 0;
  physicalJoypadMask = 0;
  applyJoypad();
  menuSel = 0;
  loadArmed = false;
  orientationPending = false;
  menuMessage[0] = '\0';
  stateExists = Storage.exists(statePath.c_str());
  menuOpen.store(true, std::memory_order_release);
  menuChanged();
}

void GameBoyActivity::closeMenu() {
  if (settingsDirty) saveSettings();
  loadArmed = false;
  mappedInput.suppressCurrentTouchContact();
  emuIdle.store(false, std::memory_order_release);  // before the emulator may run
  menuOpen.store(false, std::memory_order_release);
  fullRefreshDue.store(true, std::memory_order_release);  // clear the menu's traces
  displayDue.store(true, std::memory_order_release);
  requestUpdate();
}

void GameBoyActivity::menuChanged() {
  displayDue.store(true, std::memory_order_release);
  requestUpdate();
}

// Waits (bounded) until the emulator task has stopped between frames.
bool GameBoyActivity::waitEmuIdle() {
  const unsigned long start = millis();
  while (!emuIdle.load(std::memory_order_acquire)) {
    if (millis() - start > 500) return false;
    delay(2);
  }
  return true;
}

void GameBoyActivity::menuActivate(const MenuItem item) {
  if (item != MenuItem::Load) loadArmed = false;
  menuMessage[0] = '\0';
  switch (item) {
    case MenuItem::Orientation:
      orientationPending = true;  // turned in handleInput() once no finger is down
      settingsDirty = true;
      break;
    case MenuItem::Speed:
      if (fastRefreshValidated()) {
        fastPreset = static_cast<uint8_t>(fastPreset % (kRefreshPresetCount - 1) + 1);
        refreshPreset = fastPreset;
        applyRefreshPreset();
        settingsDirty = true;
      } else {
        std::strncpy(menuMessage, "Only Standard on this screen", sizeof(menuMessage) - 1);
      }
      break;
    case MenuItem::Colours:
      colourMode = static_cast<uint8_t>((colourMode + 1) % kColourModeCount);
      if (cgb) walnut_cgb_set_colours(cgb, colourMode);
      settingsDirty = true;
      break;
    case MenuItem::Status:
      statusLineShown.store(!statusLineShown.load(std::memory_order_relaxed), std::memory_order_release);
      settingsDirty = true;
      break;
    case MenuItem::Save: {
      const bool ok = waitEmuIdle() && writeState(statePath);
      stateExists = stateExists || ok;
      std::strncpy(menuMessage, ok ? "Saved" : "Could not save", sizeof(menuMessage) - 1);
      break;
    }
    case MenuItem::Load:
      if (!stateExists) {
        std::strncpy(menuMessage, "No quick save yet", sizeof(menuMessage) - 1);
      } else if (!loadArmed) {
        loadArmed = true;
        std::strncpy(menuMessage, "Load again: play since the save is lost", sizeof(menuMessage) - 1);
      } else if (waitEmuIdle() && readState(statePath)) {
        // A real cartridge clock does not go back with the game.
        time_t rawTime = 0;
        time(&rawTime);
        struct tm timeInfo {};
        localtime_r(&rawTime, &timeInfo);
        setCoreRtc(timeInfo);
        closeMenu();
        return;
      } else {
        loadArmed = false;
        std::strncpy(menuMessage, "Could not load", sizeof(menuMessage) - 1);
      }
      break;
    case MenuItem::Back:
      closeMenu();
      return;
    default:
      break;
  }
  menuChanged();
}

// The SETTINGS rows, in logical coordinates for the current orientation.
void GameBoyActivity::menuRowRect(const int row, int& x, int& y, int& w, int& h) const {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  constexpr int top = 56;
  h = std::min(72, (screenH - top - 48) / static_cast<int>(MenuItem::Count));
  x = 12;
  w = screenW - 24;
  y = top + row * h;
}

int GameBoyActivity::menuRowAt(const int x, const int y) const {
  for (int row = 0; row < static_cast<int>(MenuItem::Count); ++row) {
    int rx, ry, rw, rh;
    menuRowRect(row, rx, ry, rw, rh);
    if (hitRect(x, y, rx, ry + 2, rw, rh - 4)) return row;
  }
  return -1;
}

void GameBoyActivity::drawMenu() {
  renderer.clearScreen(0xFF);
  renderer.drawText(UI_12_FONT_ID, 16, 14, "SETTINGS", true, EpdFontFamily::BOLD);
  const char* gameName = title[0] ? title : "GAME BOY";
  const int tw = renderer.getTextWidth(UI_10_FONT_ID, gameName);
  renderer.drawText(UI_10_FONT_ID, renderer.getScreenWidth() - 16 - tw, 20, gameName, true);
  for (int row = 0; row < static_cast<int>(MenuItem::Count); ++row) {
    const char* label = "";
    const char* value = "";
    switch (static_cast<MenuItem>(row)) {
      case MenuItem::Orientation:
        label = "Orientation";
        value = kOrientations[orientation].label;
        break;
      case MenuItem::Speed:
        label = "Screen speed";
        value = fastRefreshValidated() ? kRefreshPresets[fastPreset].label : kRefreshPresets[0].label;
        break;
      case MenuItem::Colours:
        // Game Boy Color games only: see colourMode.
        label = "Colours";
        value = kColourModes[colourMode].label;
        break;
      case MenuItem::Status:
        label = "Status line";
        value = statusLineShown.load(std::memory_order_acquire) ? "On" : "Off";
        break;
      case MenuItem::Save:
        label = "Quick save";
        value = stateExists ? "Replaces the last one" : "";
        break;
      case MenuItem::Load:
        label = "Quick load";
        value = !stateExists ? "No save yet" : (loadArmed ? "Tap again" : "");
        break;
      case MenuItem::Back:
        label = "Back to game";
        break;
      default:
        break;
    }
    int x, y, w, h;
    menuRowRect(row, x, y, w, h);
    // The row the buttons act on is drawn dark.
    const bool selected = row == menuSel;
    renderer.fillRect(x, y + 2, w, h - 4, selected);
    renderer.drawRect(x, y + 2, w, h - 4, true);
    const int textY = y + h / 2 - 9;
    renderer.drawText(UI_10_FONT_ID, x + 12, textY, label, !selected, EpdFontFamily::BOLD);
    const int vw = renderer.getTextWidth(UI_10_FONT_ID, value);
    renderer.drawText(UI_10_FONT_ID, x + w - 12 - vw, textY, value, !selected);
  }
  int x, y, w, h;
  menuRowRect(static_cast<int>(MenuItem::Count), x, y, w, h);
  if (menuMessage[0]) renderer.drawText(UI_10_FONT_ID, x + 4, y + 10, menuMessage, true, EpdFontFamily::BOLD);
}

// Turns the screen to kOrientations[value] and lays the controls out again
// (under the render lock: a refresh may be running).
void GameBoyActivity::applyOrientation(const uint8_t value) {
  if (value >= kOrientationCount) return;
  {
    RenderLock lock(*this);
    orientation = value;
    portrait = kOrientations[value].portrait;
    renderer.setOrientation(kOrientations[value].orientation);
    setLayout();
  }
  fullRefreshDue.store(true, std::memory_order_release);
}

// /.crosspoint/gameboy.cfg: "key=value" lines, shared by every game.
void GameBoyActivity::loadSettings() {
  orientation = 0;
  fastPreset = kDefaultFastPreset;
  romFolder.clear();
  colourMode = WALNUT_COLOURS_CONTRAST;
  bool status = false;
  HalFile file;
  if (Storage.exists(kSettingsPath) && Storage.openFileForRead(TAG, kSettingsPath, file)) {
    char text[768] = {};
    const int got = file.read(reinterpret_cast<uint8_t*>(text), sizeof(text) - 1);
    file.close();
    if (got > 0) {
      text[got] = '\0';
      for (char* line = std::strtok(text, "\n"); line; line = std::strtok(nullptr, "\n")) {
        unsigned value = 0;
        if (std::sscanf(line, "orientation=%u", &value) == 1 && value < kOrientationCount) {
          orientation = static_cast<uint8_t>(value);
        } else if (std::strncmp(line, "speed=", 6) == 0) {
          const char* key = line + 6;
          constexpr unsigned kOldSpeedCount = sizeof(kOldSpeedKeys) / sizeof(kOldSpeedKeys[0]);
          if (std::sscanf(key, "%u", &value) == 1 && value >= 1 && value < kOldSpeedCount) {
            key = kOldSpeedKeys[value];
          }
          for (uint8_t i = 1; i < kRefreshPresetCount; ++i) {
            if (std::strcmp(key, kRefreshPresets[i].key) == 0) fastPreset = i;
          }
        } else if (std::sscanf(line, "status=%u", &value) == 1) {
          status = value != 0;
        } else if (std::strncmp(line, "colours=", 8) == 0) {
          for (uint8_t i = 0; i < kColourModeCount; ++i) {
            if (std::strcmp(line + 8, kColourModes[i].key) == 0) colourMode = i;
          }
        } else if (std::strncmp(line, "romdir=", 7) == 0) {
          romFolder = line + 7;
        }
      }
    }
  }
  statusLineShown.store(status, std::memory_order_release);
}

std::string GameBoyActivity::lastRomFolder() {
  HalFile file;
  if (!Storage.exists(kSettingsPath) || !Storage.openFileForRead(TAG, kSettingsPath, file)) return "/";
  char text[768] = {};
  const int got = file.read(reinterpret_cast<uint8_t*>(text), sizeof(text) - 1);
  file.close();
  if (got <= 0) return "/";
  text[got] = '\0';
  for (char* line = std::strtok(text, "\n"); line; line = std::strtok(nullptr, "\n")) {
    if (std::strncmp(line, "romdir=", 7) == 0 && line[7] == '/') {
      const std::string folder = line + 7;
      return Storage.exists(folder.c_str()) ? folder : "/";
    }
  }
  return "/";
}

void GameBoyActivity::saveSettings() {
  char text[768];
  const int len = snprintf(text, sizeof(text), "orientation=%u\nspeed=%s\ncolours=%s\nstatus=%u\nromdir=%s\n",
                           orientation, kRefreshPresets[fastPreset].key, kColourModes[colourMode].key,
                           statusLineShown.load(std::memory_order_acquire) ? 1u : 0u, romFolder.c_str());
  HalFile file;
  if (len <= 0 || len >= static_cast<int>(sizeof(text)) || !Storage.openFileForWrite(TAG, kSettingsPath, file)) {
    LOG_ERR(TAG, "Could not save %s", kSettingsPath);
    return;
  }
  file.write(reinterpret_cast<const uint8_t*>(text), static_cast<size_t>(len));
  file.close();
  settingsDirty = false;
}

// Writes header + gb_s + cartridge RAM to `path` (through a .tmp file, so a
// failed write keeps the old one). The emulator must not be running.
bool GameBoyActivity::writeState(const std::string& path) {
  if (!gb && !cgb) return false;
  const size_t coreSize = cgb ? walnut_cgb_state_size() : sizeof(gb_s);
  StateHeader header{};
  std::memcpy(header.magic, "GBQS", 4);
  header.version = kStateVersion;
  header.reserved = cgb ? 1 : 0;  // which core the state belongs to
  header.gbSize = static_cast<uint32_t>(coreSize);
  header.cartRamSize = static_cast<uint32_t>(cartRamSize);
  header.romSize = static_cast<uint32_t>(romSize);
  std::memcpy(header.title, title, sizeof(header.title));

  // Walnut-CGB's context is opaque: copied out first.
  uint8_t* coreState = reinterpret_cast<uint8_t*>(gb);
  if (cgb) {
    coreState = static_cast<uint8_t*>(heap_caps_malloc(coreSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!coreState) return false;
    walnut_cgb_save_state(cgb, coreState);
  }
  const std::string tempPath = path + ".tmp";
  HalFile file;
  bool ok = Storage.openFileForWrite(TAG, tempPath, file);
  ok = ok && file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
       file.write(coreState, coreSize) == coreSize;
  if (cgb) heap_caps_free(coreState);
  if (ok && cartRamSize > 0) ok = file.write(cartRam, cartRamSize) == cartRamSize;
  ok = ok && file.sync();
  file.close();
  if (ok) {
    if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
    ok = Storage.rename(tempPath.c_str(), path.c_str());
  }
  if (!ok) Storage.remove(tempPath.c_str());
  LOG_INF(TAG, "State %s: %s", ok ? "saved" : "NOT saved", path.c_str());
  return ok;
}

// Loads a state written by writeState() for this ROM, keeping this session's
// callbacks. The emulator must not be running. The cartridge RAM it brings
// back is written to the .sav at the next save.
bool GameBoyActivity::readState(const std::string& path) {
  if (!gb && !cgb) return false;
  const size_t coreSize = cgb ? walnut_cgb_state_size() : sizeof(gb_s);
  HalFile file;
  if (!Storage.openFileForRead(TAG, path, file)) return false;
  StateHeader header{};
  bool ok = file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) == static_cast<int>(sizeof(header)) &&
            std::memcmp(header.magic, "GBQS", 4) == 0 && header.version == kStateVersion &&
            header.reserved == (cgb ? 1 : 0) && header.gbSize == coreSize && header.cartRamSize == cartRamSize &&
            header.romSize == romSize;
  // Read everything aside first, so a short file leaves the game untouched.
  auto* loaded = static_cast<gb_s*>(heap_caps_malloc(coreSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  uint8_t* loadedRam =
      cartRamSize > 0 ? static_cast<uint8_t*>(heap_caps_malloc(cartRamSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
                      : nullptr;
  ok = ok && loaded && (cartRamSize == 0 || loadedRam) &&
       file.read(reinterpret_cast<uint8_t*>(loaded), coreSize) == static_cast<int>(coreSize) &&
       (cartRamSize == 0 || file.read(loadedRam, cartRamSize) == static_cast<int>(cartRamSize));
  file.close();
  if (ok && cgb) {
    walnut_cgb_load_state(cgb, loaded);
    if (cartRamSize > 0) std::memcpy(cartRam, loadedRam, cartRamSize);
    saveDirty = cartRamSize > 0;
  } else if (ok) {
    // Peanut-GB's only pointers: the callbacks and the front end's context.
    loaded->gb_rom_read = gb->gb_rom_read;
    loaded->gb_cart_ram_read = gb->gb_cart_ram_read;
    loaded->gb_cart_ram_write = gb->gb_cart_ram_write;
    loaded->gb_error = gb->gb_error;
    loaded->gb_serial_tx = gb->gb_serial_tx;
    loaded->gb_serial_rx = gb->gb_serial_rx;
    loaded->gb_bootrom_read = gb->gb_bootrom_read;
    loaded->display.lcd_draw_line = gb->display.lcd_draw_line;
    loaded->direct.priv = gb->direct.priv;
    std::memcpy(gb, loaded, sizeof(gb_s));
    if (cartRamSize > 0) std::memcpy(cartRam, loadedRam, cartRamSize);
    saveDirty = cartRamSize > 0;
  }
  if (loaded) heap_caps_free(loaded);
  if (loadedRam) heap_caps_free(loadedRam);
  LOG_INF(TAG, "State %s: %s", ok ? "loaded" : "NOT loaded", path.c_str());
  return ok;
}

size_t GameBoyActivity::romBankCountFromCode(const uint8_t code) {
  // Peanut-GB v1.3.0's internal ROM-size table covers codes 0..8.
  if (code > 8) return 0;
  return static_cast<size_t>(2) << code;
}

bool GameBoyActivity::isBatteryBackedCart(const uint8_t type) { return isBatteryBackedCartType(type); }

bool GameBoyActivity::loadRom() {
  HalFile file;
  if (!Storage.openFileForRead(TAG, romPath, file)) {
    showError("Unable to open Game Boy ROM");
    return false;
  }

  const uint64_t fileSize = file.fileSize64();
  if (fileSize < ROM_HEADER_SIZE || fileSize > static_cast<uint64_t>(SIZE_MAX)) {
    file.close();
    showError("ROM size is invalid");
    return false;
  }

  uint8_t romSizeCode = 0xFF;
  if (!file.seekSet(0x148) || file.read(&romSizeCode, 1) != 1) {
    file.close();
    showError("ROM header could not be read");
    return false;
  }

  const size_t bankCount = romBankCountFromCode(romSizeCode);
  if (bankCount == 0) {
    file.close();
    showError("Unsupported ROM size");
    return false;
  }

  const size_t expectedSize = bankCount * PEANUT_ROM_BANK_BYTES;
  if (fileSize < expectedSize) {
    file.close();
    showError("ROM file is incomplete");
    return false;
  }

  // Allocate the cartridge size declared by the ROM header, not any trailing
  // padding some dump tools append to the file. This keeps PSRAM usage bounded
  // by the actual Game Boy cartridge image.
  rom = static_cast<uint8_t*>(heap_caps_malloc(expectedSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rom) {
    file.close();
    showError("Not enough PSRAM for ROM");
    return false;
  }

  if (!file.seekSet(0)) {
    file.close();
    showError("ROM seek failed");
    return false;
  }

  size_t offset = 0;
  const size_t totalSize = expectedSize;
  while (offset < totalSize) {
    const size_t chunk = std::min<size_t>(16384, totalSize - offset);
    const int got = file.read(rom + offset, chunk);
    if (got <= 0) {
      file.close();
      showError("ROM read failed");
      return false;
    }
    offset += static_cast<size_t>(got);
  }
  file.close();
  romSize = totalSize;

  if (rom[0x149] > 5) {
    showError("Unsupported cartridge RAM");
    return false;
  }

  return true;
}

bool GameBoyActivity::buildSavePaths() {
  savePath = romPath;
  const size_t slash = savePath.find_last_of('/');
  const size_t dot = savePath.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    savePath += ".sav";
  else
    savePath.replace(dot, std::string::npos, ".sav");

  tempSavePath = savePath + ".tmp";
  backupSavePath = savePath + ".bak";
  const std::string base = savePath.substr(0, savePath.size() - 4);  // without ".sav"
  statePath = base + ".state";
  resumePath = base + ".resume";
  return savePath.size() < 512 && tempSavePath.size() < 512 && backupSavePath.size() < 512 && statePath.size() < 500 &&
         resumePath.size() < 500;
}

bool GameBoyActivity::initEmulator() {
  size_t saveSize = 0;
  if (rom[0x143] & 0x80) {
    // Made for (or also for) the Game Boy Color: the Walnut-CGB core.
    int initResult = 0;
    cgb = walnut_cgb_create(rom, romSize, &GameBoyActivity::cgbLine, &GameBoyActivity::cgbError, this, &initResult);
    if (!cgb) {
      showError(initResult < 0 ? "Not enough memory for emulator" : initErrorString(initResult));
      return false;
    }
    walnut_cgb_set_ram_written(cgb, &GameBoyActivity::cgbRamWritten);
    walnut_cgb_set_colours(cgb, colourMode);
    if (walnut_cgb_save_size(cgb, &saveSize) < 0) {
      showError("Unknown cartridge RAM size");
      return false;
    }
  } else {
    gb = static_cast<gb_s*>(calloc(1, sizeof(gb_s)));
    if (!gb) {
      showError("Not enough memory for emulator");
      return false;
    }

    const enum gb_init_error_e initResult = gb_init(gb, &GameBoyActivity::romRead, &GameBoyActivity::cartRamRead,
                                                     &GameBoyActivity::cartRamWrite, &GameBoyActivity::fatalError,
                                                     this);
    if (initResult != GB_INIT_NO_ERROR) {
      showError(initErrorString(initResult));
      return false;
    }
  }

  if (gb && gb_get_save_size_s(gb, &saveSize) < 0) {
    showError("Unknown cartridge RAM size");
    return false;
  }

  if (saveSize > 0) {
    cartRam = static_cast<uint8_t*>(heap_caps_malloc(saveSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!cartRam) {
      showError("Not enough PSRAM for save RAM");
      return false;
    }
    std::memset(cartRam, 0, saveSize);
    cartRamSize = saveSize;
    if (cgb) walnut_cgb_set_cart_ram(cgb, cartRam, cartRamSize);
  }

  frameBuffers[0] = static_cast<uint8_t*>(heap_caps_malloc(EMULATOR_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  frameBuffers[1] = static_cast<uint8_t*>(heap_caps_malloc(EMULATOR_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frameBuffers[0] || !frameBuffers[1]) {
    showError("Not enough PSRAM for Game Boy video");
    return false;
  }
  std::memset(frameBuffers[0], 0, EMULATOR_FRAME_BYTES);  // white
  std::memset(frameBuffers[1], 0, EMULATOR_FRAME_BYTES);
  writeFrame = 0;
  readyFrame.store(0xFF, std::memory_order_release);
  renderFrame.store(0xFF, std::memory_order_release);

  if (gb) gb_init_lcd(gb, &GameBoyActivity::lcdDrawLine);
  return true;
}

bool GameBoyActivity::loadSave() {
  if (cartRamSize == 0 || !isBatteryBackedCart(rom[0x147])) return true;

  std::memset(cartRam, 0, cartRamSize);
  HalFile file;
  if (!Storage.openFileForRead(TAG, savePath, file)) return true;  // New save; normal case.

  size_t offset = 0;
  while (offset < cartRamSize) {
    const size_t want = cartRamSize - offset;
    const int got = file.read(cartRam + offset, want);
    if (got <= 0) break;
    offset += static_cast<size_t>(got);
  }
  file.close();
  LOG_INF(TAG, "Loaded save: %u/%u bytes", static_cast<unsigned>(offset), static_cast<unsigned>(cartRamSize));
  return true;
}

bool GameBoyActivity::saveCartRam() {
  if (!saveDirty || cartRamSize == 0 || !cartRam || !rom || !isBatteryBackedCart(rom[0x147])) return true;

  HalFile file;
  if (!Storage.openFileForWrite(TAG, tempSavePath, file)) {
    LOG_ERR(TAG, "Unable to open save temp file: %s", tempSavePath.c_str());
    return false;
  }

  const size_t written = file.write(cartRam, cartRamSize);
  const bool synced = written == cartRamSize && file.sync();
  file.close();
  if (!synced) {
    Storage.remove(tempSavePath.c_str());
    LOG_ERR(TAG, "Save write failed: wrote=%u expected=%u", static_cast<unsigned>(written),
            static_cast<unsigned>(cartRamSize));
    return false;
  }

  const bool hadExisting = Storage.exists(savePath.c_str());
  if (hadExisting) {
    Storage.remove(backupSavePath.c_str());
    if (!Storage.rename(savePath.c_str(), backupSavePath.c_str())) {
      Storage.remove(tempSavePath.c_str());
      LOG_ERR(TAG, "Could not stage previous save before commit");
      return false;
    }
  }

  if (!Storage.rename(tempSavePath.c_str(), savePath.c_str())) {
    if (hadExisting) Storage.rename(backupSavePath.c_str(), savePath.c_str());
    Storage.remove(tempSavePath.c_str());
    LOG_ERR(TAG, "Could not commit save: %s", savePath.c_str());
    return false;
  }

  if (hadExisting) Storage.remove(backupSavePath.c_str());
  saveDirty = false;
  LOG_INF(TAG, "Saved: %s", savePath.c_str());
  return true;
}

void GameBoyActivity::cleanup() {
  initialized.store(false, std::memory_order_release);
  if (gb) {
    free(gb);
    gb = nullptr;
  }
  if (cgb) {
    walnut_cgb_destroy(cgb);
    cgb = nullptr;
  }
  if (rom) {
    heap_caps_free(rom);
    rom = nullptr;
  }
  if (cartRam) {
    heap_caps_free(cartRam);
    cartRam = nullptr;
  }
  for (auto*& buffer : frameBuffers) {
    if (buffer) heap_caps_free(buffer);
    buffer = nullptr;
  }
  writeFrame = 0;
  readyFrame.store(0xFF, std::memory_order_release);
  renderFrame.store(0xFF, std::memory_order_release);
  romSize = 0;
  cartRamSize = 0;
  physicalJoypadMask = 0;
  touchJoypadMask = 0;
  activeTouch = TouchControl::None;
  saveDirty = false;
}

void GameBoyActivity::showError(const char* message) {
  std::strncpy(error, message ? message : "Game Boy error", sizeof(error) - 1);
  error[sizeof(error) - 1] = '\0';
  initialized.store(false, std::memory_order_release);
  displayDue.store(true, std::memory_order_release);
  requestUpdate();
}

void GameBoyActivity::handleInput() {
  using Button = MappedInputManager::Button;
  if (menuOpen.load(std::memory_order_acquire)) {
    // SETTINGS: Up/Down pick a row, Confirm acts on it, Back returns to the
    // game; a tap acts on the row under it.
    constexpr int rows = static_cast<int>(MenuItem::Count);
    bool touching = false;
#if CROSSINK_APP_CAP_TOUCH
    int hx = 0;
    int hy = 0;
    touching = mappedInput.isScreenTouchHeld(hx, hy) || mappedInput.wasScreenTouchReleased();
#endif
    if (orientationPending && !touching) {
      orientationPending = false;
      applyOrientation(static_cast<uint8_t>((orientation + 1) % kOrientationCount));
      menuChanged();
      return;
    }
    if (mappedInput.wasPressed(Button::Up)) {
      menuSel = (menuSel + rows - 1) % rows;
      menuChanged();
    } else if (mappedInput.wasPressed(Button::Down)) {
      menuSel = (menuSel + 1) % rows;
      menuChanged();
    } else if (mappedInput.wasPressed(Button::Confirm)) {
      menuActivate(static_cast<MenuItem>(menuSel));
    } else if (mappedInput.wasPressed(Button::Back)) {
      closeMenu();
    }
#if CROSSINK_APP_CAP_TOUCH
    int x = 0;
    int y = 0;
    if (mappedInput.wasScreenTouchDown(x, y)) {
      const int row = menuRowAt(x, y);
      if (row >= 0) {
        menuSel = row;
        menuActivate(static_cast<MenuItem>(row));
      }
    }
#endif
    return;
  }
  const Button buttons[] = {Button::Up, Button::Down, Button::Left, Button::Right, Button::Confirm, Button::Back,
                            Button::Power};
  uint8_t nextPhysical = 0;
  for (const Button button : buttons) {
    if (mappedInput.isPressed(button)) nextPhysical |= joypadBitForButton(button);
  }
  if (nextPhysical != physicalJoypadMask) {
    physicalJoypadMask = nextPhysical;
    applyJoypad();
  }

#if CROSSINK_APP_CAP_TOUCH
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTouchDown(x, y)) {
    // The title strip shows or hides the status line, once per touch.
    if (inTitleStrip(x, y) && hitTestTouch(x, y) == TouchControl::None) {
      statusLineShown.store(!statusLineShown.load(std::memory_order_relaxed), std::memory_order_release);
      displayDue.store(true, std::memory_order_release);
      requestUpdate();
    }
    updateTouch(x, y);
  }

  if (mappedInput.isScreenTouchHeld(x, y)) {
    updateTouch(x, y);
  } else if (mappedInput.wasScreenTouchReleased()) {
    activeTouch = TouchControl::None;
    touchJoypadMask = 0;
    applyJoypad();
  }
#endif
}

void GameBoyActivity::updateTouch(const int x, const int y) {
  const TouchControl control = hitTestTouch(x, y);
  if (control == TouchControl::Exit) {
    exitRequested = true;
    return;
  }
  if (control == TouchControl::Settings) {
    openMenu();
    return;
  }
  if (control == activeTouch) return;
  activeTouch = control;
  touchJoypadMask = joypadBitForTouch(control);
  applyJoypad();
}

GameBoyActivity::TouchControl GameBoyActivity::hitTestTouch(const int x, const int y) const {
  for (size_t i = 0; i < controlCount; ++i) {
    const ControlButton& c = controls[i];
    if (hitRect(x, y, c.x, c.y, c.w, c.h)) return c.control;
  }
  return TouchControl::None;
}

uint8_t GameBoyActivity::joypadBitForTouch(const TouchControl control) {
  switch (control) {
    case TouchControl::Up: return JOYPAD_UP;
    case TouchControl::Down: return JOYPAD_DOWN;
    case TouchControl::Left: return JOYPAD_LEFT;
    case TouchControl::Right: return JOYPAD_RIGHT;
    case TouchControl::A: return JOYPAD_A;
    case TouchControl::B: return JOYPAD_B;
    case TouchControl::Select: return JOYPAD_SELECT;
    case TouchControl::Start: return JOYPAD_START;
    default: return 0;
  }
}

uint8_t GameBoyActivity::joypadBitForButton(const MappedInputManager::Button button) {
  switch (button) {
    case MappedInputManager::Button::Up: return JOYPAD_UP;
    case MappedInputManager::Button::Down: return JOYPAD_DOWN;
    case MappedInputManager::Button::Left: return JOYPAD_LEFT;
    case MappedInputManager::Button::Right: return JOYPAD_RIGHT;
    case MappedInputManager::Button::Confirm: return JOYPAD_START;
    case MappedInputManager::Button::Back: return JOYPAD_SELECT;
    case MappedInputManager::Button::Power: return JOYPAD_A;
    default: return 0;
  }
}

void GameBoyActivity::applyJoypad() {
  const uint8_t joypad = static_cast<uint8_t>(~(physicalJoypadMask | touchJoypadMask));
  if (gb) gb->direct.joypad = joypad;
  if (cgb) walnut_cgb_set_joypad(cgb, joypad);
}

void GameBoyActivity::runEmulation(const uint64_t nowUs) {
  if ((!gb && !cgb) || nextFrameUs == 0) nextFrameUs = nowUs;

  uint8_t ran = 0;
  while (nowUs >= nextFrameUs && ran < MAX_CATCH_UP_FRAMES) {
    // Draw one frame in DRAW_EVERY. Never draw into a buffer the display task
    // owns, nor over the most recently completed frame before it has been
    // consumed (with two buffers, a simple lock-free handoff): such a frame
    // runs undrawn instead, so the game never waits for the screen.
    bool draw = framesSinceDraw + 1 >= DRAW_EVERY;
    if (draw) {
      const uint8_t pending = readyFrame.load(std::memory_order_acquire);
      const uint8_t rendering = renderFrame.load(std::memory_order_acquire);
      if (writeFrame == pending || writeFrame == rendering) {
        ++perfWaits;
        draw = false;
      }
    }

    const uint64_t frameStartUs = nowMicros();
    if (cgb) {
      walnut_cgb_set_draw(cgb, draw ? 1 : 0);
      walnut_cgb_run_frame(cgb);
    } else {
      // Peanut-GB's own frame skip, steered the same way (see walnut_cgb_set_draw()).
      gb->direct.frame_skip = true;
      gb->display.frame_skip_count = draw;
      gb_run_frame(gb);
    }
    const uint32_t frameUs = static_cast<uint32_t>(nowMicros() - frameStartUs);
    perfEmuUs += frameUs;
    perfEmuMaxUs = std::max(perfEmuMaxUs, frameUs);
    ++perfFrames;
    if (draw) {
      publishCompletedFrame();
      framesSinceDraw = 0;
    } else if (framesSinceDraw < DRAW_EVERY) {
      ++framesSinceDraw;
    }
    nextFrameUs += EMULATION_FRAME_PERIOD_US;
    ++ran;
  }

  if (ran == MAX_CATCH_UP_FRAMES && nowUs >= nextFrameUs) nextFrameUs = nowUs + EMULATION_FRAME_PERIOD_US;
}

void GameBoyActivity::publishCompletedFrame() {
  readyFrame.store(writeFrame, std::memory_order_release);
  writeFrame ^= 1u;
}

void GameBoyActivity::drawGameFrame() {
  const uint8_t frameIndex = readyFrame.load(std::memory_order_acquire);
  if (frameIndex > 1) return;

  const uint8_t* source = frameBuffers[frameIndex];
  uint8_t* framebuffer = renderer.getFrameBuffer();
  if (!source || !framebuffer) return;

  // Claim the exact source buffer before reading it. The emulation task checks
  // this ownership marker before starting its next frame. Keep the claim until
  // the display refresh has consumed the CrossInk framebuffer.
  renderFrame.store(frameIndex, std::memory_order_release);

  const bool drawn =
      portrait ? drawGameFramePortrait(source, framebuffer, orientation >= 2)
               : drawGameFrameLandscape(source, framebuffer, orientation >= 2);
  if (!drawn) renderFrame.store(0xFF, std::memory_order_release);
}

namespace {

// Builds one panel row of the game a byte at a time: `count` game pixels read
// from `src` every `step` bytes, each `scale` panel pixels long. A game pixel
// becomes the bits patterns[grey level][first panel pixel's phase, mod 4] (1
// = black, most significant first), so the row is about count table lookups.
constexpr int MAX_GAME_ROW_BYTES = 64;
void buildGameRow(uint8_t* out, const uint8_t* src, const int step, const int count, const int scale,
                  const uint8_t (&patterns)[kGreyLevels][4]) {
  uint32_t acc = 0;
  int bits = 0;
  for (int g = 0; g < count; ++g) {
    acc = (acc << scale) | patterns[src[g * step]][(g * scale) & 3];
    bits += scale;
    if (bits >= 8) {
      bits -= 8;
      *out++ = static_cast<uint8_t>(~(acc >> bits));  // the framebuffer's 1 is white
      acc &= (1u << bits) - 1u;
    }
  }
}

// patterns[fixedPhase][level][alongPhase] for buildGameRow: the dither of
// shadeIsBlack() for a panel row whose fixed coordinate is fixedPhase mod 4.
// alongIsX says whether the row runs along the dither's x.
void gamePatterns(uint8_t (&patterns)[4][kGreyLevels][4], const int scale, const bool alongIsX) {
  for (int fixed = 0; fixed < 4; ++fixed) {
    for (int shade = 0; shade < kGreyLevels; ++shade) {
      for (int along0 = 0; along0 < 4; ++along0) {
        uint8_t pattern = 0;
        for (int k = 0; k < scale; ++k) {
          const int along = along0 + k;
          const bool black = alongIsX ? shadeIsBlack(shade, along, fixed) : shadeIsBlack(shade, fixed, along);
          pattern = static_cast<uint8_t>((pattern << 1) | (black ? 1 : 0));
        }
        patterns[fixed][shade][along0] = pattern;
      }
    }
  }
}

}  // namespace

namespace {

// Copies a built row into the framebuffer; a flipped orientation runs the
// panel the other way, so the bytes go in reverse order with their bits
// reversed.
void putGameRow(uint8_t* out, const uint8_t* row, const int bytes, const bool flipped) {
  if (!flipped) {
    std::memcpy(out, row, static_cast<size_t>(bytes));
    return;
  }
  static constexpr uint8_t kNibbleRev[16] = {0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
                                             0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF};
  for (int i = 0; i < bytes; ++i) {
    const uint8_t b = row[bytes - 1 - i];
    out[i] = static_cast<uint8_t>((kNibbleRev[b & 0x0F] << 4) | kNibbleRev[b >> 4]);
  }
}

}  // namespace

bool GameBoyActivity::drawGameFrameLandscape(const uint8_t* source, uint8_t* framebuffer, const bool flipped) {
  // LandscapeCounterClockwise is the native X4 Pro framebuffer orientation,
  // so logical (x,y) maps directly to physical (x,y); LandscapeClockwise turns
  // it half round: physical (W-1-x, H-1-y). Peanut-GB's four shades become
  // greys (shadeIsBlack()). A game line's panel rows differ only by their y
  // phase in the 4x4 dither: four are built, the rest copied.
  const int panelBytes = renderer.getDisplayWidthBytes();
  const int screenW = renderer.getDisplayWidth();
  const int screenH = renderer.getDisplayHeight();
  const int rowBytes = gameW / 8;
  const int startX = flipped ? screenW - gameX - gameW : gameX;
  if (panelBytes <= 0 || screenW <= 0 || gameX < 0 || startX < 0 || startX % 8 != 0 || gameX + gameW > screenW ||
      gameY + gameH > screenH || rowBytes > MAX_GAME_ROW_BYTES) {
    return false;
  }
  uint8_t patterns[4][kGreyLevels][4];
  gamePatterns(patterns, scale, /*alongIsX=*/true);
  uint8_t rows[4][MAX_GAME_ROW_BYTES];
  for (int gy = 0; gy < GAMEBOY_HEIGHT; ++gy) {
    const uint8_t* line = source + gy * GAMEBOY_WIDTH;
    for (int parity = 0; parity < 4; ++parity) {
      buildGameRow(rows[parity], line, 1, GAMEBOY_WIDTH, scale, patterns[parity]);
    }
    for (int sy = 0; sy < scale; ++sy) {
      const int panelY = gy * scale + sy;
      const int row = flipped ? screenH - 1 - (gameY + panelY) : gameY + panelY;
      putGameRow(framebuffer + static_cast<size_t>(row) * panelBytes + startX / 8, rows[panelY & 3], rowBytes,
                 flipped);
    }
  }
  return true;
}

bool GameBoyActivity::drawGameFramePortrait(const uint8_t* source, uint8_t* framebuffer, const bool flipped) {
  // Portrait turns the panel a quarter: logical (x, y) is panel
  // (y, panelHeight - 1 - x), and PortraitInverted (panelWidth - 1 - y, x).
  // Each game column is one panel row with the game's lines running along it.
  // A column's panel rows differ only by their x phase in the 4x4 dither:
  // four are built, the rest copied.
  const int panelBytes = renderer.getDisplayWidthBytes();
  const int panelW = renderer.getDisplayWidth();
  const int panelH = renderer.getDisplayHeight();
  const int rowBytes = gameH / 8;
  const int startY = flipped ? panelW - gameY - gameH : gameY;
  if (panelBytes <= 0 || startY < 0 || startY % 8 != 0 || gameH % 8 != 0 || gameX < 0 || gameX + gameW > panelH ||
      gameY + gameH > panelW || rowBytes > MAX_GAME_ROW_BYTES) {
    return false;
  }
  uint8_t patterns[4][kGreyLevels][4];
  gamePatterns(patterns, scale, /*alongIsX=*/false);
  uint8_t rows[4][MAX_GAME_ROW_BYTES];
  for (int gx = 0; gx < GAMEBOY_WIDTH; ++gx) {
    for (int parity = 0; parity < 4; ++parity) {
      buildGameRow(rows[parity], source + gx, GAMEBOY_WIDTH, GAMEBOY_HEIGHT, scale, patterns[parity]);
    }
    for (int sx = 0; sx < scale; ++sx) {
      const int dstX = gx * scale + sx;
      const int row = flipped ? gameX + dstX : panelH - 1 - (gameX + dstX);
      putGameRow(framebuffer + static_cast<size_t>(row) * panelBytes + startY / 8, rows[dstX & 3], rowBytes,
                 flipped);
    }
  }
  return true;
}

void GameBoyActivity::drawControls() {
  const uint8_t currentMask = static_cast<uint8_t>(physicalJoypadMask | touchJoypadMask);

  renderer.drawRect(gameX - 1, gameY - 1, gameW + 2, gameH + 2, true);
  renderer.drawText(UI_10_FONT_ID, titleX, titleY, title[0] ? title : "GAME BOY", true, EpdFontFamily::BOLD);
  if (statusLineShown.load(std::memory_order_acquire)) {
    // The last figures (updatePerf()), "screen|game": one line right of the
    // title in portrait, two in the narrow landscape panel. Blank until the
    // first figures arrive (two seconds).
    char screen[48];
    snprintf(screen, sizeof(screen), "%s", perfText[perfTextIdx.load(std::memory_order_acquire)]);
    char* game = std::strchr(screen, '|');
    if (game) *game++ = '\0';
    if (portrait) {
      char line[64];
      snprintf(line, sizeof(line), "%s  %s", screen, game ? game : "");
      const int w = renderer.getTextWidth(SMALL_FONT_ID, line);
      renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - 8 - w, titleY + 2, line, true);
    } else {
      renderer.drawText(SMALL_FONT_ID, titleX, titleY + 24, screen, true);
      if (game) renderer.drawText(SMALL_FONT_ID, titleX, titleY + 40, game, true);
    }
  }
  for (size_t i = 0; i < controlCount; ++i) {
    const ControlButton& c = controls[i];
    const uint8_t bit = joypadBitForTouch(c.control);
    const bool active = bit != 0 ? (currentMask & bit) != 0 : activeTouch == c.control;
    renderer.fillRect(c.x + 1, c.y + 1, c.w - 2, c.h - 2, active);
    renderer.drawRect(c.x, c.y, c.w, c.h, true);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, c.label);
    renderer.drawText(UI_10_FONT_ID, c.x + (c.w - tw) / 2, c.y + c.h / 2 - 7, c.label, !active,
                      EpdFontFamily::BOLD);
  }
}

void GameBoyActivity::registerTouchTargets() {
#if CROSSINK_APP_CAP_TOUCH
  auto& registry = TouchRegistry::getInstance();
  for (size_t i = 0; i < controlCount; ++i) {
    const ControlButton& c = controls[i];
    registry.add(Rect(c.x, c.y, c.w, c.h), static_cast<int>(c.control), TouchRegistry::Kind::Button);
  }
#endif
}

uint8_t GameBoyActivity::romRead(gb_s* state, const uint_fast32_t addr) {
  auto* self = state ? static_cast<GameBoyActivity*>(state->direct.priv) : nullptr;
  if (!self || !self->rom || addr >= self->romSize) return 0xFF;
  return self->rom[addr];
}

uint8_t GameBoyActivity::cartRamRead(gb_s* state, const uint_fast32_t addr) {
  auto* self = state ? static_cast<GameBoyActivity*>(state->direct.priv) : nullptr;
  if (!self || !self->cartRam || addr >= self->cartRamSize) return 0xFF;
  return self->cartRam[addr];
}

void GameBoyActivity::cartRamWrite(gb_s* state, const uint_fast32_t addr, const uint8_t value) {
  auto* self = state ? static_cast<GameBoyActivity*>(state->direct.priv) : nullptr;
  if (!self || !self->cartRam || addr >= self->cartRamSize) return;
  if (self->cartRam[addr] != value) self->saveDirty = true;
  self->cartRam[addr] = value;
}

void GameBoyActivity::lcdDrawLine(gb_s* state, const uint8_t* pixels, const uint_fast8_t line) {
  auto* self = state ? static_cast<GameBoyActivity*>(state->direct.priv) : nullptr;
  if (self) self->writeGameBoyLine(pixels, line);
}

// A Peanut-GB line: shade in bits 1-0 (0 white..3 black), palette bits above;
// stored as grey levels (kDmgLevel).
void GameBoyActivity::writeGameBoyLine(const uint8_t* pixels, const uint_fast8_t line) {
  if (!pixels || line >= GAMEBOY_HEIGHT || !frameBuffers[writeFrame]) return;
  uint8_t* out = frameBuffers[writeFrame] + static_cast<size_t>(line) * GAMEBOY_WIDTH;
  for (int x = 0; x < GAMEBOY_WIDTH; ++x) out[x] = kDmgLevel[pixels[x] & 0x03];
}

void GameBoyActivity::fatalError(gb_s* state, enum gb_error_e errorCode, const uint16_t addr) {
  auto* self = state ? static_cast<GameBoyActivity*>(state->direct.priv) : nullptr;
  if (!self) {
    ESP.restart();
    while (true) delay(1000);
  }

  self->emergencyRestart(static_cast<int>(errorCode), addr);
}

// The core cannot run past the error: save the cartridge RAM and restart.
void GameBoyActivity::emergencyRestart(const int error, const uint16_t addr) {
  LOG_ERR(TAG, "%s fatal error=%d addr=0x%04X", cgb ? "Walnut-CGB" : "Peanut-GB", error, addr);
  if (!saveCartRam()) LOG_ERR(TAG, "Emergency save failed before restart: %s", savePath.c_str());

  ESP.restart();
  while (true) delay(1000);
}

// Walnut-CGB hands over grey levels already (0..16, see shadeIsBlack()).
void GameBoyActivity::cgbLine(void* user, const uint8_t* levels, const uint_fast8_t line) {
  auto* self = static_cast<GameBoyActivity*>(user);
  if (!self || line >= GAMEBOY_HEIGHT || !self->frameBuffers[self->writeFrame]) return;
  std::memcpy(self->frameBuffers[self->writeFrame] + static_cast<size_t>(line) * GAMEBOY_WIDTH, levels,
              GAMEBOY_WIDTH);
}

void GameBoyActivity::cgbError(void* user, const int error, const uint16_t addr) {
  auto* self = static_cast<GameBoyActivity*>(user);
  if (!self) {
    ESP.restart();
    while (true) delay(1000);
  }
  self->emergencyRestart(error, addr);
}

void GameBoyActivity::cgbRamWritten(void* user) {
  auto* self = static_cast<GameBoyActivity*>(user);
  if (self) self->saveDirty = true;
}

void GameBoyActivity::setCoreRtc(const struct tm& time) {
  if (cgb) {
    walnut_cgb_set_rtc(cgb, &time);
  } else if (gb) {
    gb_set_rtc(gb, &time);
  }
}

const char* GameBoyActivity::initErrorString(const int errorCode) {
  switch (static_cast<gb_init_error_e>(errorCode)) {
    case GB_INIT_CARTRIDGE_UNSUPPORTED: return "Unsupported cartridge";
    case GB_INIT_INVALID_CHECKSUM: return "Invalid ROM checksum";
    case GB_INIT_NO_ERROR: return "";
    default: return "Unknown Game Boy ROM error";
  }
}

#endif  // CROSSINK_GAMEBOY
