#pragma once

#if CROSSINK_DESK_PROBE

#include <cstdint>

#include "activities/Activity.h"

// Applications > Desk probe (test builds only: -DCROSSINK_DESK_PROBE=1). Phase 0 of
// docs/development/desk-mode-plan.md: measures what a desk clock on battery costs on a real
// X3 before Desk mode is designed around it.
//
// Once started it ticks every minute: wake from ESP32 light sleep (timer), read the clock and
// the BQ27220 gauge, redraw the screen (fast refresh, a half refresh every 60th tick), append
// a row to /.crosspoint/desk-probe.csv, light-sleep again. Every 30th tick it also connects to
// the last-used Wi-Fi network and disconnects, timing it. Light sleep can only wake on the
// timer and the power button (the front buttons are an ADC ladder), so the power button ends
// the run; the main loop may then treat that press as usual (sleep), which is fine: every
// tick is already on the card.
class DeskProbeActivity final : public Activity {
 public:
  DeskProbeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("DeskProbe", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return running; }
  bool needsFullPowerWhilePreventingSleep() override { return false; }

  struct Gauge {
    int soc = -1;     // percent
    int mv = -1;      // cell voltage
    int curMa = 0;    // 0x0C, signed: > 0 charging
    int avgMa = 0;    // 0x14, signed
    int remMah = -1;  // 0x10 remaining capacity
    bool ok = false;
  };
  static Gauge readGauge();

 private:
  // LightSleep: the real test. Delay: same tick loop with delay() instead of light sleep, as
  // a control (probe v1 never got past its first light sleep on an X3, 2026-11-20).
  // LightSleepKeepPins (v3): GPIO sleep switching off and the display / SD pins held, in case
  // the pins' sleep state (SD power off, display lines floating) is what hangs the wake.
  // DeepSleep (v5): every tick is a deep sleep with a timer wake; the boot goes straight to
  // deskprobe::handleBoot() (light sleep hung at random after 3-11 ticks, 2026-11-23).
  enum class SleepMode : uint8_t { LightSleepKeepPins, LightSleep, Delay, DeepSleep };

  bool running = false;
  bool wifiRounds = true;
  SleepMode sleepMode = SleepMode::DeepSleep;
  char lastRunNote[96] = "";  // what the RTC breadcrumbs say about the previous run
  bool stoppedByButton = false;
  bool halfThisTick = true;
  bool lastWifiOk = false;
  uint32_t tick = 0;
  uint32_t lastAwakeMs = 0;
  uint32_t lastRefreshMs = 0;
  uint32_t lastWifiMs = 0;
  int lastWake = 0;  // esp_sleep_wakeup_cause_t of the last wake
  Gauge gauge;
  char timeText[16] = "--:--";

  void runTick();
  void sleepUntilNextTick(uint32_t tickStartMs);
  uint32_t wifiRound();
  void appendLog(bool usb) const;
  // One line per step to /.crosspoint/desk-probe-trace.txt, opened and closed each time, so
  // the last line on the card shows where a hang happened.
  void trace(const char* step, long value = -1) const;
  const char* modeName() const;
  void readBreadcrumbs();
};

// Deep-sleep ticks (probe v5). Each tick: draw, log to /.crosspoint/desk-probe-deep.csv,
// deep sleep with a timer wake for the rest of the minute. The next boot calls handleBoot()
// right after the display and fonts are up and never returns while the run lasts. Any other
// wake (the power button) ends the run and boots normally.
namespace deskprobe {
// True while a deep-sleep run is armed (pick a seamless display start for its timer wakes).
bool deepRunArmed();
// From setup(): runs the tick when this is the run's timer wake (does not return then).
// Returns false for every other boot.
bool handleBoot(int wakeupCause, GfxRenderer& renderer);
// From the probe screen: arms the run and does its first tick (does not return).
[[noreturn]] void startDeepRun(GfxRenderer& renderer);
}  // namespace deskprobe

#endif  // CROSSINK_DESK_PROBE
