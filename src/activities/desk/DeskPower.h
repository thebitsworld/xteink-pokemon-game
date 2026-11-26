#pragma once

#if CROSSINK_DESK

#include <cstddef>
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
// The task that called esp_restart() during the last battery run, once (false: none recorded).
bool takeRestartTask(char* out, size_t size);
// Light sleeps since the last call: returned OK, rejected, the last rejection's error, the
// milliseconds the OK ones lasted, and the wakeup cause pending at the last rejection (-1 none).
void takeSleepStats(uint32_t& ok, uint32_t& rejected, int& lastErr, uint32_t& sleptMs, int& rejectCause);
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

// The X4 Pro (ESP32-S3) froze inside light sleep after a few ticks, and the RTC watchdog did
// not bring it back (battery logs, 2026-11-23/24). It keeps its power latch through deep sleep,
// which the X3 cannot, so there each tick is a deep sleep with a timer wake: the minute's tick
// runs in a fresh boot (with whole RAM), and the loop's state waits in RTC memory.
bool deepSleepTicks();

enum class BootWake : uint8_t { Reset, Timer, PowerButton };
// Why a boot inside a battery run began: a deep-sleep tick, the power button during one, or a
// reset (the watchdog...).
BootWake bootWake();

struct TickState {
  int32_t ticksSinceHalf;
  uint32_t lastWeatherTry;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  // The frame the panel shows (the last tick's minute and its UTC time), drawn again at the next
  // tick into the controller's previous-frame plane, which deep sleep wipes.
  uint8_t hour;
  uint8_t minute;
  uint8_t shownValid;
  uint32_t shownUnix;
  uint8_t weatherFailStreak;  // failed weather tries in a row (the retry wait grows)
};
bool loadTickState(TickState& out);
// Waits until the panel's BUSY line has been idle for stableMs (at most maxMs); returns the
// milliseconds waited.
uint32_t waitPanelIdle(uint32_t stableMs, uint32_t maxMs);
void clearTickState();
// Saves the state, powers Wi-Fi, the card and the panel down, and deep-sleeps for ms (or until
// the power button). Does not return.
[[noreturn]] void deepSleepFor(const TickState& state, uint32_t ms);

// The battery loop holds the SD and panel lines through light sleep, and a hold outlives a
// watchdog reset: setup() lets them go before it touches the card.
void releasePinHolds();

}  // namespace deskpower

#endif  // CROSSINK_DESK
