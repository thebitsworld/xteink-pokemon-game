#pragma once

#if CROSSINK_DESK

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "AgentStats.h"
#include "Pomodoro.h"
#include "DeskWeather.h"
#include "DeskWeatherService.h"
#include "DeskPower.h"
#include "CrossPointSettings.h"
#include "activities/Activity.h"

// Desk mode (docs/development/desk-mode-plan.md).
// Phase 1: main screen offline - clock, Gregorian and lunar date, next calendar event - redrawn
// once a minute. Phase 2: the weather card from Open-Meteo (on entry when older than 10 min,
// every 10 min on USB, on Select). Phase 3: the PC page and the main screen's PC strip from the
// agent in tools/desk-agent (on entry, every minute on USB, on Select). Phase 4: the photo
// frame (Photo Slideshow's folder and settings - Fit/Crop, random order, interval on USB - next
// photo on Select or after the slideshow interval, with a date/lunar/weather strip) and
// Pomodoro (runs in the background; the screen jumps to it when a phase ends). Up/Down switch
// pages; the weather and PC fetches run in a background task so the buttons stay responsive.
// On USB the screen keeps the reader awake and Wi-Fi stays up between polls. Phase 5: on
// battery, after 30 s without input (no Pomodoro running), the main screen switches to the
// battery loop (DeskPower.h): minute ticks in light sleep, weather every 60 min, PC off; the
// power button or plugging USB leaves it (restart back into Desk). A watchdog reset inside the
// loop comes back to it from setup() via resumeBatteryFromBoot().
class DeskActivity final : public Activity {
 public:
  DeskActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Desk", renderer, mappedInput) {}

  // From setup(), when the RTC marker says a battery run was going: loads the config and the
  // cached weather and goes on with the battery loop. Does not return.
  [[noreturn]] void resumeBatteryFromBoot();

  // "Sleep opens Desk" (SETTINGS.deskOnSleep): a sleep asked for while Desk is open, or that
  // opens it, goes to the battery loop at once when on battery.
  static void requestBatteryNow() { batteryNowRequested = true; }
  static bool isOpen() { return open; }
  // Desk opened in the last few seconds. Leaving the battery saver restarts the reader, which
  // takes a while on the X3; a second short press made meanwhile ("it did not wake") sent Desk
  // straight back to the saver (battery log: exit-button then enter in the same minute,
  // 2026-11-23/24). Such a press is ignored.
  static bool justOpened() { return open && millis() - openedAtMs < 8000; }
  // "Light keeps Desk awake" (SETTINGS.deskLightAwake) with the frontlight on and the battery
  // not low: Desk stays awake on battery instead of its battery saver or the reader's sleep.
  static bool lightKeepsAwake();

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool needsFullPowerWhilePreventingSleep() override { return false; }

 private:
  enum class Page : uint8_t { Main, Calendar, Pc, Photo, Pomodoro, Count };

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
  uint8_t pcFailStreak = 0;  // polls in a row that found no PC
  uint32_t pcRetryMs() const;
  bool wifiKeptForUsb = false;

  bool netBusy = false;  // a job was started and not applied yet
  std::atomic<bool> netDone{false};
  bool netWantWeather = false, netWantPc = false, netUserAsked = false, netKeepWifi = false;
  bool netPcRan = false;
  int netPhotoIndex = -1;  // a PC photo to download, -1 none
  deskweather::Result netPhotoResult = deskweather::Result::Ok;
  uint32_t netNow = 0;
  deskweather::Config netConfig;
  desk::WeatherReport netWeather;
  desk::AgentStats netPc;
  deskweather::Result netWeatherResult = deskweather::Result::Ok;
  deskweather::Result netPcResult = deskweather::Result::Ok;

  std::vector<std::string> photos;
  int photoIndex = 0;
  uint32_t lastPhotoChangeMs = 0;
  int pcPhotoLoaded = -1;   // the PC photo now in kPcPhotoPath, -1 none
  bool pcPhotoFailed = false;
  int calendarOffset = 0;   // months from the current one on the calendar page
  static bool pcPhotos() { return SETTINGS.deskPhotoSource == 1; }
  static bool isNight(const LocalTime& t);
  void nextPhoto();
  static const char* pageName(Page p);
  static Page neighbour(Page p, int step);
  void drawPageHints(const char* back, const char* confirm) const;
  // Button hints in the order Back, Select, front previous, front next. A touch reader (X4 Pro)
  // draws no hints of its own, so Desk draws them there as a bar of buttons to tap.
  void drawHints(const char* back, const char* confirm, const char* prev, const char* next) const;
  // The touch bar's button under (x, y), in the order above; -1 for none.
  int touchHintAt(int x, int y) const;
  mutable uint8_t touchBarMask = 0;  // bit i: the touch bar shows button i (none on a photo)

  desk::Pomodoro pomodoro;
  uint32_t shownPomodoroMinutes = 0;

  uint32_t lastInputMs = 0;
  bool selectWasHeld = false;  // Select held past kAddressHoldMs, acted on at its release
  bool batteryMode = false;  // drawn by the battery loop: no button hints
  static bool batteryNowRequested;
  static bool open;
  static uint32_t openedAtMs;

  static LocalTime readLocalTime();
  static bool readUtcUnix(uint32_t& out);
  static int utcOffsetMinutes();
  static void formatLocalHm(uint32_t unixSec, char* out, size_t size);

  // The network job: weather and/or PC in a FreeRTOS task on copies of the state; loop()
  // applies the results once it is done (applyNetJob). userAsked: a failed Wi-Fi opens the picker.
  void startNetJob(bool wantWeather, bool wantPc, bool userAsked, int photoIndex = -1);
  bool applyNetJob();  // true when it opened the Wi-Fi picker
  void waitNetJob();
  static void netTaskEntry(void* arg);
  void runNetJob();
  bool pcFresh(uint32_t nowUnix) const;
  void openSettings();
  void syncConfigFromSettings();
  static bool pageEnabled(Page p);

  void drawStatusBar(const char* title, const char* middle) const;
  void drawBar(int x, int y, int w, int h, int pct) const;
  void drawGauge(int cx, int cy, int r, int pct, int thickness) const;
  void drawWeatherCard(int top, int height, uint32_t nowUnix) const;
  void drawSkyIcon(int x, int y, int size, desk::Sky sky, bool day) const;
  void renderMain(uint32_t nowUnix);
  void renderPc(uint32_t nowUnix);
  // Pushes to the panel itself (photo::render's grayscale passes).
  void renderPhoto(uint32_t nowUnix);
  void renderPomodoro();
  void renderCalendar();
  void loadPhotos();
  static std::string photoFolder();  // Photo Slideshow's folder, no trailing slash
  void switchPage(Page to);
  static uint32_t nowSec();  // monotonic seconds for Pomodoro
  // The page as render() draws it, without the render lock (also used by the battery loop).
  void drawPage();
  void loadState();
  // fromBoot: after a reset inside a run; tick: a deep-sleep tick's boot (deskpower::deepSleepTicks).
  [[noreturn]] void runBatteryLoop(bool fromBoot, const deskpower::TickState* tick = nullptr);
  // heldBeforeMs: how long the power button was already down (a press that woke a deep sleep).
  [[noreturn]] void leaveBatteryLoop(bool touch = false, uint32_t heldBeforeMs = 0);
};

#endif  // CROSSINK_DESK
