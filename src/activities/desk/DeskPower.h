#pragma once

#if CROSSINK_DESK

#include <cstdint>

// Desk mode on battery (docs/development/desk-mode-plan.md, phase 5), from the Desk probe's
// results on the X3 (2026-11-23):
// - deep sleep cannot wake on an ESP timer (the chip loses power), so minute ticks use light
//   sleep;
// - light sleep only wakes reliably with the display and SD pins held, GPIO sleep switching
//   off, sleeps of at most 10 s, and from a minimal loop with the reader's background work
//   stopped (probe v6: 25 ticks, no hang, about -3 mA);
// - the RTC watchdog guards each sleep: a sleep that never returns resets the chip, and the
//   boot resumes the battery loop from RTC memory.
// The light sleep wakes on the timer and the power button only (the front buttons are an ADC
// ladder).
namespace deskpower {

// USB power: the X3 has no USB-detect pin and HalGPIO infers USB from a charging current, which
// is zero once the cell is full; a PC connection (USB Serial/JTAG host) counts too.
bool externalPower();

// BQ27220 readings for the battery log.
struct GaugeSample {
  int soc = -1;
  int mv = -1;
  int curMa = 0;  // > 0 charging
};
GaugeSample readGauge();
// One line per battery-loop event to /.crosspoint/desk-battery.csv (with a header).
void logBattery(const char* event, const char* localTime, const GaugeSample& g, uint32_t resets);

void watchdogArm(uint32_t timeoutMs);
void watchdogFeed();
void watchdogDisarm();

enum class Wake : uint8_t { Timer, PowerButton };
// Light sleep for up to ms, in chunks of at most 10 s, pins held, the watchdog fed per chunk.
// Returns early on the power button.
Wake sleepFor(uint32_t ms);

// Milliseconds until the next minute starts by the RTC (falls back to 60 s).
uint32_t msToNextMinute();

// Waits (up to 5 s) until the power button is up.
void waitPowerButtonRelease();

// RTC-memory markers (kept through the watchdog's system reset, lost on a power cut).
bool batteryRunArmed();
void setBatteryRunArmed(bool armed);
uint32_t batteryResets();  // resets the current battery run came back from
void countBatteryReset();
void requestOpenDeskAfterRestart();
bool consumeOpenDeskRequest();

}  // namespace deskpower

#endif  // CROSSINK_DESK
