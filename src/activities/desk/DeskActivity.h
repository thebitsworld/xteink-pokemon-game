#pragma once

#if CROSSINK_DESK

#include <cstdint>

#include "DeskWeather.h"
#include "DeskWeatherService.h"
#include "activities/Activity.h"

// Desk mode (docs/development/desk-mode-plan.md). Phase 1: the main screen offline - clock,
// Gregorian and lunar date, next calendar event - redrawn once a minute. Phase 2: the weather
// card from Open-Meteo, fetched on entry when older than the interval, every 10 minutes on USB
// and on Select. PC stats come in phase 3; battery ticks (light sleep in <= 10 s chunks, pins
// held) in phase 5. For now the screen keeps the reader awake only on USB power.
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

  deskweather::Config weatherConfig;
  desk::WeatherReport weather;
  deskweather::Result weatherResult = deskweather::Result::Ok;
  bool weatherFetchPending = false;
  bool weatherFetching = false;
  uint32_t lastWeatherAttemptMs = 0;
  bool weatherAttempted = false;

  static LocalTime readLocalTime();
  static bool readUtcUnix(uint32_t& out);
  static int utcOffsetMinutes();
  void runWeatherFetch();
  void drawWeatherCard(int top, int height, uint32_t nowUnix) const;
  void drawSkyIcon(int x, int y, int size, desk::Sky sky, bool day) const;
};

#endif  // CROSSINK_DESK
