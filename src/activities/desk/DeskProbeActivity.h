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

 private:
  struct Gauge {
    int soc = -1;     // percent
    int mv = -1;      // cell voltage
    int curMa = 0;    // 0x0C, signed: > 0 charging
    int avgMa = 0;    // 0x14, signed
    int remMah = -1;  // 0x10 remaining capacity
    bool ok = false;
  };

  // LightSleep: the real test. Delay: same tick loop with delay() instead of light sleep, as
  // a control (probe v1 never got past its first light sleep on an X3, 2026-11-20).
  enum class SleepMode : uint8_t { LightSleep, Delay };

  bool running = false;
  bool wifiRounds = true;
  SleepMode sleepMode = SleepMode::LightSleep;
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
  static Gauge readGauge();
};

#endif  // CROSSINK_DESK_PROBE
