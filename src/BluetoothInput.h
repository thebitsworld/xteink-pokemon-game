#pragma once

// Bluetooth controllers and page turners (BLE HID, X4 Pro). Built on the SDK's
// BleKeyboardHost, the way Ink-boy reads a controller in its emulator: every
// button edge is a raw code (where the report differs from its rest frame),
// mapped to one of eight controls. Up/Down/Left/Right/A/B are injected into
// MappedInputManager as the front buttons, so menus, the reader and the Pokemon
// game follow the controller with no code of their own; the Game Boy screen
// also reads Start and Select through heldControls().
//
// A control is found by the code learned for it (Settings > System > Bluetooth
// > Controller buttons), else by the key the report carries: arrows / WASD,
// Enter / X / K = A, Esc / Backspace / Z / J = B, Space = Start, Tab = Select,
// Page Down / Page Up = Right / Left (page turners turn pages).

#include <cstdint>

class MappedInputManager;

namespace bt {

enum Control : uint8_t { Up, Down, Left, Right, A, B, Start, Select, ControlCount };

constexpr uint8_t controlBit(const Control control) { return static_cast<uint8_t>(1u << control); }

#if CROSSINK_BLUETOOTH

// Starts the radio when Settings has Bluetooth on (setup()).
void begin();
// The Settings switch: starts or stops the radio and saves the setting.
bool setEnabled(bool on);
bool running();
// Why the last start failed ("memory, RAM 70 KB, block 31/48 KB"), or "".
const char* lastStartError();
// Once per loop, after MappedInputManager::update(): reads the controller and
// injects this frame's button edges.
void pump(const MappedInputManager& input);
// Controls held on the controller (controlBit() bits).
uint8_t heldControls();
// Before deep sleep: the radio goes down so it cannot wake the panel later.
void stopForSleep();
// The ESP32-C3 readers (X3/X4) have no PSRAM: a Lua app needs the tens of KB
// the radio holds, so it pauses while one runs and comes back after. No-ops
// on the S3 readers.
void pauseForMemory();
void resumeAfterMemory();

// Learning the controller's buttons: while on, presses are kept for
// takeLearnedPress() and not injected.
void setLearning(bool on);
bool takeLearnedPress(uint32_t& code);
uint32_t learnedCode(Control control);
void setLearnedCodes(const uint32_t (&codes)[ControlCount]);
void clearLearnedCodes();
bool hasLearnedCodes();
// True when two learned codes name the same controller button (a touch-mapping
// pad's taps land a little apart each time).
bool sameButton(uint32_t a, uint32_t b);

#else

inline void begin() {}
inline bool running() { return false; }
inline void pump(const MappedInputManager&) {}
inline uint8_t heldControls() { return 0; }
inline void stopForSleep() {}
inline void pauseForMemory() {}
inline void resumeAfterMemory() {}

#endif

}  // namespace bt
