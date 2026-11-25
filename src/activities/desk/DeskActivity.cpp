#include "DeskActivity.h"

#if CROSSINK_DESK

#include <Arduino.h>
#include <CrossInkHalFrontlight.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#ifndef SIMULATOR
#include <esp_system.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DeskAgentService.h"
#include "DeskFonts.h"
#include "DeskPower.h"
#include "Lunar.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/util/PhotoRender.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr size_t kMaxPhotos = 500;

// Open-Meteo gives °C; the reader shows °C or °F (desk.json "unit").
int shownTemp(const int c, const bool f) { return f ? static_cast<int>(std::lround(c * 9.0 / 5.0 + 32.0)) : c; }

constexpr uint32_t kPollMs = 1000;
constexpr int kHalfRefreshEveryMinutes = 60;          // USB default of "Full refresh" (plan)
constexpr uint32_t kWeatherEveryMs = 10 * 60 * 1000;  // USB default of "Weather"
constexpr uint32_t kWeatherRetryMs = 2 * 60 * 1000;   // after a failed attempt
constexpr uint32_t kPcEveryMs = 60 * 1000;            // USB default of "PC + Claude"
constexpr uint32_t kPcRetryMs = 2 * 60 * 1000;
constexpr uint32_t kPcFreshSec = 5 * 60;  // older PC numbers are hidden from the main screen
constexpr int kMargin = 16;
constexpr uint32_t kBatteryIdleMs = 30 * 1000;       // no input this long on battery: battery loop
// "Weather on battery" (SETTINGS.deskBatteryWeather): 30, 60 or 120 minutes.
uint32_t batteryWeatherSec() {
  switch (SETTINGS.deskBatteryWeather) {
    case 0:
      return 30 * 60;
    case 2:
      return 120 * 60;
    default:
      return 60 * 60;
  }
}
constexpr uint32_t kBatteryWeatherRetrySec = 15 * 60;  // after a failed try (was: every minute)
constexpr int kBatteryHalfRefreshEvery = 180;        // battery default of "Full refresh": 3 h
constexpr uint32_t kBatteryWatchdogMs = 60 * 1000;   // > a 10 s sleep chunk and a weather fetch
constexpr uint32_t kPhotoUnattendedMs = 60 * 60 * 1000;  // photo / PC page on battery: back to the clock
constexpr uint32_t kAddressHoldMs = 800;               // hold Select on the PC page: PC address
constexpr uint32_t kNetTaskStackBytes = 10240;         // HTTPS + JSON; the loop task has 8192
constexpr int kPhotoStripH = 128;                      // the info strip under a photo
constexpr int kBottomSafe = 24;
constexpr char kPcPhotoPath[] = "/.crosspoint/desk-pc-photo.png";  // the PC photo on screen
constexpr int kNightTickMinutes = 30;                  // battery loop at night: a tick per half hour                        // the X3's bezel hides the panel's last rows
constexpr int kPhotoStripGap = 10;

// ---- text in the reader's language (I18n: English and Vietnamese; others show English) ----

const char* skyText(const desk::Sky s) {
  switch (s) {
    case desk::Sky::Clear:
      return tr(STR_DESK_SKY_CLEAR);
    case desk::Sky::PartlyCloudy:
      return tr(STR_DESK_SKY_PARTLY);
    case desk::Sky::Cloudy:
      return tr(STR_DESK_SKY_CLOUDY);
    case desk::Sky::Fog:
      return tr(STR_DESK_SKY_FOG);
    case desk::Sky::Drizzle:
      return tr(STR_DESK_SKY_DRIZZLE);
    case desk::Sky::Rain:
      return tr(STR_DESK_SKY_RAIN);
    case desk::Sky::Storm:
      return tr(STR_DESK_SKY_STORM);
    case desk::Sky::Snow:
      return tr(STR_DESK_SKY_SNOW);
    case desk::Sky::Unknown:
      break;
  }
  return tr(STR_DESK_SKY_UNKNOWN);
}

void rainHintText(const desk::WeatherReport& w, const uint32_t nowUnix, char* out, const size_t size) {
  out[0] = '\0';
  if (!desk::weatherFresh(w, nowUnix)) return;
  const int in = desk::hoursUntilRain(w);
  if (in == 0) {
    snprintf(out, size, "%s", tr(STR_DESK_RAIN_NOW));
  } else if (in > 0) {
    snprintf(out, size, tr(STR_DESK_RAIN_AT), (w.firstHour + in) % 24);
  } else {
    snprintf(out, size, tr(STR_DESK_NO_RAIN), desk::kForecastHours);
  }
}

const char* weekdayText(const int wd) {
  switch (wd) {
    case 0:
      return tr(STR_DESK_WEEKDAY_0);
    case 1:
      return tr(STR_DESK_WEEKDAY_1);
    case 2:
      return tr(STR_DESK_WEEKDAY_2);
    case 3:
      return tr(STR_DESK_WEEKDAY_3);
    case 4:
      return tr(STR_DESK_WEEKDAY_4);
    case 5:
      return tr(STR_DESK_WEEKDAY_5);
    default:
      return tr(STR_DESK_WEEKDAY_6);
  }
}

const char* weekdayShortText(const int wd) {
  switch (wd) {
    case 0:
      return tr(STR_DESK_WEEKDAY_SHORT_0);
    case 1:
      return tr(STR_DESK_WEEKDAY_SHORT_1);
    case 2:
      return tr(STR_DESK_WEEKDAY_SHORT_2);
    case 3:
      return tr(STR_DESK_WEEKDAY_SHORT_3);
    case 4:
      return tr(STR_DESK_WEEKDAY_SHORT_4);
    case 5:
      return tr(STR_DESK_WEEKDAY_SHORT_5);
    default:
      return tr(STR_DESK_WEEKDAY_SHORT_6);
  }
}

const char* lunarMonthText(const int m) {
  switch (m) {
    case 1:
      return tr(STR_DESK_LUNAR_MONTH_1);
    case 2:
      return tr(STR_DESK_LUNAR_MONTH_2);
    case 3:
      return tr(STR_DESK_LUNAR_MONTH_3);
    case 4:
      return tr(STR_DESK_LUNAR_MONTH_4);
    case 5:
      return tr(STR_DESK_LUNAR_MONTH_5);
    case 6:
      return tr(STR_DESK_LUNAR_MONTH_6);
    case 7:
      return tr(STR_DESK_LUNAR_MONTH_7);
    case 8:
      return tr(STR_DESK_LUNAR_MONTH_8);
    case 9:
      return tr(STR_DESK_LUNAR_MONTH_9);
    case 10:
      return tr(STR_DESK_LUNAR_MONTH_10);
    case 11:
      return tr(STR_DESK_LUNAR_MONTH_11);
    default:
      return tr(STR_DESK_LUNAR_MONTH_12);
  }
}

void eventText(const desk::CalendarEvent& ev, char* out, const size_t size) {
  using K = desk::EventKind;
  const char* fixed = nullptr;
  switch (ev.kind) {
    case K::Tet:
      fixed = tr(STR_DESK_EV_TET);
      break;
    case K::Lantern:
      fixed = tr(STR_DESK_EV_LANTERN);
      break;
    case K::HungKings:
      fixed = tr(STR_DESK_EV_HUNG);
      break;
    case K::DoanNgo:
      fixed = tr(STR_DESK_EV_DOAN_NGO);
      break;
    case K::VuLan:
      fixed = tr(STR_DESK_EV_VU_LAN);
      break;
    case K::MidAutumn:
      fixed = tr(STR_DESK_EV_MID_AUTUMN);
      break;
    case K::KitchenGods:
      fixed = tr(STR_DESK_EV_KITCHEN);
      break;
    case K::NewYear:
      fixed = tr(STR_DESK_EV_NEW_YEAR);
      break;
    case K::Reunification:
      fixed = tr(STR_DESK_EV_REUNIFICATION);
      break;
    case K::Labour:
      fixed = tr(STR_DESK_EV_LABOUR);
      break;
    case K::National:
      fixed = tr(STR_DESK_EV_NATIONAL);
      break;
    case K::FullMoon:
    case K::FirstDay:
      snprintf(out, size, ev.kind == K::FullMoon ? tr(STR_DESK_EV_FULL_MOON) : tr(STR_DESK_EV_FIRST_DAY),
               lunarMonthText(ev.lunarMonth), ev.leap ? tr(STR_DESK_LEAP) : "");
      return;
    case K::None:
      break;
  }
  snprintf(out, size, "%s", fixed != nullptr ? fixed : ev.name);
}

const char* phaseText(const desk::Pomodoro::Phase p) {
  switch (p) {
    case desk::Pomodoro::Phase::Focus:
      return tr(STR_DESK_FOCUS);
    case desk::Pomodoro::Phase::ShortBreak:
      return tr(STR_DESK_SHORT_BREAK);
    case desk::Pomodoro::Phase::LongBreak:
      return tr(STR_DESK_LONG_BREAK);
    case desk::Pomodoro::Phase::Idle:
      break;
  }
  return tr(STR_DESK_READY);
}

void uptimeText(const uint32_t seconds, char* out, const size_t size) {
  const unsigned long days = seconds / 86400;
  const unsigned long hours = (seconds % 86400) / 3600;
  const unsigned long minutes = (seconds % 3600) / 60;
  if (days > 0) {
    snprintf(out, size, tr(STR_DESK_UPTIME_DH), days, hours);
  } else if (hours > 0) {
    snprintf(out, size, tr(STR_DESK_UPTIME_HM), hours, minutes);
  } else {
    snprintf(out, size, tr(STR_DESK_UPTIME_M), minutes);
  }
}

char decimalMark() { return tr(STR_DESK_DECIMAL_MARK)[0]; }

}  // namespace

bool DeskActivity::batteryNowRequested = false;
bool DeskActivity::open = false;

// ---- time ------------------------------------------------------------------------------------

uint32_t DeskActivity::nowSec() { return millis() / 1000; }

int DeskActivity::utcOffsetMinutes() {
  // The reader's UTC offset in quarter hours, biased by 48 (as HalClock::formatTime reads it).
  const uint8_t biased = SETTINGS.clockUtcOffsetQ > 104 ? 104 : SETTINGS.clockUtcOffsetQ;
  return (static_cast<int>(biased) - 48) * 15;
}

bool DeskActivity::readUtcUnix(uint32_t& out) {
  uint16_t y = 0;
  uint8_t mo = 0, d = 0, h = 0, mi = 0;
  if (!halClock.getDateTime(y, mo, d, h, mi)) return false;
  const int32_t days = desk::julianDay(d, mo, y) - 2440588;  // 1970-01-01
  if (days < 0) return false;
  out = static_cast<uint32_t>(days) * 86400u + h * 3600u + mi * 60u;
  return true;
}

void DeskActivity::formatLocalHm(const uint32_t unixSec, char* out, const size_t size) {
  const int localMin = (static_cast<int>((unixSec / 60) % 1440) + utcOffsetMinutes() + 2 * 1440) % 1440;
  snprintf(out, size, "%02d:%02d", localMin / 60, localMin % 60);
}

DeskActivity::LocalTime DeskActivity::readLocalTime() {
  LocalTime t;
  uint16_t y = 0;
  uint8_t mo = 0, d = 0, h = 0, mi = 0;
  if (!halClock.getDateTime(y, mo, d, h, mi)) return t;
  // The RTC keeps UTC; apply the reader's UTC offset.
  int total = h * 60 + mi + utcOffsetMinutes();
  int32_t jd = desk::julianDay(d, mo, y);
  while (total < 0) {
    total += 1440;
    --jd;
  }
  while (total >= 1440) {
    total -= 1440;
    ++jd;
  }
  desk::gregorianFromJulianDay(jd, t.day, t.month, t.year);
  t.hour = total / 60;
  t.minute = total % 60;
  t.valid = true;
  return t;
}

// ---- lifecycle and scheduling ------------------------------------------------------------------

void DeskActivity::loadState() {
  page = Page::Main;
  shown = readLocalTime();
  forceHalfRefresh = true;
  minutesSinceFull = 0;
  lastPollMs = millis();
  enteredMs = lastPollMs;
  lastInputMs = lastPollMs;
  deskweather::loadConfig(config);
  // Before Settings > System > Desk (2026-11-23) these lived in desk.json: move them over once.
  bool moved = false;
  if (config.city[0] != '\0' && SETTINGS.deskCity[0] == '\0') {
    snprintf(SETTINGS.deskCity, sizeof(SETTINGS.deskCity), "%s", config.city);
    moved = true;
  }
  if (config.agent[0] != '\0' && SETTINGS.deskPcAddress[0] == '\0') {
    snprintf(SETTINGS.deskPcAddress, sizeof(SETTINGS.deskPcAddress), "%s", config.agent);
    moved = true;
  }
  if (config.fahrenheit && SETTINGS.deskTempUnit == 0) {
    SETTINGS.deskTempUnit = 1;
    moved = true;
  }
  if (moved) {
    SETTINGS.saveToFile();
    syncConfigFromSettings();
    deskweather::saveConfig(config);  // without those keys from now on
  }
  syncConfigFromSettings();
  if (!pageEnabled(page)) page = Page::Main;
  deskweather::loadCache(weather);
}

void DeskActivity::onEnter() {
  Activity::onEnter();
  open = true;
  loadState();
  uint32_t now = 0;
  const bool haveNow = readUtcUnix(now);
  // Fetch at once unless the cached forecast is younger than the interval.
  weatherFetchPending = !(weather.valid && haveNow && now >= weather.fetchedAt &&
                          now - weather.fetchedAt < kWeatherEveryMs / 1000);
  weatherAttempted = false;
  pcPollPending = true;
  pcAttempted = false;
  pcFailStreak = 0;
  loadPhotos();
  lastPhotoChangeMs = millis();
  requestUpdate();
}

// The photo frame shows Photo Slideshow's folder with its settings (owner's choice,
// 2026-11-23): folder from APP_STATE, Fit/Crop, random order, interval.
std::string DeskActivity::photoFolder() {
  std::string dir = APP_STATE.slideshowFolderPath;
  while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
  return dir;
}

// The next photo: from the SD card it is drawn at once; from the PC it is drawn once downloaded.
void DeskActivity::nextPhoto() {
  if (!photos.empty()) photoIndex = (photoIndex + 1) % static_cast<int>(photos.size());
  lastPhotoChangeMs = millis();
  if (pcPhotos()) {
    pcPhotoFailed = false;  // loop() starts the download; the photo is drawn when it is in
    return;
  }
  requestUpdate();
}

void DeskActivity::loadPhotos() {
  photos.clear();
  photoIndex = 0;
  if (pcPhotos()) {
    // The agent's photos by number (its count comes with the PC numbers).
    for (int i = 0; i < pc.photoCount && i < static_cast<int>(kMaxPhotos); ++i) photos.push_back(std::to_string(i));
    if (SETTINGS.slideshowRandomOrder && photos.size() > 1) {
      for (size_t i = photos.size() - 1; i > 0; --i) {
        const size_t j = static_cast<size_t>(random(static_cast<long>(i + 1)));
        std::swap(photos[i], photos[j]);
      }
    }
    pcPhotoLoaded = -1;
    pcPhotoFailed = false;
    return;
  }
  const std::string folder = photoFolder();
  if (folder.empty()) return;
  auto dir = Storage.open(folder.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  char name[256];
  for (auto file = dir.openNextFile(); file && photos.size() < kMaxPhotos; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      const std::string n(name);
      // The slideshow's rule: BMP or PNG, no hidden files or macOS "._" sidecars.
      if (name[0] != '.' && (FsHelpers::hasBmpExtension(n) || FsHelpers::hasPngExtension(n))) photos.push_back(n);
    }
    file.close();
  }
  dir.close();
  FsHelpers::sortFileList(photos);
  if (SETTINGS.slideshowRandomOrder && photos.size() > 1) {
    for (size_t i = photos.size() - 1; i > 0; --i) {
      const size_t j = static_cast<size_t>(random(static_cast<long>(i + 1)));
      std::swap(photos[i], photos[j]);
    }
  }
}

void DeskActivity::switchPage(const Page to) {
  page = to;
  forceHalfRefresh = true;
  requestUpdate();
}

void DeskActivity::onExit() {
  // The job runs on its own task and holds this activity: it has to end before Desk does. Told to
  // give up, it ends within the step in progress (a 20 s Wi-Fi wait once held the exit, X3
  // owner's report 2026-11-24).
  deskweather::setCancel(true);
  waitNetJob();
  deskweather::setCancel(false);
  Activity::onExit();
  open = false;
  batteryNowRequested = false;
  if (wifiKeptForUsb) deskweather::wifiDown();
  wifiKeptForUsb = false;
}

bool DeskActivity::preventAutoSleep() {
  // The photo frame and the PC page stay up on battery too (back to the clock after an hour alone).
  return deskpower::externalPower() || netBusy || pomodoro.running() || page == Page::Photo || page == Page::Pc ||
         lightKeepsAwake();
}

bool DeskActivity::lightKeepsAwake() {
  // The light's PWM stops in light and deep sleep, so a lit Desk can only stay awake. Below 10 %
  // the battery saver (light off) or the reader's sleep takes over again.
  return SETTINGS.deskLightAwake && Frontlight.present() && Frontlight.isOn() &&
         (deskpower::externalPower() || powerManager.getBatteryPercentage() >= 10);
}

void DeskActivity::startNetJob(const bool wantWeather, const bool wantPc, const bool userAsked, const int photoIndex) {
  if (netBusy || (!wantWeather && !wantPc && photoIndex < 0)) return;
  netPhotoIndex = photoIndex;
  if (wantWeather) weatherFetchPending = false;
  if (wantPc) pcPollPending = false;
  netWantWeather = wantWeather;
  netWantPc = wantPc;
  netUserAsked = userAsked;
  netPcRan = false;
  uint32_t now = 0;
  if (!readUtcUnix(now)) now = weather.fetchedAt;
  netNow = now;
  // The PC page polls every minute and PC photos come one by one: keep Wi-Fi up for them.
  netKeepWifi = deskpower::externalPower() || page == Page::Pc || (page == Page::Photo && pcPhotos());
  netConfig = config;
  netWeather = weather;
  netPc = pc;
  weatherFetching = wantWeather;  // "Updating..." on the weather card
  pcPolling = wantPc;
  netDone = false;
  netBusy = true;
  if (xTaskCreate(&DeskActivity::netTaskEntry, "DeskNet", kNetTaskStackBytes, this, 1, nullptr) != pdPASS) {
    LOG_ERR("DESK", "net task not started: running inline");
    runNetJob();
    netDone = true;
  }
  if (page == Page::Main && wantWeather) requestUpdate();
}

void DeskActivity::netTaskEntry(void* arg) {
  auto* self = static_cast<DeskActivity*>(arg);
  self->runNetJob();
  self->netDone = true;
  vTaskDelete(nullptr);
}

void DeskActivity::runNetJob() {
  if (netWantWeather) {
    netWeatherResult = deskweather::refresh(netConfig, netWeather, netNow, netKeepWifi);
    // No network at all: the PC poll would only fail the same way.
    if (netWeatherResult == deskweather::Result::NoSavedWifi || netWeatherResult == deskweather::Result::WifiFailed) {
      return;
    }
  }
  if (deskweather::cancelled()) return;  // Desk is closing
  if (netWantPc) {
    netPcResult = deskagent::poll(netConfig, netPc, netNow, netKeepWifi);
    netPcRan = true;
  }
  if (netPhotoIndex >= 0 && !deskweather::cancelled()) {
    netPhotoResult = deskagent::downloadPhoto(netConfig, netPhotoIndex, kPcPhotoPath, netKeepWifi);
  }
}

void DeskActivity::waitNetJob() {
  if (!netBusy) return;
  while (!netDone) delay(20);
  netUserAsked = false;  // leaving or going to the battery loop: no Wi-Fi picker
  applyNetJob();
}

namespace {
void batteryLogNow(const char* event);
}  // namespace

bool DeskActivity::applyNetJob() {
  if (!netBusy || !netDone) return false;
  netBusy = false;
  const uint32_t nowMs = millis();
  config = netConfig;
  wifiKeptForUsb = wifiKeptForUsb || netKeepWifi;
  if (netWantWeather) {
    weather = netWeather;
    weatherResult = netWeatherResult;
    weatherFetching = false;
    // Failed fetches outside the battery loop go to the same log ("net-weather-...").
    if (!deskweather::cancelled() && (weatherResult == deskweather::Result::FetchFailed ||
                                      weatherResult == deskweather::Result::PlaceFailed)) {
      const deskweather::FetchInfo info = deskweather::lastFetchInfo();
      char event[48];
      snprintf(event, sizeof(event), "net-weather-%d-s%d-h%d-m%d", static_cast<int>(weatherResult), info.status,
               info.freeKb, info.maxBlockKb);
      batteryLogNow(event);
    }
    weatherAttempted = true;
    lastWeatherAttemptMs = nowMs;
    if (page == Page::Main) forceHalfRefresh = true;
  }
  if (netWantPc) {
    pcPolling = false;
    if (netPcRan) {
      pc = netPc;
      pcResult = netPcResult;
      pcAttempted = true;
      lastPcAttemptMs = nowMs;
      if (pcResult == deskweather::Result::Ok) {
        pcFailStreak = 0;
      } else if (pcFailStreak < 255) {
        ++pcFailStreak;
      }
    }
  }
  if (netPhotoIndex >= 0) {
    const bool sameShown = netPhotoIndex == photoIndex;
    if (netPhotoResult == deskweather::Result::Ok) {
      pcPhotoLoaded = netPhotoIndex;
    } else if (sameShown) {
      pcPhotoFailed = true;
    }
    netPhotoIndex = -1;
    if (page == Page::Photo && sameShown) requestUpdate();
  }
  // The PC's photo count changed: a new list (a PC photo page shows the first one).
  if (pcPhotos() && netWantPc && netPcRan && static_cast<int>(photos.size()) != pc.photoCount) {
    loadPhotos();
    if (page == Page::Photo) requestUpdate();
  }
  if (page != Page::Photo) requestUpdate();  // a photo is not redrawn for new numbers

  const bool noNetwork = netWantWeather && (weatherResult == deskweather::Result::NoSavedWifi ||
                                            weatherResult == deskweather::Result::WifiFailed);
  if (!netUserAsked || !noNetwork || !open) return false;
  // No network to use: pick or join one (the picker saves it), then fetch again.
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             weatherFetchPending = true;
                             pcPollPending = true;
                           }
                           lastInputMs = millis();
                           forceHalfRefresh = true;
                           requestUpdate();
                         });
  return true;
}

// Settings > System > Desk, opened straight away (hold Select on any page). Desk reads its
// settings from SETTINGS; the service config takes the place and PC address from there.
void DeskActivity::openSettings() {
  if (netBusy) return;  // the job holds a copy of the config
  const std::string oldCity = SETTINGS.deskCity;
  const std::string oldPc = SETTINGS.deskPcAddress;
  const uint8_t oldPhotoSource = SETTINGS.deskPhotoSource;
  auto settings = std::make_unique<SettingsActivity>(renderer, mappedInput, false, true);
  settings->openDeskSettingsOnEnter();
  startActivityForResult(std::move(settings), [this, oldCity, oldPc, oldPhotoSource](const ActivityResult&) {
    lastInputMs = millis();
    forceHalfRefresh = true;
    syncConfigFromSettings();
    if (oldCity != SETTINGS.deskCity) {
      config.resolvedFor[0] = '\0';  // resolve the place again on the next fetch
      config.place.valid = false;
      weather = desk::WeatherReport{};  // the old place's forecast must not linger
      weatherFetchPending = true;
    }
    if (oldPc != SETTINGS.deskPcAddress) {
      pc = desk::AgentStats{};  // numbers from another PC must not linger
      pcPollPending = true;
      pcFailStreak = 0;
    }
    if (oldPhotoSource != SETTINGS.deskPhotoSource) loadPhotos();
    if (!pageEnabled(page)) page = Page::Main;
    requestUpdate();
  });
}

// SETTINGS -> the service config (place by name, PC address).
void DeskActivity::syncConfigFromSettings() {
  snprintf(config.city, sizeof(config.city), "%s", SETTINGS.deskCity);
  snprintf(config.agent, sizeof(config.agent), "%s", SETTINGS.deskPcAddress);
}

bool DeskActivity::pageEnabled(const Page p) {
  switch (p) {
    case Page::Calendar:
      return SETTINGS.deskShowCalendar;
    case Page::Pc:
      return SETTINGS.deskShowPc;
    case Page::Photo:
      return SETTINGS.deskShowPhoto;
    case Page::Pomodoro:
      return SETTINGS.deskShowPomodoro;
    default:
      return true;
  }
}

const char* DeskActivity::pageName(const Page p) {
  switch (p) {
    case Page::Main:
      return tr(STR_DESK_TAB_CLOCK);
    case Page::Calendar:
      return tr(STR_DESK_TAB_CALENDAR);
    case Page::Pc:
      return tr(STR_DESK_TAB_PC);
    case Page::Photo:
      return tr(STR_DESK_TAB_PHOTO);
    case Page::Pomodoro:
    case Page::Count:
      break;
  }
  return tr(STR_DESK_TAB_POMO);  // "Pomodoro" does not fit a button
}

DeskActivity::Page DeskActivity::neighbour(const Page p, const int step) {
  // The next page that is turned on (Settings > System > Desk); the clock always is.
  const int n = static_cast<int>(Page::Count);
  int i = static_cast<int>(p);
  for (int tries = 0; tries < n; ++tries) {
    i = (i + step + n) % n;
    if (pageEnabled(static_cast<Page>(i))) return static_cast<Page>(i);
  }
  return Page::Main;
}

// Back, Select, and the two page buttons named after the pages they open (no arrows:
// "« Pomodoro" does not fit a button).
void DeskActivity::drawPageHints(const char* back, const char* confirm) const {
  drawHints(back, confirm, pageName(neighbour(page, -1)), pageName(neighbour(page, 1)));
}

namespace {
constexpr int kTouchBarH = 44;
constexpr int kTouchBarBottom = 6;
constexpr int kTouchBarGap = 6;
}  // namespace

void DeskActivity::drawHints(const char* back, const char* confirm, const char* prev, const char* next) const {
  if (!mappedInput.hasTouch()) {
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(back), confirm, prev, next);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  const int w = renderer.getScreenWidth();
  const int y = renderer.getScreenHeight() - kTouchBarBottom - kTouchBarH;
  const int cellW = (w - 2 * kMargin - 3 * kTouchBarGap) / 4;
  const char* labels[] = {back, confirm, prev, next};
  const int textY = y + (kTouchBarH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  for (int i = 0; i < 4; ++i) {
    if (labels[i] == nullptr || labels[i][0] == '\0') continue;
    touchBarMask |= 1u << i;
    const int x = kMargin + i * (cellW + kTouchBarGap);
    renderer.fillRect(x, y, cellW, kTouchBarH, false);
    renderer.drawRect(x, y, cellW, kTouchBarH);
    const std::string text = renderer.truncatedText(UI_10_FONT_ID, labels[i], cellW - 8);
    renderer.drawText(UI_10_FONT_ID, x + (cellW - renderer.getTextWidth(UI_10_FONT_ID, text.c_str())) / 2, textY,
                      text.c_str());
  }
}

int DeskActivity::touchHintAt(const int x, const int y) const {
  const int top = renderer.getScreenHeight() - kTouchBarBottom - kTouchBarH;
  if (y < top - 8 || x < kMargin) return -1;  // a little slack above the bar
  const int cellW = (renderer.getScreenWidth() - 2 * kMargin - 3 * kTouchBarGap) / 4;
  const int i = std::min((x - kMargin) / (cellW + kTouchBarGap), 3);
  return touchBarMask & (1u << i) ? i : -1;
}

// Quiet hours (Settings > System > Desk): from deskNightStart to deskNightEnd, local time.
bool DeskActivity::isNight(const LocalTime& t) {
  if (!SETTINGS.deskNightMode || !t.valid) return false;
  const int from = SETTINGS.deskNightStart % 24;
  const int to = SETTINGS.deskNightEnd % 24;
  if (from == to) return false;
  return from < to ? (t.hour >= from && t.hour < to) : (t.hour >= from || t.hour < to);
}

// No PC answering (switched off, agent not running, none at all): each try brings Wi-Fi up and,
// without a fixed address, waits on an mDNS query. The wait doubles with every miss in a row -
// 2, 4, 8, 16 min - and stays at 30 min; an answer, a new address or Refresh starts it over.
uint32_t DeskActivity::pcRetryMs() const {
  constexpr uint32_t kMaxMs = 30 * 60 * 1000;
  const uint8_t doublings = pcFailStreak > 1 ? static_cast<uint8_t>(std::min<int>(pcFailStreak - 1, 4)) : 0;
  return std::min<uint32_t>(kPcRetryMs << doublings, kMaxMs);
}

bool DeskActivity::pcFresh(const uint32_t nowUnix) const {
  return pc.valid && nowUnix >= pc.receivedAt && nowUnix - pc.receivedAt <= kPcFreshSec;
}

// ---- battery loop (phase 5) ------------------------------------------------------------------

void DeskActivity::resumeBatteryFromBoot() {
  loadState();
  const deskpower::BootWake wake = deskpower::bootWake();
  deskpower::TickState tick{};
  const bool haveTick = deskpower::loadTickState(tick);
  deskpower::clearTickState();
  if (wake == deskpower::BootWake::PowerButton) {
    // The press that woke a deep-sleep tick: millis() is about how long it has been down.
    batteryMode = true;
    leaveBatteryLoop(false, millis());
  }
  if (wake == deskpower::BootWake::Timer && haveTick) runBatteryLoop(true, &tick);
  deskpower::countBatteryReset();
  runBatteryLoop(true);
}

namespace {
void batteryLogNow(const char* event) {
  char when[24] = "--";
  uint16_t y = 0;
  uint8_t mo = 0, d = 0, h = 0, mi = 0;
  if (halClock.getDateTime(y, mo, d, h, mi)) {
    snprintf(when, sizeof(when), "%04u-%02u-%02u %02u:%02uZ", y, mo, d, h, mi);  // UTC, as the RTC keeps it
  }
  deskpower::logBattery(event, when, deskpower::readGauge(), deskpower::batteryResets());
}
}  // namespace

void DeskActivity::leaveBatteryLoop(const bool touch, const uint32_t heldBeforeMs) {
  deskpower::clearTickState();
  // The press that woke the sleep is still held: restarting now would hand its release to the
  // new boot as a short press, i.e. a sleep (owner's report, 2026-11-23).
  // A press let go before now counts as short (a wake's boot takes longer than a tap).
  const uint32_t waitedMs = deskpower::waitPowerButtonRelease();
  const uint32_t heldMs = waitedMs > 0 ? heldBeforeMs + waitedMs : 0;
  // A long press powers off (a real sleep from a fresh boot, so the RAM a long run has split
  // up starts whole again); a short one opens the full Desk (owner's request, 2026-11-24).
  const bool powerOff =
      !touch && !deskpower::externalPower() && heldMs >= SETTINGS.getPowerButtonLongPressDuration();
  batteryLogNow(deskpower::externalPower() ? "exit-usb"
                : touch                    ? "exit-touch"
                : powerOff                 ? "exit-off"
                                           : "exit-button");
  deskpower::watchdogDisarm();
  deskpower::setBatteryRunArmed(false);
  if (powerOff) {
    deskpower::requestSleepAfterRestart();
  } else {
    deskpower::requestOpenDeskAfterRestart();
  }
  ESP.restart();
  for (;;) {
    delay(1000);
  }
}

void DeskActivity::runBatteryLoop(const bool fromBoot, const deskpower::TickState* tick) {
  page = Page::Main;
  batteryMode = true;
  forceHalfRefresh = tick == nullptr;  // a deep-sleep tick goes on with the run's own count
  if (!fromBoot) {
    waitNetJob();  // the loop owns Wi-Fi from here on
    // Let a render already queued finish before the loop draws without the render task.
    requestUpdateAndWait();
  }
  if (wifiKeptForUsb || WiFi.getMode() != WIFI_MODE_NULL) deskweather::wifiDown();
  wifiKeptForUsb = false;
  // The frontlight's PWM stops in light sleep, so a light left on blinked on and off with every
  // tick (X4 Pro, owner's report 2026-11-23). Off for the run; leaving it restarts the reader,
  // which restores the light by its own settings.
  Frontlight.setOn(false);
#ifndef SIMULATOR
  gpio.stopButtonCapture();
#endif
  // Full CPU speed for the whole run (light sleep stops the clock anyway, so this only costs the
  // ~0.5 s awake per tick). Desk on battery does not prevent auto-sleep, so the main loop had
  // dropped the CPU to 10 MHz before the loop began; on the X3 the APB clock follows the CPU below
  // 80 MHz, which throws off the panel's SPI timing. Runs stopped after one or two ticks that way
  // (battery log, 2026-11-23), while probe v6 - which kept full speed - ran 25.
  HalPowerManager::Lock powerLock;
  deskpower::setBatteryRunArmed(true);
  deskpower::watchdogArm(kBatteryWatchdogMs);
  if (tick == nullptr) {
    // With the last reset's cause (esp_reset_reason_t: 1 power-on, 3 software restart, 6/7 task
    // watchdog, 9 RTC watchdog...), so a freeze cleared by hand shows up as the next run's cause.
    char event[24];
#ifndef SIMULATOR
    const int reason = static_cast<int>(esp_reset_reason());
#else
    const int reason = 0;
#endif
    // And where the last run was when it stopped ("-c1": in light sleep, see setCrumb).
    snprintf(event, sizeof(event), "%s-r%d-c%u", fromBoot ? "resume" : "enter", reason,
             static_cast<unsigned>(deskpower::lastCrumb()));
    batteryLogNow(event);
    deskpower::setCrumb(0);
  }

  int ticksSinceHalf = tick ? tick->ticksSinceHalf : 0;
  uint32_t lastWeatherTry = tick ? tick->lastWeatherTry : 0;  // RTC seconds of the last failed try in this run
  if (tick) {
    shown.year = tick->year;  // the day the last tick showed, for the new-day check
    shown.month = tick->month;
    shown.day = tick->day;
  }
  for (;;) {
    deskpower::watchdogFeed();
    if (deskpower::externalPower()) leaveBatteryLoop();  // charging: the full Desk again
    batteryLogNow("tick");
    deskpower::setCrumb(4);

    const LocalTime t = readLocalTime();
    const bool newDay = t.day != shown.day || t.month != shown.month || t.year != shown.year;
    shown = t;
    if (newDay) {
      pomodoro.newDay();
      forceHalfRefresh = true;
    }
    if (++ticksSinceHalf >= kBatteryHalfRefreshEvery) forceHalfRefresh = true;
    if (forceHalfRefresh) ticksSinceHalf = 0;

    uint32_t now = 0;
    const bool haveNow = readUtcUnix(now);
    const bool due = !weather.valid || now < weather.fetchedAt || now - weather.fetchedAt >= batteryWeatherSec();
    const bool backingOff = lastWeatherTry != 0 && now >= lastWeatherTry && now - lastWeatherTry < kBatteryWeatherRetrySec;
    const bool night = isNight(t);
    if (haveNow && due && !backingOff && !night) {
      const uint32_t startMs = millis();
      weatherResult = deskweather::refresh(config, weather, now, false);
      weatherAttempted = true;
      lastWeatherTry = weatherResult == deskweather::Result::Ok ? 0 : now;
      deskpower::watchdogFeed();
      // In the battery log: why a forecast went missing, and how long Wi-Fi was up.
      // A failed fetch adds its HTTP status and the heap (free / largest block, KB) as it began.
      char event[64];
      const deskweather::FetchInfo info = deskweather::lastFetchInfo();
      if (weatherResult == deskweather::Result::FetchFailed || weatherResult == deskweather::Result::PlaceFailed) {
        snprintf(event, sizeof(event), "weather-%d-%lums-s%d-h%d-m%d", static_cast<int>(weatherResult),
                 static_cast<unsigned long>(millis() - startMs), info.status, info.freeKb, info.maxBlockKb);
      } else {
        snprintf(event, sizeof(event), "weather-%d-%lums", static_cast<int>(weatherResult),
                 static_cast<unsigned long>(millis() - startMs));
      }
      batteryLogNow(event);
    }

    deskpower::setCrumb(5);
    drawPage();
    deskpower::setCrumb(6);
    batteryLogNow("drawn");  // a run that stops after "tick" hangs drawing, after "drawn" sleeping
    uint32_t sleepMs = deskpower::msToNextMinute();
    if (night && t.valid) {
      // At night the clock moves on the half hour: sleep to the next :00 or :30.
      const int minutesLeft = kNightTickMinutes - (t.minute % kNightTickMinutes);
      sleepMs += static_cast<uint32_t>(minutesLeft - 1) * 60000u;
    }
    if (deskpower::deepSleepTicks()) {
      deskpower::TickState next{};
      next.ticksSinceHalf = ticksSinceHalf;
      next.lastWeatherTry = lastWeatherTry;
      next.year = static_cast<uint16_t>(t.year);
      next.month = static_cast<uint8_t>(t.month);
      next.day = static_cast<uint8_t>(t.day);
      deskpower::deepSleepFor(next, sleepMs);  // the next tick is a fresh boot
    }
    const deskpower::Wake wake = deskpower::sleepFor(sleepMs);
    deskpower::setCrumb(3);
    if (wake != deskpower::Wake::Timer) leaveBatteryLoop(wake == deskpower::Wake::Touch);
  }
}

void DeskActivity::loop() {
  // Touch (X4 Pro, which has no front buttons to spare): the touch bar's buttons act as the keys
  // they stand for, and a sideways swipe across the screen turns the page (left: next). A swipe
  // in from the left edge is the system's Back, taken below before any page turn.
  int tapX = 0;
  int tapY = 0;
  const bool touch = mappedInput.hasTouch();
  const bool tappedAnywhere = touch && mappedInput.wasScreenTapped(tapX, tapY);
  const int tapped = tappedAnywhere ? touchHintAt(tapX, tapY) : -1;
  const auto swipe = touch ? mappedInput.wasSwipe() : MappedInputManager::SwipeDir::None;
  const bool swipePrev = swipe == MappedInputManager::SwipeDir::Right;
  const bool swipeNext = swipe == MappedInputManager::SwipeDir::Left;
  // On the X3 the two right-hand front buttons report Left/Right; Up/Down are the side keys.
  const bool up = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                  mappedInput.wasPressed(MappedInputManager::Button::Left) || swipePrev || tapped == 2;
  const bool down = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                    mappedInput.wasPressed(MappedInputManager::Button::Right) || swipeNext || tapped == 3;
  const bool back = mappedInput.wasPressed(MappedInputManager::Button::Back) || tapped == 0;
  // Select acts on release: a short press is the page's action, a hold opens Desk settings. The
  // hold is measured while the button is down (the held time is gone once it is released) and
  // acted on at the release, so the release does not reach the screen it opens.
  const bool selectReleased =
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) && millis() - enteredMs > 400;
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && millis() - enteredMs > 400 &&
      mappedInput.getHeldTime() >= kAddressHoldMs) {
    selectWasHeld = true;
  }
  const bool selectHeld = selectReleased && selectWasHeld;
  if (selectReleased) selectWasHeld = false;
  const bool select = selectReleased || tapped == 1;
  if (up || down || select || back || tappedAnywhere || swipe != MappedInputManager::SwipeDir::None) {
    lastInputMs = millis();
  }

  if (applyNetJob()) return;  // opened the Wi-Fi picker

  // A sleep asked for (power button, idle timeout) with "Sleep opens Desk" on: battery loop now.
  if (batteryNowRequested) {
    batteryNowRequested = false;
    if (!deskpower::externalPower() && deskpower::batteryLoopSupported() && !lightKeepsAwake()) runBatteryLoop(false);
  }
  // On battery and left alone: the minute-tick loop in light sleep (does not return).
  if (deskpower::batteryLoopSupported() && !deskpower::externalPower() && !pomodoro.running() && !lightKeepsAwake() &&
      !weatherFetchPending && !pcPollPending && !netBusy &&
      millis() - lastInputMs > kBatteryIdleMs && page != Page::Photo && page != Page::Pc) {
    runBatteryLoop(false);
  }

  if (back) {
    if (page != Page::Main) {
      switchPage(Page::Main);
      return;
    }
    finish();
    return;
  }
  // The side keys (Up/Down) always switch pages. The two right-hand front buttons (Left/Right)
  // switch pages too, except where their labels say otherwise: on the calendar they change the
  // month, and with a Pomodoro timer set they skip the phase and reset. The side keys changed
  // the month once, which left them no way off the calendar (owner's report, 2026-11-23).
  // The touch bar's last two buttons carry the same labels as the front buttons; a swipe always
  // turns the page.
  const bool frontPrev = mappedInput.wasPressed(MappedInputManager::Button::Left) || tapped == 2;
  const bool frontNext = mappedInput.wasPressed(MappedInputManager::Button::Right) || tapped == 3;
  if (page == Page::Pomodoro && pomodoro.phase() != desk::Pomodoro::Phase::Idle && (frontPrev || frontNext)) {
    if (frontPrev) {
      pomodoro.skip(nowSec());
    } else {
      pomodoro.reset();
    }
    requestUpdate();
    return;
  }
  if (page == Page::Calendar && (frontPrev || frontNext)) {
    calendarOffset += frontNext ? 1 : -1;
    requestUpdate();
    return;
  }
  if (up || down) {
    calendarOffset = 0;
    switchPage(neighbour(page, down ? 1 : -1));
    return;
  }
  if (selectHeld) {
    openSettings();
    return;
  }
  if (select) {
    switch (page) {
      case Page::Main:
        shown = readLocalTime();
        startNetJob(true, true, true);
        break;
      case Page::Pc:
        pcFailStreak = 0;  // asked for: back to the short retries
        startNetJob(false, true, false);
        break;
      case Page::Calendar:
        calendarOffset = 0;
        requestUpdate();
        break;
      case Page::Photo:
        nextPhoto();
        break;
      case Page::Pomodoro:
        pomodoro.toggle(nowSec());
        requestUpdate();
        break;
      case Page::Count:
        break;
    }
    return;
  }

  // Pomodoro runs on every page; the screen jumps to it when a phase ends.
  if (pomodoro.update(nowSec()) == desk::Pomodoro::Event::PhaseEnded && pageEnabled(Page::Pomodoro)) {
    switchPage(Page::Pomodoro);
    return;
  }
  if (page == Page::Pomodoro && pomodoro.remainingMinutes(nowSec()) != shownPomodoroMinutes) {
    requestUpdate();
  }

  const uint32_t nowMs = millis();
  const uint32_t photoEveryMs = static_cast<uint32_t>(SETTINGS.slideshowIntervalSeconds) * 1000u;
  if ((page == Page::Photo || page == Page::Pc) && !deskpower::externalPower() &&
      nowMs - lastInputMs >= kPhotoUnattendedMs) {
    switchPage(Page::Main);  // on battery: the clock (and its battery loop) after an hour alone
    return;
  }
  // Off the PC page on battery: Wi-Fi kept up for its polls goes down.
  if (wifiKeptForUsb && !netBusy && page != Page::Pc && !deskpower::externalPower()) {
    deskweather::wifiDown();
    wifiKeptForUsb = false;
  }
  if (page == Page::Photo && photos.size() > 1 && photoEveryMs > 0 && nowMs - lastPhotoChangeMs >= photoEveryMs &&
      (!pcPhotos() || pcPhotoLoaded == photoIndex || pcPhotoFailed)) {
    nextPhoto();
    return;
  }
  // Photos from the PC: the count comes with the PC numbers, each photo is downloaded when shown.
  if (page == Page::Photo && pcPhotos() && !netBusy) {
    if (photos.empty()) {
      if (!pcPollPending && (!pcAttempted || nowMs - lastPcAttemptMs >= pcRetryMs())) pcPollPending = true;
    } else if (pcPhotoLoaded != photoIndex && !pcPhotoFailed) {
      startNetJob(false, false, false, photoIndex);
      return;
    }
  }
  if (!netBusy) {
    bool wantWeather = weatherFetchPending;
    bool wantPc = pcPollPending;
    if (deskpower::externalPower() && !isNight(shown)) {
      const uint32_t weatherWait = weatherResult == deskweather::Result::Ok ? kWeatherEveryMs : kWeatherRetryMs;
      wantWeather = wantWeather || (weatherAttempted && nowMs - lastWeatherAttemptMs >= weatherWait);
    } else if (lightKeepsAwake() && !isNight(shown)) {
      // Awake on battery for the light: weather at the battery saver's pace.
      const uint32_t weatherWait =
          (weatherResult == deskweather::Result::Ok ? batteryWeatherSec() : kBatteryWeatherRetrySec) * 1000u;
      wantWeather = wantWeather || (weatherAttempted && nowMs - lastWeatherAttemptMs >= weatherWait);
    }
    if (deskpower::externalPower() || page == Page::Pc) {
      const uint32_t pcWait = pcResult == deskweather::Result::Ok ? kPcEveryMs : pcRetryMs();
      wantPc = wantPc || (pcAttempted && nowMs - lastPcAttemptMs >= pcWait);
    }
    if (wantWeather || wantPc) startNetJob(wantWeather, wantPc, false);
  }

  if (nowMs - lastPollMs < kPollMs) return;
  lastPollMs = nowMs;
  const LocalTime t = readLocalTime();
  if (t.valid == shown.valid && t.minute == shown.minute && t.hour == shown.hour && t.day == shown.day) return;
  const bool newDay = t.day != shown.day || t.month != shown.month || t.year != shown.year;
  shown = t;
  if (newDay) pomodoro.newDay();
  if (page == Page::Photo) return;  // a photo is not redrawn for the clock
  if (newDay || ++minutesSinceFull >= kHalfRefreshEveryMinutes) forceHalfRefresh = true;
  requestUpdate();
}

// ---- drawing ---------------------------------------------------------------------------------

void DeskActivity::drawStatusBar(const char* title, const char* middle) const {
  const int w = renderer.getScreenWidth();
  char buf[32];
  renderer.drawText(UI_10_FONT_ID, kMargin, 14, title, true, EpdFontFamily::BOLD);
  if (deskpower::externalPower()) {
    snprintf(buf, sizeof(buf), "%s", tr(STR_DESK_CHARGING));
  } else {
    snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
  }
  renderer.drawText(UI_10_FONT_ID, w - kMargin - renderer.getTextWidth(UI_10_FONT_ID, buf), 14, buf);
  if (middle != nullptr && middle[0] != '\0') renderer.drawCenteredText(UI_10_FONT_ID, 14, middle);
  renderer.fillRect(0, 44, w, 2, true);
}

void DeskActivity::drawBar(const int x, const int y, const int w, const int h, const int pct) const {
  renderer.drawRect(x, y, w, h, 2, true);
  const int inner = (w - 4) * (pct < 0 ? 0 : (pct > 100 ? 100 : pct)) / 100;
  if (inner > 0) renderer.fillRect(x + 2, y + 2, inner, h - 4, true);
}

// A three-quarter ring gauge (owner's pick, design C, 2026-11-23) centred on (cx, cy): a band of
// the given thickness from lower left, over the top, to lower right (270 degrees), filled black
// clockwise up to pct and outlined beyond; the gap at the bottom is left for a label. Drawn
// pixel by pixel so the band is solid at any size.
void DeskActivity::drawGauge(const int cx, const int cy, const int r, int pct, const int thickness) const {
  pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
  constexpr double kStart = 0.75 * M_PI;  // lower left, screen angles (y down, clockwise)
  constexpr double kSweep = 1.5 * M_PI;
  const int ri = r - thickness;
  const int outer2 = r * r;
  const int inner2 = ri * ri;
  const int edgeOut2 = (r - 2) * (r - 2);
  const int edgeIn2 = (ri + 2) * (ri + 2);
  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      const int d2 = dx * dx + dy * dy;
      if (d2 > outer2 || d2 < inner2) continue;
      double a = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) - kStart;
      while (a < 0) a += 2 * M_PI;
      if (a > kSweep) continue;  // the gap at the bottom
      const double f = a / kSweep;
      const double d = std::sqrt(static_cast<double>(d2));
      const bool filled = pct > 0 && f * 100.0 <= pct;
      const bool outline = d2 >= edgeOut2 || d2 <= edgeIn2 || a * d <= 2.0 || (kSweep - a) * d <= 2.0;
      if (filled || outline) renderer.drawPixel(cx + dx, cy + dy, true);
    }
  }
}

void DeskActivity::drawSkyIcon(const int x, const int y, const int s, const desk::Sky sky, const bool day) const {
  using desk::Sky;
  auto disc = [this](const int cx, const int cy, const int r, const Color c) {
    renderer.fillRoundedRect(cx - r, cy - r, 2 * r, 2 * r, r, c);
  };
  auto cloud = [&](const int top, const int h) {
    // A filled silhouette: two bumps over a rounded base.
    disc(x + s * 36 / 100, top + h * 45 / 100, h * 32 / 100, Color::Black);
    disc(x + s * 62 / 100, top + h * 35 / 100, h * 40 / 100, Color::Black);
    renderer.fillRoundedRect(x + s * 10 / 100, top + h * 45 / 100, s * 80 / 100, h * 45 / 100, h * 22 / 100,
                             Color::Black);
  };

  switch (sky) {
    case Sky::Clear:
      if (day) {
        disc(x + s / 2, y + s / 2, s * 22 / 100, Color::Black);
        for (int i = 0; i < 8; ++i) {
          static const int kDx[8] = {0, 71, 100, 71, 0, -71, -100, -71};
          static const int kDy[8] = {-100, -71, 0, 71, 100, 71, 0, -71};
          const int cx = x + s / 2, cy = y + s / 2;
          renderer.drawLine(cx + kDx[i] * s * 32 / 10000, cy + kDy[i] * s * 32 / 10000, cx + kDx[i] * s * 46 / 10000,
                            cy + kDy[i] * s * 46 / 10000, 4, true);
        }
      } else {
        disc(x + s / 2, y + s / 2, s * 34 / 100, Color::Black);
        disc(x + s * 64 / 100, y + s * 38 / 100, s * 30 / 100, Color::White);  // crescent
      }
      break;
    case Sky::PartlyCloudy:
      disc(x + s * 34 / 100, y + s * 34 / 100, s * 22 / 100, Color::Black);
      cloud(y + s * 30 / 100, s * 62 / 100);
      break;
    case Sky::Cloudy:
      cloud(y + s * 15 / 100, s * 70 / 100);
      break;
    case Sky::Fog:
      for (int i = 0; i < 4; ++i) {
        renderer.fillRect(x + s * (i % 2 ? 10 : 20) / 100, y + s * (22 + i * 18) / 100, s * 70 / 100, s * 8 / 100,
                          true);
      }
      break;
    case Sky::Drizzle:
    case Sky::Rain:
    case Sky::Storm:
    case Sky::Snow: {
      cloud(y, s * 62 / 100);
      const int dropTop = y + s * 66 / 100;
      if (sky == Sky::Storm) {
        const int bx[5] = {x + s * 52 / 100, x + s * 38 / 100, x + s * 50 / 100, x + s * 42 / 100, x + s * 64 / 100};
        const int by[5] = {dropTop - 4, dropTop + s * 18 / 100, dropTop + s * 18 / 100, y + s, dropTop + s * 10 / 100};
        renderer.fillPolygon(bx, by, 5, true);
      } else if (sky == Sky::Snow) {
        for (int i = 0; i < 3; ++i) {
          disc(x + s * (28 + i * 22) / 100, dropTop + s * (i % 2 ? 18 : 10) / 100, 4, Color::Black);
        }
      } else {
        const int n = sky == Sky::Drizzle ? 2 : 3;
        for (int i = 0; i < n; ++i) {
          const int dx = x + s * (30 + i * (n == 2 ? 30 : 20)) / 100;
          renderer.drawLine(dx, dropTop, dx - s * 8 / 100, dropTop + s * 26 / 100, 4, true);
        }
      }
      break;
    }
    case Sky::Unknown:
      renderer.drawRoundedRect(x + s / 6, y + s / 6, s * 2 / 3, s * 2 / 3, 3, 8, true);
      renderer.drawText(UI_12_FONT_ID, x + s / 2 - renderer.getTextWidth(UI_12_FONT_ID, "?", EpdFontFamily::BOLD) / 2,
                        y + s / 2 - 12, "?", true, EpdFontFamily::BOLD);
      break;
  }
}

void DeskActivity::drawWeatherCard(const int top, const int height, const uint32_t nowUnix) const {
  const int w = renderer.getScreenWidth();
  const int left = kMargin;
  const int cardW = w - 2 * kMargin;
  char buf[96];
  renderer.drawRoundedRect(left, top, cardW, height, 2, 10, true);

  if (!weather.valid) {
    const char* msg = weatherFetching
                          ? tr(STR_DESK_UPDATING)
                          : (weatherAttempted ? deskweather::resultText(weatherResult) : tr(STR_DESK_NO_DATA));
    renderer.drawCenteredText(UI_12_FONT_ID, top + height / 2 - 26, tr(STR_DESK_WEATHER), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height / 2 + 6, msg[0] ? msg : tr(STR_DESK_NO_DATA));
    return;
  }

  const bool fresh = desk::weatherFresh(weather, nowUnix);
  const int pad = 14;
  drawSkyIcon(left + pad, top + pad, 72, fresh ? weather.sky : desk::Sky::Unknown, weather.isDay);

  const bool f = SETTINGS.deskTempUnit == 1;
  snprintf(buf, sizeof(buf), "%d°", shownTemp(weather.tempC, f));
  const int tempX = left + pad + 84;
  renderer.drawText(DESK_DIGITS_FONT_ID, tempX, top + pad + 4, buf);
  // The digits font has no letters: the unit in the UI font beside it.
  const int unitX = tempX + renderer.getTextWidth(DESK_DIGITS_FONT_ID, buf) + 2;
  const char* unit = f ? "F" : "C";
  renderer.drawText(UI_12_FONT_ID, unitX, top + pad + 8, unit, true, EpdFontFamily::BOLD);
  const int textX = unitX + renderer.getTextWidth(UI_12_FONT_ID, unit, EpdFontFamily::BOLD) + 14;
  renderer.drawText(UI_12_FONT_ID, textX, top + pad + 6, skyText(weather.sky), true, EpdFontFamily::BOLD);
  snprintf(buf, sizeof(buf), tr(STR_DESK_LOW_HIGH), shownTemp(weather.tempMin, f), shownTemp(weather.tempMax, f));
  renderer.drawText(UI_10_FONT_ID, textX, top + pad + 42, buf);

  int y = top + pad + 86;
  char hint[64];
  rainHintText(weather, nowUnix, hint, sizeof(hint));
  if (weatherFetching) {
    snprintf(hint, sizeof(hint), "%s", tr(STR_DESK_UPDATING));
  } else if (!fresh) {
    snprintf(hint, sizeof(hint), "%s%s%s", tr(STR_DESK_STALE), weatherAttempted ? " · " : "",
             weatherAttempted ? deskweather::resultText(weatherResult) : "");
  }
  renderer.drawText(UI_10_FONT_ID, left + pad, y, hint, true, EpdFontFamily::BOLD);
  const int level = desk::aqiLevel(weather.aqi);
  if (SETTINGS.deskAirQuality && fresh && level >= 0) {
    static const StrId kLevels[6] = {StrId::STR_DESK_AQI_0, StrId::STR_DESK_AQI_1, StrId::STR_DESK_AQI_2,
                                     StrId::STR_DESK_AQI_3, StrId::STR_DESK_AQI_4, StrId::STR_DESK_AQI_5};
    char air[48];
    snprintf(air, sizeof(air), "AQI %d · %s", weather.aqi, I18N.get(kLevels[level]));
    const int airW = renderer.getTextWidth(UI_10_FONT_ID, air, EpdFontFamily::BOLD);
    const int airX = left + cardW - pad - airW;
    if (level >= 2) {
      // Unhealthy for some or worse: white on black so it is seen from across the room.
      renderer.fillRoundedRect(airX - 8, y - 2, airW + 16, 30, 6, Color::Black);
      renderer.drawText(UI_10_FONT_ID, airX, y, air, false, EpdFontFamily::BOLD);
    } else {
      renderer.drawText(UI_10_FONT_ID, airX, y, air, true, EpdFontFamily::BOLD);
    }
  }
  y += 34;

  renderer.drawLine(left + pad, y, left + cardW - pad, y, 1, true);
  y += 10;
  const int slotW = (cardW - 2 * pad) / 4;
  for (int i = 0; i < 4; ++i) {
    const int idx = i * 3;  // now, +3 h, +6 h, +9 h
    const int cx = left + pad + slotW * i + slotW / 2;
    snprintf(buf, sizeof(buf), tr(STR_DESK_HOUR), (weather.firstHour + idx) % 24);
    renderer.drawText(UI_10_FONT_ID, cx - renderer.getTextWidth(UI_10_FONT_ID, buf) / 2, y, buf);
    const char* label = skyText(weather.hourly[idx]);
    renderer.drawText(UI_10_FONT_ID, cx - renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD) / 2, y + 28,
                      label, true, EpdFontFamily::BOLD);
    snprintf(buf, sizeof(buf), "%d°", shownTemp(weather.hourlyTemp[idx], f));
    renderer.drawText(UI_10_FONT_ID, cx - renderer.getTextWidth(UI_10_FONT_ID, buf) / 2, y + 56, buf);
  }
}

void DeskActivity::renderMain(const uint32_t nowUnix) {
  const int w = renderer.getScreenWidth();
  char buf[96];

  char middle[64] = "";
  if (weather.valid && config.place.valid) {
    // The time of the last good forecast, said as such: a bare "Hà Nội · 15:00" next to a
    // 17:00 clock read as a wrong clock (owner's report, 2026-11-23).
    char hm[8];
    char updated[32];
    formatLocalHm(weather.fetchedAt, hm, sizeof(hm));
    snprintf(updated, sizeof(updated), tr(STR_DESK_UPDATED), hm);
    snprintf(middle, sizeof(middle), "%s · %s", config.place.name, updated);
  }
  drawStatusBar("Desk", middle);

  int y = 62;
  if (!shown.valid) {
    renderer.drawCenteredText(DESK_CLOCK_FONT_ID, y, "--:--");
    y += renderer.getLineHeight(DESK_CLOCK_FONT_ID) + 10;
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_DESK_NO_CLOCK), true, EpdFontFamily::BOLD);
    y += 34;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_DESK_NO_CLOCK_HINT));
    y += 40;
  } else {
    snprintf(buf, sizeof(buf), "%02d:%02d", shown.hour, shown.minute);
    renderer.drawCenteredText(DESK_CLOCK_FONT_ID, y, buf);
    y += renderer.getLineHeight(DESK_CLOCK_FONT_ID) + 2;

    const int32_t jd = desk::julianDay(shown.day, shown.month, shown.year);
    snprintf(buf, sizeof(buf), "%s, %d/%d/%d", weekdayText(desk::weekdayOf(jd)), shown.day, shown.month,
             shown.year);
    renderer.drawCenteredText(UI_12_FONT_ID, y, buf, true, EpdFontFamily::BOLD);
    y += 38;

    const desk::LunarDate lunar = desk::solarToLunar(shown.day, shown.month, shown.year);
    char yearName[32];
    desk::canChiOfYear(lunar.year, yearName, sizeof(yearName));
    snprintf(buf, sizeof(buf), tr(STR_DESK_LUNAR_LINE), lunar.day, lunar.month, lunar.leap ? tr(STR_DESK_LEAP) : "",
             yearName);
    renderer.drawCenteredText(UI_12_FONT_ID, y, buf);
    y += 34;

    char dayName[32];
    char monthName[32];
    desk::canChiOfDay(jd, dayName, sizeof(dayName));
    desk::canChiOfMonth(lunar, monthName, sizeof(monthName));
    snprintf(buf, sizeof(buf), tr(STR_DESK_CANCHI_LINE), dayName, monthName);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    y += 38;

    const desk::CalendarEvent ev = desk::nextCalendarEvent(shown.day, shown.month, shown.year);
    if (ev.found) {
      char pill[32];
      if (ev.daysUntil == 0) {
        snprintf(pill, sizeof(pill), "%s", tr(STR_DESK_TODAY));
      } else if (ev.daysUntil == 1) {
        snprintf(pill, sizeof(pill), "%s", tr(STR_DESK_TOMORROW));
      } else {
        snprintf(pill, sizeof(pill), tr(STR_DESK_DAYS_LEFT), ev.daysUntil);
      }
      char evName[64];
      eventText(ev, evName, sizeof(evName));
      const int pillW = renderer.getTextWidth(UI_10_FONT_ID, pill, EpdFontFamily::BOLD) + 20;
      const int nameW = renderer.getTextWidth(UI_10_FONT_ID, evName);
      const int x0 = (w - (pillW + 10 + nameW)) / 2;
      renderer.fillRect(x0, y - 4, pillW, 30, true);
      renderer.drawText(UI_10_FONT_ID, x0 + 10, y, pill, false, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, x0 + pillW + 10, y, evName);
    }
    y += 44;
  }

  drawWeatherCard(y, 240, nowUnix);
  y += 240 + 14;

  // PC strip: three bars, shown while the agent's numbers are fresh.
  if (pcFresh(nowUnix)) {
    renderer.fillRect(kMargin, y, w - 2 * kMargin, 2, true);
    y += 10;
    struct Item {
      const char* label;
      int pct;
    };
    Item items[3] = {{"CPU", pc.cpuPct}, {"RAM", desk::percentOf(pc.ramUsedGb, pc.ramTotalGb)}, {"", 0}};
    if (pc.claude.known) {
      items[2] = {"Claude 5h", pc.claude.sessionPct};
    } else if (pc.diskCount > 0) {
      // A drive letter fits the column; a long mount path does not.
      static char diskLabel[16];
      if (strlen(pc.disks[0].name) <= 3) {
        snprintf(diskLabel, sizeof(diskLabel), tr(STR_DESK_DISK), pc.disks[0].name);
      } else {
        snprintf(diskLabel, sizeof(diskLabel), "%s", tr(STR_DESK_DISK_ANY));
      }
      items[2] = {diskLabel, desk::percentOf(pc.disks[0].usedGb, pc.disks[0].totalGb)};
    }
    const int colW = (w - 2 * kMargin) / 3;
    for (int i = 0; i < 3; ++i) {
      if (items[i].label[0] == '\0') continue;
      const int x = kMargin + i * colW;
      renderer.drawText(UI_10_FONT_ID, x, y, items[i].label);
      snprintf(buf, sizeof(buf), "%d%%", items[i].pct);
      renderer.drawText(UI_10_FONT_ID, x + colW - 14 - renderer.getTextWidth(UI_10_FONT_ID, buf, EpdFontFamily::BOLD),
                        y, buf, true, EpdFontFamily::BOLD);
      drawBar(x, y + 28, colW - 14, 12, items[i].pct);
    }
  }

  if (batteryMode) {
    // The front buttons cannot wake the light sleep: only the power button can.
    renderer.drawCenteredText(UI_10_FONT_ID,
                              renderer.getScreenHeight() - kBottomSafe - renderer.getLineHeight(UI_10_FONT_ID),
                              isNight(shown) ? tr(STR_DESK_NIGHT_SAVER) : tr(STR_DESK_BATTERY_SAVER));
    return;
  }
  drawPageHints(tr(STR_DESK_EXIT), tr(STR_DESK_REFRESH));
}

void DeskActivity::renderPc(const uint32_t nowUnix) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  char buf[96];
  char hm[8];
  drawStatusBar(tr(STR_DESK_PC), config.agent[0] != '\0' ? config.agent : tr(STR_DESK_PC_AUTO));

  if (!pc.valid || !pcFresh(nowUnix)) {
    int y = h / 2 - 90;
    renderer.drawCenteredText(UI_12_FONT_ID, y, pcPolling ? tr(STR_DESK_PC_SEARCHING) : tr(STR_DESK_PC_NOT_FOUND), true,
                              EpdFontFamily::BOLD);
    y += 44;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_DESK_PC_HINT1));
    y += 30;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_DESK_PC_HINT2));
    y += 44;
    if (pcAttempted && pcResult != deskweather::Result::Ok) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, deskweather::resultText(pcResult));
      y += 30;
    }
    if (pc.valid) {
      formatLocalHm(pc.receivedAt, hm, sizeof(hm));
      snprintf(buf, sizeof(buf), tr(STR_DESK_LAST_SEEN), hm);
      renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
      y += 30;
    }
    y += 14;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_DESK_PC_ADDRESS_HINT), true, EpdFontFamily::BOLD);
  } else {
    int y = 58;
    renderer.drawText(UI_12_FONT_ID, kMargin, y, pc.host, true, EpdFontFamily::BOLD);
    formatLocalHm(pc.receivedAt, hm, sizeof(hm));
    snprintf(buf, sizeof(buf), tr(STR_DESK_UPDATED), hm);
    renderer.drawText(UI_10_FONT_ID, w - kMargin - renderer.getTextWidth(UI_10_FONT_ID, buf), y + 6, buf);

    // CPU and RAM: two equal three-quarter rings, the percent in the middle, the name in the gap.
    const int r = 96;
    const int cy = 200;  // the rings' centre
    const int ramPct = desk::percentOf(pc.ramUsedGb, pc.ramTotalGb);
    char ramText[32];
    desk::formatSize(pc.ramUsedGb, pc.ramTotalGb, ramText, sizeof(ramText), decimalMark());
    const struct {
      const char* name;
      int pct;
      const char* detail;
    } dials[2] = {{"CPU", pc.cpuPct, ""}, {"RAM", ramPct, ramText}};
    const int digitsH = renderer.getLineHeight(DESK_DIGITS_FONT_ID);
    for (int i = 0; i < 2; ++i) {
      const int cx = w / 4 + i * w / 2;
      drawGauge(cx, cy, r, dials[i].pct, 24);
      snprintf(buf, sizeof(buf), "%d%%", dials[i].pct);
      renderer.drawText(DESK_DIGITS_FONT_ID, cx - renderer.getTextWidth(DESK_DIGITS_FONT_ID, buf) / 2,
                        cy - digitsH / 2 - 4, buf);
      renderer.drawText(UI_10_FONT_ID, cx - renderer.getTextWidth(UI_10_FONT_ID, dials[i].name, EpdFontFamily::BOLD) / 2,
                        cy + 58, dials[i].name, true, EpdFontFamily::BOLD);
      if (dials[i].detail[0] != '\0') {
        renderer.drawText(UI_10_FONT_ID, cx - renderer.getTextWidth(UI_10_FONT_ID, dials[i].detail) / 2, cy + 86,
                          dials[i].detail);
      }
    }
    y = cy + 122;
    renderer.drawLine(kMargin, y, w - kMargin, y, 1, true);
    y += 12;

    // Disks: thin bars (up to 3; 2 when the Claude card needs the room).
    const int maxDisks = pc.claude.known ? 2 : 3;
    for (int i = 0; i < pc.diskCount && i < maxDisks; ++i) {
      char label[24];
      snprintf(label, sizeof(label), tr(STR_DESK_DISK), pc.disks[i].name);
      desk::formatSize(pc.disks[i].usedGb, pc.disks[i].totalGb, buf, sizeof(buf), decimalMark());
      renderer.drawText(UI_10_FONT_ID, kMargin, y, label);
      renderer.drawText(UI_10_FONT_ID, w - kMargin - renderer.getTextWidth(UI_10_FONT_ID, buf, EpdFontFamily::BOLD), y,
                        buf, true, EpdFontFamily::BOLD);
      drawBar(kMargin, y + 28, w - 2 * kMargin, 14, desk::percentOf(pc.disks[i].usedGb, pc.disks[i].totalGb));
      y += 54;
    }

    // Network and uptime on one line.
    char down[24];
    char up[24];
    char uptime[32];
    char upFor[48];
    desk::formatRate(pc.netDownBps, down, sizeof(down), decimalMark());
    desk::formatRate(pc.netUpBps, up, sizeof(up), decimalMark());
    uptimeText(pc.uptimeSec, uptime, sizeof(uptime));
    snprintf(upFor, sizeof(upFor), tr(STR_DESK_UP_FOR), uptime);
    snprintf(buf, sizeof(buf), "↓ %s  ↑ %s  ·  %s", down, up, upFor);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    y += 40;

    // Claude: two smaller three-quarter rings (5-hour session, week) with their reset times.
    const int cardW = w - 2 * kMargin;
    if (!pc.claude.known) {
      renderer.drawRoundedRect(kMargin, y, cardW, 64, 2, 10, true);
      renderer.drawText(UI_12_FONT_ID, kMargin + 14, y + 14, "Claude", true, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, kMargin + 120, y + 20, tr(STR_DESK_CLAUDE_NO_SOURCE));
    } else {
      const int qr = 58;
      const int cardH = 236;
      renderer.drawRoundedRect(kMargin, y, cardW, cardH, 2, 10, true);
      renderer.drawText(UI_12_FONT_ID, kMargin + 14, y + 10, "Claude", true, EpdFontFamily::BOLD);
      const struct {
        const char* label;
        int pct;
        uint32_t reset;
      } q[2] = {{tr(STR_DESK_CLAUDE_SESSION), pc.claude.sessionPct, pc.claude.sessionReset},
                {tr(STR_DESK_CLAUDE_WEEK), pc.claude.weekPct, pc.claude.weekReset}};
      const int qcy = y + 110;
      for (int i = 0; i < 2; ++i) {
        const int cx = kMargin + cardW / 4 + i * cardW / 2;
        drawGauge(cx, qcy, qr, q[i].pct, 16);
        snprintf(buf, sizeof(buf), "%d%%", q[i].pct);
        renderer.drawText(UI_12_FONT_ID, cx - renderer.getTextWidth(UI_12_FONT_ID, buf, EpdFontFamily::BOLD) / 2,
                          qcy - 16, buf, true, EpdFontFamily::BOLD);
        renderer.drawText(UI_10_FONT_ID, cx - renderer.getTextWidth(UI_10_FONT_ID, q[i].label, EpdFontFamily::BOLD) / 2,
                          qcy + 58, q[i].label, true, EpdFontFamily::BOLD);
        if (q[i].reset != 0) {
          formatLocalHm(q[i].reset, hm, sizeof(hm));
          char when[24];
          if (q[i].reset > nowUnix + 20 * 3600) {
            // The weekly reset is days away: its weekday too ("Thu 14:00").
            const int64_t localSec = static_cast<int64_t>(q[i].reset) + utcOffsetMinutes() * 60;
            const int wd = desk::weekdayOf(static_cast<int32_t>(2440588 + localSec / 86400));  // JDN of 1970-01-01
            snprintf(when, sizeof(when), "%s %s", weekdayShortText(wd), hm);
          } else {
            snprintf(when, sizeof(when), "%s", hm);
          }
          snprintf(buf, sizeof(buf), tr(STR_DESK_RESETS_AT), when);
          renderer.drawText(UI_10_FONT_ID, cx - renderer.getTextWidth(UI_10_FONT_ID, buf) / 2, qcy + 84, buf);
        }
      }
    }
  }

  drawPageHints(tr(STR_DESK_BACK), tr(STR_DESK_REFRESH));
}

void DeskActivity::renderPhoto(const uint32_t nowUnix) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  char line1[64];
  char line2[96];

  if (pcPhotos() && (photos.empty() || pcPhotoLoaded != photoIndex)) {
    // From the PC: none yet, still downloading, or the download failed.
    renderer.clearScreen();
    drawStatusBar(tr(STR_DESK_PHOTOS), "");
    const char* head = photos.empty() ? (pcAttempted && pcResult != deskweather::Result::Ok
                                             ? deskweather::resultText(pcResult)
                                             : (pc.valid ? tr(STR_DESK_PC_NO_PHOTOS) : tr(STR_DESK_PC_SEARCHING)))
                                      : (pcPhotoFailed ? tr(STR_DESK_PHOTO_ERROR) : tr(STR_DESK_PC_PHOTO_LOADING));
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 40, head, true, EpdFontFamily::BOLD);
    if (photos.empty()) renderer.drawCenteredText(UI_10_FONT_ID, h / 2 + 4, tr(STR_DESK_PC_PHOTOS_HINT));
    drawPageHints(tr(STR_DESK_BACK), "");
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  if (photos.empty()) {
    renderer.clearScreen();
    drawStatusBar(tr(STR_DESK_PHOTOS), "");
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 60, tr(STR_DESK_NO_PHOTOS), true, EpdFontFamily::BOLD);
    const std::string folder = photoFolder();
    if (!folder.empty()) {
      snprintf(line1, sizeof(line1), tr(STR_DESK_PHOTOS_FOLDER), folder.c_str());
      renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 16, line1);
    }
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 + 14, tr(STR_DESK_PHOTOS_HINT), true, EpdFontFamily::BOLD);
    drawPageHints(tr(STR_DESK_BACK), "");
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  // The info strip, centred: the clock, then date, lunar date and temperature. The panel has no
  // partial refresh, so the clock is the time the photo was drawn: it moves on with each photo
  // (owner's choice, 2026-11-23: no whole-photo redraw every minute).
  const LocalTime t = readLocalTime();
  char clock[8] = "";
  line1[0] = '\0';
  if (t.valid) {
    snprintf(clock, sizeof(clock), "%02d:%02d", t.hour, t.minute);
    const int32_t jd = desk::julianDay(t.day, t.month, t.year);
    const int wd = desk::weekdayOf(jd);
    const desk::LunarDate lunar = desk::solarToLunar(t.day, t.month, t.year);
    snprintf(line1, sizeof(line1), tr(STR_DESK_DATE_LUNAR_SHORT), weekdayShortText(wd), t.day, t.month, lunar.day,
             lunar.month);
  }
  line2[0] = '\0';
  if (weather.valid && desk::weatherFresh(weather, nowUnix)) {
    snprintf(line2, sizeof(line2), line1[0] != '\0' ? "%s · %d°%s" : "%s%d°%s", line1,
             shownTemp(weather.tempC, SETTINGS.deskTempUnit == 1), SETTINGS.deskTempUnit == 1 ? "F" : "C");
  } else {
    snprintf(line2, sizeof(line2), "%s", line1);
  }
  // The photo is fitted above the strip (photo::render's reserveBottom), so the strip sits on
  // blank paper. The B/W pass draws it and keeps a 1-bit copy; the gray passes need it again on
  // panels with absolute gray planes (the X3: the planes alone make the final picture, so text
  // drawn on the B/W pass only came out as a blank white strip - owner's report, 2026-11-23).
  // On non-absolute planes (cleared to "no gray") the strip needs nothing there.
  const int stripX = kMargin;
  const int stripY = h - kPhotoStripH;
  const int stripW = w - 2 * kMargin;
  const int stripRowBytes = (stripW + 7) / 8;
  std::vector<uint8_t> stripMask;
  const auto overlay = [&](const bool gray) {
    if (!gray) {
      renderer.drawLine(stripX, stripY + 4, stripX + stripW, stripY + 4, 2, true);
      // Laid out from the bottom, clear of the bezel (text ran off the bottom on the X3).
      int y = h - kBottomSafe - renderer.getLineHeight(UI_10_FONT_ID);
      renderer.drawCenteredText(UI_10_FONT_ID, y, line2);
      if (clock[0] != '\0') {
        y -= renderer.getLineHeight(DESK_DIGITS_FONT_ID);
        renderer.drawCenteredText(DESK_DIGITS_FONT_ID, y, clock);
      }
      stripMask.assign(static_cast<size_t>(stripRowBytes) * kPhotoStripH, 0);
      for (int y = 0; y < kPhotoStripH; ++y) {
        for (int x = 0; x < stripW; ++x) {
          if (renderer.isPixelBlack(stripX + x, stripY + y)) {
            stripMask[y * stripRowBytes + x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
          }
        }
      }
      return;
    }
    if (!renderer.grayPlanesAreAbsolute() || stripMask.empty()) return;
    for (int y = 0; y < kPhotoStripH; ++y) {
      for (int x = 0; x < stripW; ++x) {
        if (stripMask[y * stripRowBytes + x / 8] & (0x80 >> (x % 8))) renderer.drawPixel(stripX + x, stripY + y, true);
      }
    }
  };

  const std::string path = pcPhotos() ? std::string(kPcPhotoPath) : photoFolder() + "/" + photos[photoIndex];
  const bool crop = SETTINGS.slideshowScaleMode == CrossPointSettings::SLIDESHOW_CROP;
  if (photo::render(renderer, path, crop, overlay, kPhotoStripH + kPhotoStripGap) != photo::Result::Ok) {
    renderer.clearScreen();
    drawStatusBar(tr(STR_DESK_PHOTOS), "");
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 30, tr(STR_DESK_PHOTO_ERROR), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 + 10, photos[photoIndex].c_str());
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void DeskActivity::renderPomodoro() {
  const int w = renderer.getScreenWidth();
  const uint32_t t = nowSec();
  char buf[96];
  drawStatusBar("Pomodoro", "");

  const bool idle = pomodoro.phase() == desk::Pomodoro::Phase::Idle;
  int y = 66;
  renderer.drawCenteredText(UI_12_FONT_ID, y, phaseText(pomodoro.phase()), true, EpdFontFamily::BOLD);

  // A ring of one dot per minute of the phase; filled dots are the minutes left.
  const uint32_t minutes = pomodoro.remainingMinutes(t);
  shownPomodoroMinutes = minutes;
  const int total = static_cast<int>(pomodoro.phaseLengthSec() / 60);
  const int cx = w / 2;
  const int cy = 300;
  const int radius = 160;
  for (int i = 0; i < total; ++i) {
    const double a = -M_PI / 2 + 2 * M_PI * i / total;
    const int dx = cx + static_cast<int>(std::lround(radius * std::cos(a)));
    const int dy = cy + static_cast<int>(std::lround(radius * std::sin(a)));
    const int r = 9;
    if (i < static_cast<int>(minutes)) {
      renderer.fillRoundedRect(dx - r, dy - r, 2 * r, 2 * r, r, Color::Black);
    } else {
      renderer.drawRoundedRect(dx - r, dy - r, 2 * r, 2 * r, 2, r, true);
    }
  }
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(minutes));
  renderer.drawCenteredText(DESK_CLOCK_FONT_ID, cy - renderer.getLineHeight(DESK_CLOCK_FONT_ID) / 2 - 14, buf);
  renderer.drawCenteredText(UI_10_FONT_ID, cy + 58, pomodoro.paused() ? tr(STR_DESK_PAUSED) : tr(STR_DESK_MIN_LEFT));

  y = cy + radius + 40;
  // Round dots: done rounds filled.
  snprintf(buf, sizeof(buf), tr(STR_DESK_ROUND), pomodoro.round(), pomodoro.rounds());
  const int labelW = renderer.getTextWidth(UI_12_FONT_ID, buf);
  const int dotsW = pomodoro.rounds() * 24;
  int x = (w - (labelW + 16 + dotsW)) / 2;
  renderer.drawText(UI_12_FONT_ID, x, y, buf);
  x += labelW + 16;
  for (int i = 0; i < pomodoro.rounds(); ++i) {
    const bool done = i < pomodoro.round() - 1 || (i == pomodoro.round() - 1 &&
                                                   pomodoro.phase() != desk::Pomodoro::Phase::Focus && !idle);
    if (done) {
      renderer.fillRoundedRect(x + i * 24, y + 6, 16, 16, 8, Color::Black);
    } else {
      renderer.drawRoundedRect(x + i * 24, y + 6, 16, 16, 2, 8, true);
    }
  }
  y += 44;

  if (idle) {
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_DESK_POMO_HINT));
  } else if (shown.valid && !pomodoro.paused()) {
    const int endMin = (shown.hour * 60 + shown.minute + static_cast<int>(minutes)) % 1440;
    snprintf(buf, sizeof(buf), tr(STR_DESK_ENDS_AT), endMin / 60, endMin % 60);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
  }
  y += 40;
  renderer.drawLine(kMargin, y, w - kMargin, y, 1, true);
  y += 12;
  snprintf(buf, sizeof(buf), tr(STR_DESK_POMO_TODAY), pomodoro.focusRoundsToday(),
           static_cast<unsigned long>(pomodoro.focusMinutesToday()));
  renderer.drawCenteredText(UI_10_FONT_ID, y, buf);

  const char* selectLabel = idle ? tr(STR_DESK_START) : (pomodoro.paused() ? tr(STR_DESK_RESUME) : tr(STR_DESK_PAUSE));
  if (idle) {
    drawPageHints(tr(STR_DESK_BACK), selectLabel);
  } else {
    drawHints(tr(STR_DESK_BACK), selectLabel, tr(STR_DESK_SKIP), tr(STR_DESK_RESET));
  }
}

// Month calendar (lịch tháng): Monday-first grid, each day with its lunar day ("1/11" on a lunar
// month's first day; Mùng 1 and Rằm in bold), today inverted; under it today's lunar date, can
// chi and giờ hoàng đạo. Front buttons: previous / next month; Select: back to this month; side
// keys: other pages.
void DeskActivity::renderCalendar() {
  const int w = renderer.getScreenWidth();
  char buf[128];
  const LocalTime t = shown.valid ? shown : readLocalTime();
  const int baseYear = t.valid ? t.year : 2026;
  const int baseMonth = t.valid ? t.month : 1;
  const int index = baseYear * 12 + (baseMonth - 1) + calendarOffset;
  const int year = index / 12;
  const int month = index % 12 + 1;
  snprintf(buf, sizeof(buf), tr(STR_DESK_MONTH_YEAR), month, year);
  drawStatusBar(tr(STR_DESK_CALENDAR), buf);

  const int32_t jd1 = desk::julianDay(1, month, year);
  const int days = desk::julianDay(1, month == 12 ? 1 : month + 1, month == 12 ? year + 1 : year) - jd1;
  const int offset = (desk::weekdayOf(jd1) + 6) % 7;  // Monday first
  const int rows = (offset + days + 6) / 7;
  const int gridX = 12;
  const int cellW = (w - 2 * gridX) / 7;
  const int cellH = rows == 6 ? 62 : 70;
  int y = 58;
  for (int c = 0; c < 7; ++c) {
    const char* name = weekdayShortText((c + 1) % 7);
    renderer.drawText(UI_10_FONT_ID,
                      gridX + c * cellW + (cellW - renderer.getTextWidth(UI_10_FONT_ID, name, EpdFontFamily::BOLD)) / 2,
                      y, name, true, EpdFontFamily::BOLD);
  }
  y += 30;
  renderer.drawLine(gridX, y, w - gridX, y, 2, true);
  y += 4;
  for (int d = 1; d <= days; ++d) {
    const int cell = offset + d - 1;
    const int x0 = gridX + (cell % 7) * cellW;
    const int y0 = y + (cell / 7) * cellH;
    const desk::LunarDate ld = desk::solarToLunar(d, month, year);
    const bool today = t.valid && t.year == year && t.month == month && t.day == d;
    if (today) renderer.fillRoundedRect(x0 + 3, y0 + 2, cellW - 6, cellH - 6, 8, Color::Black);
    snprintf(buf, sizeof(buf), "%d", d);
    renderer.drawText(UI_12_FONT_ID, x0 + (cellW - renderer.getTextWidth(UI_12_FONT_ID, buf, EpdFontFamily::BOLD)) / 2,
                      y0 + 6, buf, !today, EpdFontFamily::BOLD);
    if (ld.day == 1) {
      snprintf(buf, sizeof(buf), "%d/%d", ld.day, ld.month);
    } else {
      snprintf(buf, sizeof(buf), "%d", ld.day);
    }
    const auto style = (ld.day == 1 || ld.day == 15) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    renderer.drawText(UI_10_FONT_ID, x0 + (cellW - renderer.getTextWidth(UI_10_FONT_ID, buf, style)) / 2, y0 + 34,
                      buf, !today, style);
  }
  for (int r = 1; r < rows; ++r) renderer.drawLine(gridX, y + r * cellH - 2, w - gridX, y + r * cellH - 2, 1, true);
  y += rows * cellH + 6;
  renderer.drawLine(gridX, y, w - gridX, y, 2, true);
  y += 12;

  if (calendarOffset == 0 && t.valid) {
    const int32_t jd = desk::julianDay(t.day, t.month, t.year);
    const desk::LunarDate lunar = desk::solarToLunar(t.day, t.month, t.year);
    snprintf(buf, sizeof(buf), tr(STR_DESK_DATE_LUNAR_SHORT), weekdayText(desk::weekdayOf(jd)), t.day, t.month,
             lunar.day, lunar.month);
    renderer.drawCenteredText(UI_12_FONT_ID, y, buf, true, EpdFontFamily::BOLD);
    y += 36;
    char dayName[32];
    char monthName[32];
    char yearName[32];
    desk::canChiOfDay(jd, dayName, sizeof(dayName));
    desk::canChiOfMonth(lunar, monthName, sizeof(monthName));
    desk::canChiOfYear(lunar.year, yearName, sizeof(yearName));
    snprintf(buf, sizeof(buf), tr(STR_DESK_CANCHI_FULL), dayName, monthName, yearName);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    y += 34;
    // Giờ hoàng đạo, three a line: "Dần 3-5 · Mão 5-7 · Tỵ 9-11".
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_DESK_GOOD_HOURS), true, EpdFontFamily::BOLD);
    y += 28;
    const uint16_t bits = desk::auspiciousHours(jd);
    char line[96] = "";
    int inLine = 0;
    for (int i = 0; i < 12; ++i) {
      if (!(bits & (1u << i))) continue;
      char item[32];
      snprintf(item, sizeof(item), "%s%s %d-%d", inLine ? " · " : "", desk::branchName(i), (i * 2 + 23) % 24,
               (i * 2 + 1) % 24);
      strncat(line, item, sizeof(line) - strlen(line) - 1);
      if (++inLine == 3) {
        renderer.drawCenteredText(UI_10_FONT_ID, y, line);
        y += 28;
        line[0] = '\0';
        inLine = 0;
      }
    }
    if (inLine) renderer.drawCenteredText(UI_10_FONT_ID, y, line);
  } else {
    const desk::LunarDate first = desk::solarToLunar(1, month, year);
    const desk::LunarDate last = desk::solarToLunar(days, month, year);
    char yearName[32];
    desk::canChiOfYear(last.year, yearName, sizeof(yearName));
    snprintf(buf, sizeof(buf), tr(STR_DESK_CAL_LUNAR_SPAN), first.day, first.month, last.day, last.month, yearName);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    y += 34;
  }
  if (mappedInput.hasTouch()) {
    renderer.drawCenteredText(UI_10_FONT_ID,
                              renderer.getScreenHeight() - kTouchBarBottom - kTouchBarH - 8 -
                                  renderer.getLineHeight(UI_10_FONT_ID),
                              tr(STR_DESK_CAL_HINT_TOUCH));
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() - 40 - 34, tr(STR_DESK_CAL_HINT));
  }
  drawHints(tr(STR_DESK_BACK), tr(STR_DESK_TODAY), tr(STR_DESK_PREV_MONTH), tr(STR_DESK_NEXT_MONTH));
}

void DeskActivity::render(RenderLock&&) { drawPage(); }

void DeskActivity::drawPage() {
  uint32_t nowUnix = 0;
  readUtcUnix(nowUnix);
  touchBarMask = 0;  // drawHints sets the buttons this screen shows
  if (page == Page::Photo) {
    forceHalfRefresh = false;
    minutesSinceFull = 0;
    renderPhoto(nowUnix);  // pushes to the panel itself
    return;
  }
  renderer.clearScreen();
  if (page == Page::Pc) {
    renderPc(nowUnix);
  } else if (page == Page::Calendar) {
    renderCalendar();
  } else if (page == Page::Pomodoro) {
    renderPomodoro();
  } else {
    renderMain(nowUnix);
  }
  const bool half = forceHalfRefresh;
  forceHalfRefresh = false;
  if (half) minutesSinceFull = 0;
  renderer.displayBuffer(half ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

#endif  // CROSSINK_DESK
