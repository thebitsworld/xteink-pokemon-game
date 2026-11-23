#include "DeskActivity.h"

#if CROSSINK_DESK

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

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
#include "activities/util/PhotoRender.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr size_t kMaxPhotos = 500;

constexpr uint32_t kPollMs = 1000;
constexpr int kHalfRefreshEveryMinutes = 60;          // USB default of "Full refresh" (plan)
constexpr uint32_t kWeatherEveryMs = 10 * 60 * 1000;  // USB default of "Weather"
constexpr uint32_t kWeatherRetryMs = 2 * 60 * 1000;   // after a failed attempt
constexpr uint32_t kPcEveryMs = 60 * 1000;            // USB default of "PC + Claude"
constexpr uint32_t kPcRetryMs = 2 * 60 * 1000;
constexpr uint32_t kPcFreshSec = 5 * 60;  // older PC numbers are hidden from the main screen
constexpr int kMargin = 16;
constexpr uint32_t kBatteryIdleMs = 30 * 1000;       // no input this long on battery: battery loop
constexpr uint32_t kBatteryWeatherSec = 60 * 60;     // battery default of "Weather"
constexpr int kBatteryHalfRefreshEvery = 180;        // battery default of "Full refresh": 3 h
constexpr uint32_t kBatteryWatchdogMs = 60 * 1000;   // > a 10 s sleep chunk and a weather fetch

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

void DeskActivity::loadPhotos() {
  photos.clear();
  photoIndex = 0;
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
  Activity::onExit();
  open = false;
  batteryNowRequested = false;
  if (wifiKeptForUsb) deskweather::wifiDown();
  wifiKeptForUsb = false;
}

bool DeskActivity::preventAutoSleep() {
  return deskpower::externalPower() || weatherFetching || pcPolling || pomodoro.running();
}

void DeskActivity::runWeatherFetch() {
  weatherFetchPending = false;
  weatherFetching = true;
  if (page == Page::Main) requestUpdateAndWait();  // shows "Đang cập nhật..." before blocking
  uint32_t now = 0;
  if (!readUtcUnix(now)) now = weather.fetchedAt;
  const bool usb = deskpower::externalPower();
  weatherResult = deskweather::refresh(config, weather, now, usb);
  wifiKeptForUsb = wifiKeptForUsb || usb;
  weatherFetching = false;
  weatherAttempted = true;
  lastWeatherAttemptMs = millis();
  if (page == Page::Main) forceHalfRefresh = true;
  requestUpdate();
}

void DeskActivity::runPcPoll() {
  pcPollPending = false;
  pcPolling = true;
  uint32_t now = 0;
  readUtcUnix(now);
  const bool usb = deskpower::externalPower();
  pcResult = deskagent::poll(config, pc, now, usb);
  wifiKeptForUsb = wifiKeptForUsb || usb;
  pcPolling = false;
  pcAttempted = true;
  lastPcAttemptMs = millis();
  requestUpdate();
}

bool DeskActivity::pcFresh(const uint32_t nowUnix) const {
  return pc.valid && nowUnix >= pc.receivedAt && nowUnix - pc.receivedAt <= kPcFreshSec;
}

// ---- battery loop (phase 5) ------------------------------------------------------------------

void DeskActivity::resumeBatteryFromBoot() {
  loadState();
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

void DeskActivity::leaveBatteryLoop() {
  batteryLogNow(deskpower::externalPower() ? "exit-usb" : "exit-button");
  // The press that woke the sleep is still held: restarting now would hand its release to the
  // new boot as a short press, i.e. a sleep (owner's report, 2026-11-23).
  deskpower::waitPowerButtonRelease();
  deskpower::watchdogDisarm();
  deskpower::setBatteryRunArmed(false);
  deskpower::requestOpenDeskAfterRestart();
  ESP.restart();
  for (;;) {
    delay(1000);
  }
}

void DeskActivity::runBatteryLoop(const bool fromBoot) {
  page = Page::Main;
  batteryMode = true;
  forceHalfRefresh = true;
  if (!fromBoot) {
    // Let a render already queued finish before the loop draws without the render task.
    requestUpdateAndWait();
  }
  if (wifiKeptForUsb || WiFi.getMode() != WIFI_MODE_NULL) deskweather::wifiDown();
  wifiKeptForUsb = false;
#ifndef SIMULATOR
  gpio.stopButtonCapture();
#endif
  deskpower::setBatteryRunArmed(true);
  deskpower::watchdogArm(kBatteryWatchdogMs);
  batteryLogNow(fromBoot ? "resume" : "enter");

  int ticksSinceHalf = 0;
  for (;;) {
    deskpower::watchdogFeed();
    if (deskpower::externalPower()) leaveBatteryLoop();  // charging: the full Desk again
    batteryLogNow("tick");

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
    if (readUtcUnix(now) && (!weather.valid || now < weather.fetchedAt ||
                             now - weather.fetchedAt >= kBatteryWeatherSec)) {
      weatherResult = deskweather::refresh(config, weather, now, false);
      weatherAttempted = true;
      deskpower::watchdogFeed();
    }

    drawPage();
    if (deskpower::sleepFor(deskpower::msToNextMinute()) == deskpower::Wake::PowerButton) leaveBatteryLoop();
  }
}

void DeskActivity::loop() {
  // On the X3 the two right-hand front buttons report Left/Right; Up/Down are the side keys.
  const bool up = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                  mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool down = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                    mappedInput.wasPressed(MappedInputManager::Button::Right);
  const bool select = mappedInput.wasPressed(MappedInputManager::Button::Confirm) && millis() - enteredMs > 400;
  if (up || down || select || mappedInput.wasPressed(MappedInputManager::Button::Back)) lastInputMs = millis();

  // A sleep asked for (power button, idle timeout) with "Sleep opens Desk" on: battery loop now.
  if (batteryNowRequested) {
    batteryNowRequested = false;
    if (!deskpower::externalPower()) runBatteryLoop(false);
  }
  // On battery and left alone: the minute-tick loop in light sleep (does not return).
  if (!deskpower::externalPower() && !pomodoro.running() && !weatherFetchPending && !pcPollPending &&
      millis() - lastInputMs > kBatteryIdleMs && page != Page::Photo) {
    runBatteryLoop(false);
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (page != Page::Main) {
      switchPage(Page::Main);
      return;
    }
    finish();
    return;
  }
  // On the Pomodoro page with a timer set, Up skips the phase and Down resets; elsewhere (and
  // with the timer idle) Up/Down switch pages.
  if (page == Page::Pomodoro && pomodoro.phase() != desk::Pomodoro::Phase::Idle && (up || down)) {
    if (up) {
      pomodoro.skip(nowSec());
    } else {
      pomodoro.reset();
    }
    requestUpdate();
    return;
  }
  if (up || down) {
    const int n = static_cast<int>(Page::Count);
    const int step = down ? 1 : n - 1;
    switchPage(static_cast<Page>((static_cast<int>(page) + step) % n));
    return;
  }
  if (select) {
    switch (page) {
      case Page::Main:
        shown = readLocalTime();
        runWeatherFetch();
        if (weatherResult == deskweather::Result::NoSavedWifi || weatherResult == deskweather::Result::WifiFailed) {
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
          return;
        }
        runPcPoll();
        break;
      case Page::Pc:
        runPcPoll();
        break;
      case Page::Photo:
        if (!photos.empty()) photoIndex = (photoIndex + 1) % static_cast<int>(photos.size());
        lastPhotoChangeMs = millis();
        requestUpdate();
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
  if (pomodoro.update(nowSec()) == desk::Pomodoro::Event::PhaseEnded) {
    switchPage(Page::Pomodoro);
    return;
  }
  if (page == Page::Pomodoro && pomodoro.remainingMinutes(nowSec()) != shownPomodoroMinutes) {
    requestUpdate();
  }

  const uint32_t nowMs = millis();
  const uint32_t photoEveryMs = static_cast<uint32_t>(SETTINGS.slideshowIntervalSeconds) * 1000u;
  if (page == Page::Photo && deskpower::externalPower() && photos.size() > 1 && photoEveryMs > 0 &&
      nowMs - lastPhotoChangeMs >= photoEveryMs) {
    photoIndex = (photoIndex + 1) % static_cast<int>(photos.size());
    lastPhotoChangeMs = nowMs;
    requestUpdate();
    return;
  }
  if (weatherFetchPending) {
    runWeatherFetch();
    return;
  }
  if (pcPollPending) {
    runPcPoll();
    return;
  }
  if (deskpower::externalPower()) {
    const uint32_t weatherWait = weatherResult == deskweather::Result::Ok ? kWeatherEveryMs : kWeatherRetryMs;
    if (weatherAttempted && nowMs - lastWeatherAttemptMs >= weatherWait) {
      runWeatherFetch();
      return;
    }
    const uint32_t pcWait = pcResult == deskweather::Result::Ok ? kPcEveryMs : kPcRetryMs;
    if (pcAttempted && nowMs - lastPcAttemptMs >= pcWait) {
      runPcPoll();
      return;
    }
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

  snprintf(buf, sizeof(buf), "%d°", weather.tempC);
  const int tempX = left + pad + 84;
  renderer.drawText(DESK_DIGITS_FONT_ID, tempX, top + pad + 4, buf);
  const int textX = tempX + renderer.getTextWidth(DESK_DIGITS_FONT_ID, buf) + 16;
  renderer.drawText(UI_12_FONT_ID, textX, top + pad + 6, skyText(weather.sky), true, EpdFontFamily::BOLD);
  snprintf(buf, sizeof(buf), tr(STR_DESK_LOW_HIGH), weather.tempMin, weather.tempMax);
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
    snprintf(buf, sizeof(buf), "%d°", weather.hourlyTemp[idx]);
    renderer.drawText(UI_10_FONT_ID, cx - renderer.getTextWidth(UI_10_FONT_ID, buf) / 2, y + 56, buf);
  }
}

void DeskActivity::renderMain(const uint32_t nowUnix) {
  const int w = renderer.getScreenWidth();
  char buf[96];

  char middle[64] = "";
  if (weather.valid && config.place.valid) {
    char hm[8];
    formatLocalHm(weather.fetchedAt, hm, sizeof(hm));
    snprintf(middle, sizeof(middle), "%s · %s", config.place.name, hm);
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
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() - 40, tr(STR_DESK_BATTERY_SAVER));
    return;
  }
  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_DESK_EXIT)), tr(STR_DESK_REFRESH),
                                            tr(STR_DESK_PAGE), tr(STR_DESK_PAGE));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DeskActivity::renderPc(const uint32_t nowUnix) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  char buf[96];
  char hm[8];
  drawStatusBar(tr(STR_DESK_PC), "");

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
    }
  } else {
    int y = 62;
    renderer.drawText(UI_12_FONT_ID, kMargin, y, pc.host, true, EpdFontFamily::BOLD);
    formatLocalHm(pc.receivedAt, hm, sizeof(hm));
    snprintf(buf, sizeof(buf), tr(STR_DESK_UPDATED), hm);
    renderer.drawText(UI_10_FONT_ID, w - kMargin - renderer.getTextWidth(UI_10_FONT_ID, buf), y + 6, buf);
    y += 48;

    auto row = [&](const char* label, const char* value, const int pct) {
      renderer.drawText(UI_10_FONT_ID, kMargin, y, label);
      renderer.drawText(UI_10_FONT_ID, w - kMargin - renderer.getTextWidth(UI_10_FONT_ID, value, EpdFontFamily::BOLD),
                        y, value, true, EpdFontFamily::BOLD);
      drawBar(kMargin, y + 30, w - 2 * kMargin, 18, pct);
      y += 66;
    };
    snprintf(buf, sizeof(buf), "%d%%", pc.cpuPct);
    row("CPU", buf, pc.cpuPct);
    desk::formatSize(pc.ramUsedGb, pc.ramTotalGb, buf, sizeof(buf), decimalMark());
    row("RAM", buf, desk::percentOf(pc.ramUsedGb, pc.ramTotalGb));
    for (int i = 0; i < pc.diskCount && i < 3; ++i) {
      char label[24];
      snprintf(label, sizeof(label), tr(STR_DESK_DISK), pc.disks[i].name);
      desk::formatSize(pc.disks[i].usedGb, pc.disks[i].totalGb, buf, sizeof(buf), decimalMark());
      row(label, buf, desk::percentOf(pc.disks[i].usedGb, pc.disks[i].totalGb));
    }

    renderer.drawLine(kMargin, y, w - kMargin, y, 1, true);
    y += 12;
    char down[24];
    char up[24];
    char uptime[32];
    desk::formatRate(pc.netDownBps, down, sizeof(down), decimalMark());
    desk::formatRate(pc.netUpBps, up, sizeof(up), decimalMark());
    uptimeText(pc.uptimeSec, uptime, sizeof(uptime));
    snprintf(buf, sizeof(buf), tr(STR_DESK_NETWORK), down, up);
    renderer.drawText(UI_10_FONT_ID, kMargin, y, buf);
    y += 30;
    snprintf(buf, sizeof(buf), tr(STR_DESK_UP_FOR), uptime);
    renderer.drawText(UI_10_FONT_ID, kMargin, y, buf);
    y += 44;

    // Claude card.
    const int cardH = pc.claude.known ? 150 : 70;
    renderer.drawRoundedRect(kMargin, y, w - 2 * kMargin, cardH, 2, 10, true);
    renderer.drawText(UI_12_FONT_ID, kMargin + 14, y + 12, "Claude", true, EpdFontFamily::BOLD);
    if (!pc.claude.known) {
      renderer.drawText(UI_10_FONT_ID, kMargin + 120, y + 18, tr(STR_DESK_CLAUDE_NO_SOURCE));
    } else {
      const int colW = (w - 2 * kMargin - 28) / 2;
      const struct {
        const char* label;
        int pct;
        uint32_t reset;
      } q[2] = {{tr(STR_DESK_CLAUDE_SESSION), pc.claude.sessionPct, pc.claude.sessionReset},
                {tr(STR_DESK_CLAUDE_WEEK), pc.claude.weekPct, pc.claude.weekReset}};
      for (int i = 0; i < 2; ++i) {
        const int x = kMargin + 14 + i * colW;
        const int cy = y + 52;
        renderer.drawText(UI_10_FONT_ID, x, cy, q[i].label);
        snprintf(buf, sizeof(buf), "%d%%", q[i].pct);
        renderer.drawText(UI_12_FONT_ID, x, cy + 26, buf, true, EpdFontFamily::BOLD);
        drawBar(x, cy + 62, colW - 16, 12, q[i].pct);
        if (q[i].reset != 0) {
          formatLocalHm(q[i].reset, hm, sizeof(hm));
          snprintf(buf, sizeof(buf), tr(STR_DESK_RESETS_AT), hm);
          renderer.drawText(UI_10_FONT_ID, x + 70, cy + 30, buf);
        }
      }
    }
  }

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_DESK_BACK)), tr(STR_DESK_REFRESH),
                                            tr(STR_DESK_PAGE), tr(STR_DESK_PAGE));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DeskActivity::renderPhoto(const uint32_t nowUnix) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  char line1[64];
  char line2[64];

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
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_DESK_BACK)), "", tr(STR_DESK_PAGE),
                                                tr(STR_DESK_PAGE));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  // The info strip: date, lunar date, weather and the photo's number. No clock: a photo is
  // only redrawn when it changes.
  line1[0] = '\0';
  if (shown.valid) {
    const int32_t jd = desk::julianDay(shown.day, shown.month, shown.year);
    const int wd = desk::weekdayOf(jd);
    const desk::LunarDate lunar = desk::solarToLunar(shown.day, shown.month, shown.year);
    snprintf(line1, sizeof(line1), tr(STR_DESK_DATE_LUNAR_SHORT), weekdayShortText(wd), shown.day, shown.month,
             lunar.day, lunar.month);
  }
  if (weather.valid && desk::weatherFresh(weather, nowUnix)) {
    snprintf(line2, sizeof(line2), tr(STR_DESK_PHOTO_STRIP), weather.tempC, skyText(weather.sky), photoIndex + 1,
             static_cast<int>(photos.size()));
  } else {
    snprintf(line2, sizeof(line2), tr(STR_DESK_PHOTO_N), photoIndex + 1, static_cast<int>(photos.size()));
  }
  const int stripH = 76;
  const int stripX = 14;
  const int stripY = h - 14 - stripH;
  const int stripW = w - 28;
  const auto overlay = [&](const bool gray) {
    if (gray) {
      // No gray under the strip: the B/W pass draws it alone.
      renderer.fillRect(stripX, stripY, stripW, stripH, false);
      return;
    }
    renderer.fillRoundedRect(stripX, stripY, stripW, stripH, 10, Color::White);
    renderer.drawRoundedRect(stripX, stripY, stripW, stripH, 2, 10, true);
    renderer.drawText(UI_12_FONT_ID, stripX + 16, stripY + 8, line1, true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, stripX + 16, stripY + 44, line2);
  };

  const std::string path = photoFolder() + "/" + photos[photoIndex];
  const bool crop = SETTINGS.slideshowScaleMode == CrossPointSettings::SLIDESHOW_CROP;
  if (photo::render(renderer, path, crop, overlay) != photo::Result::Ok) {
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
  const auto labels = idle ? mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_DESK_BACK)), selectLabel,
                                                   tr(STR_DESK_PAGE), tr(STR_DESK_PAGE))
                           : mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_DESK_BACK)), selectLabel,
                                                   tr(STR_DESK_SKIP), tr(STR_DESK_RESET));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DeskActivity::render(RenderLock&&) { drawPage(); }

void DeskActivity::drawPage() {
  uint32_t nowUnix = 0;
  readUtcUnix(nowUnix);
  if (page == Page::Photo) {
    forceHalfRefresh = false;
    minutesSinceFull = 0;
    renderPhoto(nowUnix);  // pushes to the panel itself
    return;
  }
  renderer.clearScreen();
  if (page == Page::Pc) {
    renderPc(nowUnix);
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
