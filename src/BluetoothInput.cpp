#include "BluetoothInput.h"

#if CROSSINK_BLUETOOTH

#include <Arduino.h>
#include <BleKeyboardHost.h>
#include <BoardConfig.h>
#include <Epub/Section.h>
#include <HalStorage.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"

namespace bt {
namespace {

constexpr char TAG[] = "BT";
constexpr char HOST_NAME[] = "Xteink Pokemon";
constexpr char BUTTONS_PATH[] = "/.crosspoint/bluetooth_buttons.bin";
constexpr char BUTTONS_TMP_PATH[] = "/.crosspoint/bluetooth_buttons.tmp";
constexpr uint8_t BUTTONS_MAGIC[4] = {'B', 'T', 'B', '1'};
constexpr uint8_t NO_CONTROL = ControlCount;

// One held controller button: the edge code its release carries, the control.
struct Held {
  uint32_t code = 0;
  uint8_t control = NO_CONTROL;
};

std::array<Held, 8> held{};
std::array<uint32_t, 8> pendingReleases{};
uint32_t learned[ControlCount] = {};
bool learnedLoaded = false;
bool learning = false;
uint32_t learnedPress = 0;
// Learning takes one code per physical press. A pad's D-pad or stick often
// changes several report bytes at once (a hat byte and an axis byte, both
// axes of a diagonal), each its own edge: without this the second edge was
// learned as the next control, so Down also filled Left. While any learned-
// gesture code is still down, further presses belong to the same press; the
// next press counts only LEARN_QUIET_MS after everything was let go.
std::array<uint32_t, 8> learningDown{};
uint32_t learningQuietSinceMs = 0;
uint32_t learningLastPressMs = 0;
constexpr uint32_t LEARN_QUIET_MS = 250;
// A release that never came (a dropped report) must not block learning: a
// press this long after the last one starts a new gesture anyway.
constexpr uint32_t LEARN_STUCK_MS = 1500;
bool wasConnected = false;
bool pausedForMemory = false;
// Serialises the radio's start/stop with its use: a chapter layout on the
// render task stops it (layoutStarted()) while the main loop polls it.
std::recursive_mutex radioMutex;

#if FREEINK_MCU_C3
// X3/X4 (ESP32-C3) radio policy, after CrossPoint Reader's BlePageTurner
// (lib/BlePageTurner/src/RadioPolicy.h): the reader there has ~150 KB of
// heap, the NimBLE stack takes ~49 KB of it, and laying out a chapter needs
// what is left (a 32 KB inflate block and the page index). So the radio runs
// only in the reader, with a remote paired, after the page is shown; it stops
// for every chapter layout, for 30 s without a link, and outside the reader.
constexpr size_t STACK_BYTES = 49152;
constexpr size_t READER_BLOCK_BYTES = 32768;          // the reader's inflate ring, kept whole beside the stack
constexpr size_t START_FREE_BYTES = STACK_BYTES + READER_BLOCK_BYTES;
constexpr size_t MINIMUM_FREE_BYTES = 65536;          // below: short in total, not in pieces
constexpr uint32_t IDLE_OFF_MS = 30u * 1000u;         // a radio nobody connected to for this long stops
constexpr uint32_t LAYOUT_QUIET_MS = 1500;            // no layout for this long before it starts again
constexpr uint32_t START_RETRY_MS = 10u * 1000u;      // between refused starts in the reader
constexpr uint8_t REFUSALS_BEFORE_RESTART = 3;        // heap in pieces this many times in a row: restart
constexpr uint32_t RESTART_SPENT_MAGIC = 0x42485231u;
// Outlives ESP.restart(): a restart for a heap in pieces is spent until the
// radio comes up again, so a heap still in pieces cannot restart in a loop.
RTC_NOINIT_ATTR uint32_t restartMemo;

std::atomic<int> layoutDepth{0};
std::atomic<uint32_t> lastLayoutMs{0};
Place lastPlace = Place::Other;
uint32_t placeSinceMs = 0;
uint32_t radioUpMs = 0;
uint32_t lastLinkMs = 0;
uint32_t lastStartTryMs = 0;
bool idleStopped = false;
uint8_t inPiecesInRow = 0;
#endif

// The front button each control stands for outside the Game Boy screen.
bool buttonFor(const uint8_t control, MappedInputManager::Button& out) {
  using Button = MappedInputManager::Button;
  switch (control) {
    case Up: out = Button::Up; return true;
    case Down: out = Button::Down; return true;
    case Left: out = Button::Left; return true;
    case Right: out = Button::Right; return true;
    case A: out = Button::Confirm; return true;
    case B: out = Button::Back; return true;
    default: return false;  // Start / Select: the Game Boy only
  }
}

// Keyboards, page turners and the SDK's synthetic axis-pad usages.
uint8_t controlForKey(const uint8_t usage) {
  switch (usage) {
    case 0x52: case 0x1A: case 0x43: return Up;              // Up arrow, W, axis up
    case 0x51: case 0x16: case 0x45: return Down;            // Down arrow, S, axis down
    case 0x50: case 0x04: case 0x41: case 0x42:              // Left arrow, A, axis left
    case 0x4B: return Left;                                  // Page Up
    case 0x4F: case 0x07: case 0x46:                         // Right arrow, D, axis right
    case 0x4E: return Right;                                 // Page Down
    case 0x28: case 0x58: case 0x1B: case 0x0E: case 0x47:   // Enter, keypad Enter, X, K, axis A
      return A;
    case 0x29: case 0x2A: case 0x1D: case 0x0D: case 0x48:   // Esc, Backspace, Z, J, axis B
      return B;
    case 0x2C: return Start;                                 // Space
    case 0x2B: return Select;                                // Tab
    default: return NO_CONTROL;
  }
}

void loadLearned() {
  if (learnedLoaded) return;
  learnedLoaded = true;
  std::memset(learned, 0, sizeof(learned));
  HalFile file;
  if (!Storage.openFileForRead(TAG, BUTTONS_PATH, file)) return;
  uint8_t bytes[4 + 4 * ControlCount] = {};
  const int count = file.read(bytes, sizeof(bytes));
  file.close();
  if (count != static_cast<int>(sizeof(bytes)) || std::memcmp(bytes, BUTTONS_MAGIC, 4) != 0) return;
  for (uint8_t i = 0; i < ControlCount; ++i) std::memcpy(&learned[i], bytes + 4 + 4 * i, 4);
}

bool saveLearned() {
  (void)Storage.mkdir("/.crosspoint");
  uint8_t bytes[4 + 4 * ControlCount] = {};
  std::memcpy(bytes, BUTTONS_MAGIC, 4);
  for (uint8_t i = 0; i < ControlCount; ++i) std::memcpy(bytes + 4 + 4 * i, &learned[i], 4);
  HalFile file;
  if (!Storage.openFileForWrite(TAG, BUTTONS_TMP_PATH, file)) return false;
  const bool written = file.write(bytes, sizeof(bytes)) == sizeof(bytes);
  file.close();
  if (!written) {
    Storage.remove(BUTTONS_TMP_PATH);
    return false;
  }
  Storage.remove(BUTTONS_PATH);
  return Storage.rename(BUTTONS_TMP_PATH, BUTTONS_PATH);
}

// Learning: the first press of a gesture is the learned code; every other
// edge until the controller is idle again (all of the gesture's codes let go,
// then LEARN_QUIET_MS without a press) is the same physical press.
void learnFrom(const freeink::RawButtonEvent& event) {
  const uint32_t code = event.code();
  bool anyDown = false;
  for (const uint32_t down : learningDown) anyDown = anyDown || down != 0;
  if (!event.pressed) {
    for (auto& down : learningDown) {
      if (down == code) down = 0;
    }
    bool stillDown = false;
    for (const uint32_t down : learningDown) stillDown = stillDown || down != 0;
    if (anyDown && !stillDown) learningQuietSinceMs = event.atMs;
    return;
  }
  if (event.wasRest) return;
  if (anyDown && event.atMs - learningLastPressMs >= LEARN_STUCK_MS) {
    learningDown.fill(0);
    anyDown = false;
    learningQuietSinceMs = learningLastPressMs;
  }
  learningLastPressMs = event.atMs;
  // An axis zone comes as a single tap with no release of its own.
  const bool tap = event.byteIndex == 0xFF && event.reportId == 0xFF;
  const bool idle = !anyDown && event.atMs - learningQuietSinceMs >= LEARN_QUIET_MS;
  if (idle && learnedPress == 0) learnedPress = code;
  if (tap) {
    learningQuietSinceMs = event.atMs;
    return;
  }
  for (auto& down : learningDown) {
    if (down == 0 || down == code) {
      down = code;
      break;
    }
  }
}

// A tap from a touch-mapping pad (BSP D6mini mode V3) names the spot it landed on,
// jittered a little each press: it is the control learned nearest to it, within
// TOUCH_MATCH_STEPS steps of 16 units (of 4096) on each axis.
constexpr int TOUCH_MATCH_STEPS = 8;

bool isTouchCode(const uint32_t code) {
  return static_cast<uint8_t>(code >> 16) == freeink::RawButtonEvent::TOUCH_REPORT_ID;
}

uint8_t controlForTouch(const uint32_t code) {
  uint8_t best = NO_CONTROL;
  int bestDistance = TOUCH_MATCH_STEPS + 1;
  for (uint8_t i = 0; i < ControlCount; ++i) {
    if (learned[i] == 0 || !isTouchCode(learned[i])) continue;
    const int dx = std::abs(static_cast<int>(code & 0xFF) - static_cast<int>(learned[i] & 0xFF));
    const int dy = std::abs(static_cast<int>((code >> 8) & 0xFF) - static_cast<int>((learned[i] >> 8) & 0xFF));
    const int distance = std::max(dx, dy);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

uint8_t controlFor(const freeink::RawButtonEvent& event) {
  const uint32_t code = event.code();
  if (isTouchCode(code)) return controlForTouch(code);
  for (uint8_t i = 0; i < ControlCount; ++i) {
    if (learned[i] != 0 && learned[i] == code) return i;
  }
  return controlForKey(event.keycode);
}

bool controlHeld(const uint8_t control) {
  for (const auto& h : held) {
    if (h.code != 0 && h.control == control) return true;
  }
  return false;
}

void releaseAll(const MappedInputManager& input) {
  for (auto& h : held) {
    MappedInputManager::Button button;
    if (h.code != 0 && buttonFor(h.control, button)) input.remoteInjectRelease(button);
    h = {};
  }
}

// Why the last start failed, for the Bluetooth screen (readers whose USB port
// is locked have no log): the step and the internal RAM it saw, in KB.
// Kept short, it shows on one line: "init 67K/31K" = the step, then free
// internal RAM and its largest block.
char startError[40] = {};
bool startErrorIsMemory = false;

void noteStartError(const char* step) {
  const unsigned freeKb = static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
  const unsigned blockKb = static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
  std::snprintf(startError, sizeof(startError), "%s%s%uK/%uK", step, step[0] ? " " : "", freeKb, blockKb);
  LOG_ERR(TAG, "Bluetooth did not start: %s", startError);
}

bool startRadio() {
  std::lock_guard<std::recursive_mutex> lock(radioMutex);
  startError[0] = '\0';
  startErrorIsMemory = false;
  if (BleHid.isRunning()) return true;
  if (BleHid.isStopping() && !BleHid.end(1000)) {
    noteStartError("stopping");
    return false;
  }
#if FREEINK_MCU_C3
  // The stack and the reader's block must both fit; enough bytes but no
  // block for them is a heap in pieces (only a restart joins it again).
  const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (freeBytes < START_FREE_BYTES || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < READER_BLOCK_BYTES) {
    noteStartError(freeBytes >= MINIMUM_FREE_BYTES ? "pieces" : "");
    startErrorIsMemory = true;
    if (freeBytes >= MINIMUM_FREE_BYTES) ++inPiecesInRow;
    return false;
  }
#endif
  if (!BleHid.begin(HOST_NAME)) {
    noteStartError("init");
    return false;
  }
#if FREEINK_MCU_C3
  // The stack may take the reader's last whole block: roll it back.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < READER_BLOCK_BYTES) {
    BleHid.end(1500);
    noteStartError("pieces");
    startErrorIsMemory = true;
    ++inPiecesInRow;
    return false;
  }
  inPiecesInRow = 0;
  restartMemo = 0;  // the radio came up: a later heap in pieces may restart once more
  radioUpMs = lastLinkMs = millis();
#endif
  return true;
}

bool stopRadio() {
  std::lock_guard<std::recursive_mutex> lock(radioMutex);
  if (!BleHid.isRunning() && !BleHid.isStopping()) return true;
  if (BleHid.end(1500)) return true;
  LOG_ERR(TAG, "Bluetooth is still stopping");
  return false;
}

#if FREEINK_MCU_C3
// SectionLayoutHooks (render task): the layout gets the radio's RAM.
void layoutStarted() {
  ++layoutDepth;
  lastLayoutMs = millis();
  if (BleHid.isRunning()) {
    LOG_INF(TAG, "Bluetooth stops for a chapter layout");
    stopRadio();
  }
}

void layoutEnded() {
  lastLayoutMs = millis();
  --layoutDepth;
}

// Heap in pieces too often: restart into the same book, once per radio start.
void restartForWholeHeap() {
  if (inPiecesInRow < REFUSALS_BEFORE_RESTART || restartMemo == RESTART_SPENT_MAGIC) return;
  LOG_INF(TAG, "Heap in pieces %u times: restarting into the book for Bluetooth", inPiecesInRow);
  restartMemo = RESTART_SPENT_MAGIC;
  APP_STATE.lastSleepFromReader = true;  // the boot reopens the open book
  APP_STATE.saveToFile();
  ESP.restart();
}
#endif

}  // namespace

void begin(const bool allowStart) {
  loadLearned();
#if FREEINK_MCU_C3
  // X3/X4: the radio starts in the reader only (policyTick()).
  (void)allowStart;
  if (esp_reset_reason() != ESP_RST_SW) restartMemo = 0;  // power-on garbage reads as "not spent"
  SectionLayoutHooks::onStart = &layoutStarted;
  SectionLayoutHooks::onEnd = &layoutEnded;
#else
  if (!SETTINGS.bluetoothEnabled) return;
  if (!allowStart) {
    std::snprintf(startError, sizeof(startError), "%s", "Back held at start");
    return;
  }
  startRadio();
#endif
}

bool setEnabled(const bool on) {
  SETTINGS.bluetoothEnabled = on ? 1 : 0;
  SETTINGS.saveToFile();
  return on ? startRadio() : stopRadio();
}

void policyTick(const Place place) {
#if FREEINK_MCU_C3
  const uint32_t now = millis();
  if (place == Place::Rendering) return;  // a page is rendering: leave the radio as it is
  if (place != lastPlace) {
    lastPlace = place;
    placeSinceMs = now;
    if (place == Place::Reader) {
      idleStopped = false;  // every book visit starts over
      lastStartTryMs = 0;
    }
  }
  const bool on = BleHid.isRunning();
  if (!SETTINGS.bluetoothEnabled) {
    if (on) stopRadio();
    return;
  }
  if (place == Place::Pairing) {
    // Pairing is the user asking for the radio now.
    if (!on && (lastStartTryMs == 0 || now - lastStartTryMs >= START_RETRY_MS)) {
      lastStartTryMs = now;
      startRadio();
    }
    return;
  }
  if (place != Place::Reader) {
    if (on) stopRadio();
    lastStartTryMs = 0;
    return;
  }
  if (on) {
    if (BleHid.isConnected() || BleHid.isConnecting()) {
      lastLinkMs = now;
    } else if (now - lastLinkMs >= IDLE_OFF_MS) {
      LOG_INF(TAG, "Bluetooth stops: no remote linked for 30 s");
      idleStopped = true;
      stopRadio();
    }
    return;
  }
  // Off in the reader: start once the page is shown and no layout runs.
  if (idleStopped || BleHid.pairedCount() == 0 || layoutDepth.load() > 0 ||
      now - lastLayoutMs.load() < LAYOUT_QUIET_MS || now - placeSinceMs < LAYOUT_QUIET_MS) {
    return;
  }
  if (lastStartTryMs != 0 && now - lastStartTryMs < START_RETRY_MS) return;
  lastStartTryMs = now;
  if (!startRadio()) restartForWholeHeap();
#else
  (void)place;
#endif
}

void noteDeviceInput() {
#if FREEINK_MCU_C3
  idleStopped = false;  // a key of the device wakes a radio stopped for idleness
#endif
}

bool holdsReaderHeap() {
#if FREEINK_MCU_C3
  return BleHid.isRunning();
#else
  return false;
#endif
}

bool running() { return BleHid.isRunning(); }

const char* lastStartError() { return startError; }

bool lastStartWasMemory() { return startErrorIsMemory; }

void pump(const MappedInputManager& input) {
  std::lock_guard<std::recursive_mutex> lock(radioMutex);
  input.remoteBeginFrame();
  if (!BleHid.isRunning()) {
    if (heldControls() != 0) releaseAll(input);  // stopped or paused while a button was down
    return;
  }
  BleHid.poll();

  const bool connected = BleHid.isConnected();
  if (wasConnected && !connected) releaseAll(input);  // a dropped link releases what it held
  wasConnected = connected;

  // A release that came in the frame of its press waits one frame, so screens
  // see the press before it ends.
  uint8_t pressedNow = 0;
  const auto release = [&input, &pressedNow](const uint32_t code) {
    for (auto& h : held) {
      if (h.code != code) continue;
      const uint8_t control = h.control;
      if (pressedNow & controlBit(static_cast<Control>(control))) {
        for (auto& p : pendingReleases) {
          if (p == 0) {
            p = code;
            return;
          }
        }
      }
      h = {};
      MappedInputManager::Button button;
      if (!controlHeld(control) && buttonFor(control, button)) input.remoteInjectRelease(button);
      return;
    }
  };
  std::array<uint32_t, 8> due = pendingReleases;
  pendingReleases.fill(0);
  for (const uint32_t code : due) {
    if (code != 0) release(code);
  }

  freeink::RawButtonEvent event;
  while (BleHid.popRawButton(event)) {
    const uint32_t code = event.code();
    if (learning) {
      learnFrom(event);
      continue;
    }
    if (!event.pressed) {
      release(code);
      continue;
    }
    if (event.wasRest) continue;
    const uint8_t control = controlFor(event);
    if (control == NO_CONTROL) continue;
    Held* slot = nullptr;
    for (auto& h : held) {
      if (h.code == code) {
        slot = &h;
        break;
      }
      if (!slot && h.code == 0) slot = &h;
    }
    if (!slot) continue;
    *slot = {code, control};
    MappedInputManager::Button button;
    if (buttonFor(control, button)) input.remoteInjectPress(button);
    pressedNow |= controlBit(static_cast<Control>(control));
  }
}

uint8_t heldControls() {
  uint8_t mask = 0;
  for (const auto& h : held) {
    if (h.code != 0 && h.control < ControlCount) mask |= controlBit(static_cast<Control>(h.control));
  }
  return mask;
}

void stopForSleep() {
  held.fill({});
  pendingReleases.fill(0);
  (void)stopRadio();
}

void pauseForMemory() {
#if FREEINK_MCU_C3
  if (!BleHid.isRunning()) return;
  pausedForMemory = stopRadio();
#endif
}

void resumeAfterMemory() {
  if (!pausedForMemory) return;
  pausedForMemory = false;
#if !FREEINK_MCU_C3
  if (SETTINGS.bluetoothEnabled) startRadio();
#endif
  // X3/X4: policyTick() starts it again where it may run (the reader).
}

void setLearning(const bool on) {
  learning = on;
  learnedPress = 0;
  learningDown.fill(0);
  learningQuietSinceMs = 0;
  learningLastPressMs = 0;
}

bool takeLearnedPress(uint32_t& code) {
  if (learnedPress == 0) return false;
  code = learnedPress;
  learnedPress = 0;
  return true;
}

uint32_t learnedCode(const Control control) {
  loadLearned();
  return control < ControlCount ? learned[control] : 0;
}

void setLearnedCodes(const uint32_t (&codes)[ControlCount]) {
  std::memcpy(learned, codes, sizeof(learned));
  learnedLoaded = true;
  if (!saveLearned()) LOG_ERR(TAG, "Could not save controller buttons");
}

void clearLearnedCodes() {
  std::memset(learned, 0, sizeof(learned));
  learnedLoaded = true;
  Storage.remove(BUTTONS_PATH);
}

bool sameButton(const uint32_t a, const uint32_t b) {
  if (a == b) return true;
  if (!isTouchCode(a) || !isTouchCode(b)) return false;
  // Two touch spots this close are one button tapped twice (its jitter); real
  // buttons of a touch-mapping pad sit further apart.
  const int dx = std::abs(static_cast<int>(a & 0xFF) - static_cast<int>(b & 0xFF));
  const int dy = std::abs(static_cast<int>((a >> 8) & 0xFF) - static_cast<int>((b >> 8) & 0xFF));
  return std::max(dx, dy) <= 1;
}

bool hasLearnedCodes() {
  loadLearned();
  for (const uint32_t code : learned) {
    if (code != 0) return true;
  }
  return false;
}

}  // namespace bt

#endif
