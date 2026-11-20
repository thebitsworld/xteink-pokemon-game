#pragma once

#if CROSSINK_DESK

#include <cstdint>

#include "activities/Activity.h"

// Desk mode (docs/development/desk-mode-plan.md), phase 1: the main screen offline - clock,
// Gregorian and lunar date, next calendar event - redrawn once a minute. Weather and PC stats
// come in later phases; battery ticks (light sleep in <= 10 s chunks, pins held) in phase 5.
// For now the screen keeps the reader awake only on USB power; on battery the normal
// auto-sleep applies.
class DeskActivity final : public Activity {
 public:
  DeskActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Desk", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool needsFullPowerWhilePreventingSleep() override { return false; }

 private:
  struct LocalTime {
    bool valid = false;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
  };

  LocalTime shown;               // what the screen shows
  int minutesSinceFull = 0;      // fast refreshes since the last half refresh
  bool forceHalfRefresh = true;  // next render cleans the panel
  uint32_t lastPollMs = 0;

  static LocalTime readLocalTime();
};

#endif  // CROSSINK_DESK
