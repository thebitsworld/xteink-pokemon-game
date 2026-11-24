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
// The light sleep wakes on the timer and the power button (the front buttons are an ADC
// ladder), and on a touch where the build arms the touch controller's INT line (the X4 Pro test
// build).
namespace deskpower {

// The light-sleep battery loop (pins held, RTC minute ticks, RWDT recovery) was proven on the
// X3 only. Elsewhere Desk leaves sleeping on battery to the reader's own sleep.
bool batteryLoopSupported();

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

enum class Wake : uint8_t { Timer, PowerButton, Touch };
// Light sleep for up to ms, in chunks of at most 10 s, pins held, the watchdog fed per chunk.
// Returns early on the power button or a touch.
Wake sleepFor(uint32_t ms);

// Milliseconds until the next minute starts by the RTC (falls back to 60 s).
uint32_t msToNextMinute();

// Waits (up to 5 s) until the power button is up; returns how long it was still held.
uint32_t waitPowerButtonRelease();

// RTC-memory markers (kept through the watchdog's system reset, lost on a power cut).
bool batteryRunArmed();
void setBatteryRunArmed(bool armed);
uint32_t batteryResets();  // resets the current battery run came back from
void countBatteryReset();
void requestOpenDeskAfterRestart();
bool consumeOpenDeskRequest();
// A long press leaves the battery loop for a real sleep (power off): the loop restarts the
// reader, and the fresh boot goes to sleep with the RAM it starts with.
void requestSleepAfterRestart();
bool consumeSleepRequest();

// Where the battery loop last was (kept through a reset, so the next run's first log line can
// tell where a freeze stopped it): 1 light sleep entered, 2 woke from it, 3 back in the loop,
// 4 tick logged, 5 drawing, 6 drawn. 0: none recorded.
void setCrumb(uint8_t stage);
uint8_t lastCrumb();

// The battery loop holds the SD and panel lines through light sleep, and a hold outlives a
// watchdog reset: setup() lets them go before it touches the card.
void releasePinHolds();

}  // namespace deskpower

#endif  // CROSSINK_DESK
