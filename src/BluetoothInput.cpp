#include "BluetoothInput.h"

#if CROSSINK_BLUETOOTH

#include <BleKeyboardHost.h>
#include <BoardConfig.h>
#include <HalStorage.h>
#include <esp_heap_caps.h>
#include <Logging.h>

#include <array>
#include <cstring>

#include "CrossPointSettings.h"
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
bool wasConnected = false;
bool pausedForMemory = false;
constexpr size_t C3_MIN_FREE_BLOCK = 48 * 1024;

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

uint8_t controlFor(const freeink::RawButtonEvent& event) {
  const uint32_t code = event.code();
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

bool startRadio() {
  if (BleHid.isRunning()) return true;
  if (BleHid.isStopping() && !BleHid.end(1000)) return false;
#if FREEINK_MCU_C3
  // The NimBLE host and controller take ~40 KB of the C3's internal RAM; with
  // less free in one piece the start would fail half way or starve the reader.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < C3_MIN_FREE_BLOCK) {
    LOG_ERR(TAG, "Not enough memory to start Bluetooth");
    return false;
  }
#endif
  if (!BleHid.begin(HOST_NAME)) {
    LOG_ERR(TAG, "Could not start Bluetooth");
    return false;
  }
  return true;
}

bool stopRadio() {
  if (!BleHid.isRunning() && !BleHid.isStopping()) return true;
  if (BleHid.end(1500)) return true;
  LOG_ERR(TAG, "Bluetooth is still stopping");
  return false;
}

}  // namespace

void begin() {
  loadLearned();
  if (SETTINGS.bluetoothEnabled) startRadio();
}

bool setEnabled(const bool on) {
  SETTINGS.bluetoothEnabled = on ? 1 : 0;
  SETTINGS.saveToFile();
  return on ? startRadio() : stopRadio();
}

bool running() { return BleHid.isRunning(); }

void pump(const MappedInputManager& input) {
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
    if (!event.pressed) {
      release(code);
      continue;
    }
    if (event.wasRest) continue;
    if (learning) {
      learnedPress = code;
      continue;
    }
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
  if (SETTINGS.bluetoothEnabled) startRadio();
}

void setLearning(const bool on) {
  learning = on;
  learnedPress = 0;
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

bool hasLearnedCodes() {
  loadLearned();
  for (const uint32_t code : learned) {
    if (code != 0) return true;
  }
  return false;
}

}  // namespace bt

#endif
