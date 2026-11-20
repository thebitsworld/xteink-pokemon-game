#pragma once

#if CROSSINK_DESK

#include <cstdint>
#include <string>
#include <vector>

#include "AgentStats.h"
#include "Pomodoro.h"
#include "DeskWeather.h"
#include "DeskWeatherService.h"
#include "activities/Activity.h"

// Desk mode (docs/development/desk-mode-plan.md).
// Phase 1: main screen offline - clock, Gregorian and lunar date, next calendar event - redrawn
// once a minute. Phase 2: the weather card from Open-Meteo (on entry when older than 10 min,
// every 10 min on USB, on Select). Phase 3: the PC page and the main screen's PC strip from the
// agent in tools/desk-agent (on entry, every minute on USB, on Select). Phase 4: the photo
// frame (/desk/photos, next photo on Select and every 30 min on USB) and Pomodoro (runs in the
// background; the screen jumps to it when a phase ends). Up/Down switch pages.
// Battery ticks (light sleep in <= 10 s chunks, pins held) come in phase 5; for now the screen
// keeps the reader awake only on USB, where Wi-Fi also stays up between polls.
class DeskActivity final : public Activity {
 public:
  DeskActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Desk", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool needsFullPowerWhilePreventingSleep() override { return false; }

 private:
  enum class Page : uint8_t { Main, Pc, Photo, Pomodoro, Count };

  struct LocalTime {
    bool valid = false;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
  };

  Page page = Page::Main;
  LocalTime shown;               // what the screen shows
  int minutesSinceFull = 0;      // fast refreshes since the last half refresh
  bool forceHalfRefresh = true;  // next render cleans the panel
  uint32_t lastPollMs = 0;
  uint32_t enteredMs = 0;  // the Select that opened Desk must not also refresh it

  deskweather::Config config;
  desk::WeatherReport weather;
  deskweather::Result weatherResult = deskweather::Result::Ok;
  bool weatherFetchPending = false;
  bool weatherFetching = false;
  bool weatherAttempted = false;
  uint32_t lastWeatherAttemptMs = 0;

  desk::AgentStats pc;
  deskweather::Result pcResult = deskweather::Result::Ok;
  bool pcPollPending = false;
  bool pcPolling = false;
  bool pcAttempted = false;
  uint32_t lastPcAttemptMs = 0;
  bool wifiKeptForUsb = false;

  std::vector<std::string> photos;
  int photoIndex = 0;
  uint32_t lastPhotoChangeMs = 0;

  desk::Pomodoro pomodoro;
  uint32_t shownPomodoroMinutes = 0;

  static LocalTime readLocalTime();
  static bool readUtcUnix(uint32_t& out);
  static int utcOffsetMinutes();
  static void formatLocalHm(uint32_t unixSec, char* out, size_t size);

  void runWeatherFetch();
  void runPcPoll();
  bool pcFresh(uint32_t nowUnix) const;

  void drawStatusBar(const char* title, const char* middle) const;
  void drawBar(int x, int y, int w, int h, int pct) const;
  void drawWeatherCard(int top, int height, uint32_t nowUnix) const;
  void drawSkyIcon(int x, int y, int size, desk::Sky sky, bool day) const;
  void renderMain(uint32_t nowUnix);
  void renderPc(uint32_t nowUnix);
  // Pushes to the panel itself (photo::render's grayscale passes).
  void renderPhoto(uint32_t nowUnix);
  void renderPomodoro();
  void loadPhotos();
  void switchPage(Page to);
  static uint32_t nowSec();  // monotonic seconds for Pomodoro
};

#endif  // CROSSINK_DESK
