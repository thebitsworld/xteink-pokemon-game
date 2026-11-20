#include "DeskActivity.h"

#if CROSSINK_DESK

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>

#include <algorithm>
#include <cmath>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "DeskAgentService.h"
#include "DeskFonts.h"
#include "Lunar.h"
#include "MappedInputManager.h"
#include "activities/util/PhotoRender.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr char kPhotoDir[] = "/desk/photos";
constexpr uint32_t kPhotoEveryMs = 30 * 60 * 1000;  // USB default of "Photo: next image"
constexpr size_t kMaxPhotos = 500;

constexpr uint32_t kPollMs = 1000;
constexpr int kHalfRefreshEveryMinutes = 60;          // USB default of "Full refresh" (plan)
constexpr uint32_t kWeatherEveryMs = 10 * 60 * 1000;  // USB default of "Weather"
constexpr uint32_t kWeatherRetryMs = 2 * 60 * 1000;   // after a failed attempt
constexpr uint32_t kPcEveryMs = 60 * 1000;            // USB default of "PC + Claude"
constexpr uint32_t kPcRetryMs = 2 * 60 * 1000;
constexpr uint32_t kPcFreshSec = 5 * 60;  // older PC numbers are hidden from the main screen
constexpr int kMargin = 16;

}  // namespace

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

void DeskActivity::onEnter() {
  Activity::onEnter();
  page = Page::Main;
  shown = readLocalTime();
  forceHalfRefresh = true;
  minutesSinceFull = 0;
  lastPollMs = millis();
  enteredMs = lastPollMs;

  deskweather::loadConfig(config);
  deskweather::loadCache(weather);
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

void DeskActivity::loadPhotos() {
  photos.clear();
  auto dir = Storage.open(kPhotoDir);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  char name[256];
  for (auto file = dir.openNextFile(); file && photos.size() < kMaxPhotos; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      const std::string n(name);
      if (name[0] != '.' && (FsHelpers::hasBmpExtension(n) || FsHelpers::hasPngExtension(n))) photos.push_back(n);
    }
    file.close();
  }
  dir.close();
  std::sort(photos.begin(), photos.end());
  if (photoIndex >= static_cast<int>(photos.size())) photoIndex = 0;
}

void DeskActivity::switchPage(const Page to) {
  page = to;
  forceHalfRefresh = true;
  requestUpdate();
}

void DeskActivity::onExit() {
  Activity::onExit();
  if (wifiKeptForUsb) deskweather::wifiDown();
  wifiKeptForUsb = false;
}

bool DeskActivity::preventAutoSleep() {
  return gpio.isUsbConnected() || weatherFetching || pcPolling || pomodoro.running();
}

void DeskActivity::runWeatherFetch() {
  weatherFetchPending = false;
  weatherFetching = true;
  if (page == Page::Main) requestUpdateAndWait();  // shows "Đang cập nhật..." before blocking
  uint32_t now = 0;
  if (!readUtcUnix(now)) now = weather.fetchedAt;
  const bool usb = gpio.isUsbConnected();
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
  const bool usb = gpio.isUsbConnected();
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

void DeskActivity::loop() {
  const bool up = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool down = mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool select = mappedInput.wasPressed(MappedInputManager::Button::Confirm) && millis() - enteredMs > 400;

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
  if (page == Page::Photo && gpio.isUsbConnected() && photos.size() > 1 &&
      nowMs - lastPhotoChangeMs >= kPhotoEveryMs) {
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
  if (gpio.isUsbConnected()) {
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
  if (gpio.isUsbConnected()) {
    snprintf(buf, sizeof(buf), "Đang sạc");
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
    const char* msg = weatherFetching ? "Đang cập nhật..." : (weatherAttempted ? deskweather::resultText(weatherResult)
                                                                              : "Chưa có dữ liệu");
    renderer.drawCenteredText(UI_12_FONT_ID, top + height / 2 - 26, "Thời tiết", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height / 2 + 6, msg[0] ? msg : "Chưa có dữ liệu");
    return;
  }

  const bool fresh = desk::weatherFresh(weather, nowUnix);
  const int pad = 14;
  drawSkyIcon(left + pad, top + pad, 72, fresh ? weather.sky : desk::Sky::Unknown, weather.isDay);

  snprintf(buf, sizeof(buf), "%d°", weather.tempC);
  const int tempX = left + pad + 84;
  renderer.drawText(DESK_DIGITS_FONT_ID, tempX, top + pad + 4, buf);
  const int textX = tempX + renderer.getTextWidth(DESK_DIGITS_FONT_ID, buf) + 16;
  renderer.drawText(UI_12_FONT_ID, textX, top + pad + 6, desk::skyLabel(weather.sky), true, EpdFontFamily::BOLD);
  snprintf(buf, sizeof(buf), "Thấp %d° · Cao %d°", weather.tempMin, weather.tempMax);
  renderer.drawText(UI_10_FONT_ID, textX, top + pad + 42, buf);

  int y = top + pad + 86;
  char hint[64];
  desk::rainHint(weather, nowUnix, hint, sizeof(hint));
  if (weatherFetching) {
    snprintf(hint, sizeof(hint), "Đang cập nhật...");
  } else if (!fresh) {
    snprintf(hint, sizeof(hint), "Dữ liệu cũ%s%s", weatherAttempted ? " · " : "",
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
    snprintf(buf, sizeof(buf), "%dh", (weather.firstHour + idx) % 24);
    renderer.drawText(UI_10_FONT_ID, cx - renderer.getTextWidth(UI_10_FONT_ID, buf) / 2, y, buf);
    const char* label = desk::skyLabel(weather.hourly[idx]);
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
    renderer.drawCenteredText(UI_12_FONT_ID, y, "Chưa có giờ", true, EpdFontFamily::BOLD);
    y += 34;
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Settings > Clock > Sync now (cần Wi-Fi)");
    y += 40;
  } else {
    snprintf(buf, sizeof(buf), "%02d:%02d", shown.hour, shown.minute);
    renderer.drawCenteredText(DESK_CLOCK_FONT_ID, y, buf);
    y += renderer.getLineHeight(DESK_CLOCK_FONT_ID) + 2;

    const int32_t jd = desk::julianDay(shown.day, shown.month, shown.year);
    snprintf(buf, sizeof(buf), "%s, %d/%d/%d", desk::weekdayName(desk::weekdayOf(jd)), shown.day, shown.month,
             shown.year);
    renderer.drawCenteredText(UI_12_FONT_ID, y, buf, true, EpdFontFamily::BOLD);
    y += 38;

    const desk::LunarDate lunar = desk::solarToLunar(shown.day, shown.month, shown.year);
    char yearName[32];
    desk::canChiOfYear(lunar.year, yearName, sizeof(yearName));
    snprintf(buf, sizeof(buf), "Âm lịch %d/%d%s · năm %s", lunar.day, lunar.month, lunar.leap ? " nhuận" : "",
             yearName);
    renderer.drawCenteredText(UI_12_FONT_ID, y, buf);
    y += 34;

    char dayName[32];
    char monthName[32];
    desk::canChiOfDay(jd, dayName, sizeof(dayName));
    desk::canChiOfMonth(lunar, monthName, sizeof(monthName));
    snprintf(buf, sizeof(buf), "Ngày %s · tháng %s", dayName, monthName);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    y += 38;

    const desk::CalendarEvent ev = desk::nextCalendarEvent(shown.day, shown.month, shown.year);
    if (ev.found) {
      char pill[32];
      if (ev.daysUntil == 0) {
        snprintf(pill, sizeof(pill), "Hôm nay");
      } else {
        snprintf(pill, sizeof(pill), "Còn %d ngày", ev.daysUntil);
      }
      const int pillW = renderer.getTextWidth(UI_10_FONT_ID, pill, EpdFontFamily::BOLD) + 20;
      const int nameW = renderer.getTextWidth(UI_10_FONT_ID, ev.name);
      const int x0 = (w - (pillW + 10 + nameW)) / 2;
      renderer.fillRect(x0, y - 4, pillW, 30, true);
      renderer.drawText(UI_10_FONT_ID, x0 + 10, y, pill, false, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, x0 + pillW + 10, y, ev.name);
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
        snprintf(diskLabel, sizeof(diskLabel), "Ổ %s", pc.disks[0].name);
      } else {
        snprintf(diskLabel, sizeof(diskLabel), "Ổ đĩa");
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

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow("Thoát"), "Làm mới", "Trang", "Trang");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DeskActivity::renderPc(const uint32_t nowUnix) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  char buf[96];
  char hm[8];
  drawStatusBar("Máy tính", "");

  if (!pc.valid || !pcFresh(nowUnix)) {
    int y = h / 2 - 90;
    renderer.drawCenteredText(UI_12_FONT_ID, y, pcPolling ? "Đang tìm máy tính..." : "Không thấy máy tính", true,
                              EpdFontFamily::BOLD);
    y += 44;
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Agent chưa chạy hoặc PC đang tắt.");
    y += 30;
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Đồng hồ, lịch và thời tiết vẫn hoạt động.");
    y += 44;
    if (pcAttempted && pcResult != deskweather::Result::Ok) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, deskweather::resultText(pcResult));
      y += 30;
    }
    if (pc.valid) {
      formatLocalHm(pc.receivedAt, hm, sizeof(hm));
      snprintf(buf, sizeof(buf), "Lần cuối thấy lúc %s", hm);
      renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    }
  } else {
    int y = 62;
    renderer.drawText(UI_12_FONT_ID, kMargin, y, pc.host, true, EpdFontFamily::BOLD);
    formatLocalHm(pc.receivedAt, hm, sizeof(hm));
    snprintf(buf, sizeof(buf), "cập nhật %s", hm);
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
    desk::formatSize(pc.ramUsedGb, pc.ramTotalGb, buf, sizeof(buf));
    row("RAM", buf, desk::percentOf(pc.ramUsedGb, pc.ramTotalGb));
    for (int i = 0; i < pc.diskCount && i < 3; ++i) {
      char label[24];
      snprintf(label, sizeof(label), "Ổ %s", pc.disks[i].name);
      desk::formatSize(pc.disks[i].usedGb, pc.disks[i].totalGb, buf, sizeof(buf));
      row(label, buf, desk::percentOf(pc.disks[i].usedGb, pc.disks[i].totalGb));
    }

    renderer.drawLine(kMargin, y, w - kMargin, y, 1, true);
    y += 12;
    char down[24];
    char up[24];
    char uptime[32];
    desk::formatRate(pc.netDownBps, down, sizeof(down));
    desk::formatRate(pc.netUpBps, up, sizeof(up));
    desk::formatUptime(pc.uptimeSec, uptime, sizeof(uptime));
    snprintf(buf, sizeof(buf), "Mạng ↓ %s   ↑ %s", down, up);
    renderer.drawText(UI_10_FONT_ID, kMargin, y, buf);
    y += 30;
    snprintf(buf, sizeof(buf), "Đã chạy %s", uptime);
    renderer.drawText(UI_10_FONT_ID, kMargin, y, buf);
    y += 44;

    // Claude card.
    const int cardH = pc.claude.known ? 150 : 70;
    renderer.drawRoundedRect(kMargin, y, w - 2 * kMargin, cardH, 2, 10, true);
    renderer.drawText(UI_12_FONT_ID, kMargin + 14, y + 12, "Claude", true, EpdFontFamily::BOLD);
    if (!pc.claude.known) {
      renderer.drawText(UI_10_FONT_ID, kMargin + 120, y + 18, "Chưa có nguồn số liệu");
    } else {
      const int colW = (w - 2 * kMargin - 28) / 2;
      const struct {
        const char* label;
        int pct;
        uint32_t reset;
      } q[2] = {{"Phiên 5 giờ", pc.claude.sessionPct, pc.claude.sessionReset},
                {"Tuần này", pc.claude.weekPct, pc.claude.weekReset}};
      for (int i = 0; i < 2; ++i) {
        const int x = kMargin + 14 + i * colW;
        const int cy = y + 52;
        renderer.drawText(UI_10_FONT_ID, x, cy, q[i].label);
        snprintf(buf, sizeof(buf), "%d%%", q[i].pct);
        renderer.drawText(UI_12_FONT_ID, x, cy + 26, buf, true, EpdFontFamily::BOLD);
        drawBar(x, cy + 62, colW - 16, 12, q[i].pct);
        if (q[i].reset != 0) {
          formatLocalHm(q[i].reset, hm, sizeof(hm));
          snprintf(buf, sizeof(buf), "Làm mới %s", hm);
          renderer.drawText(UI_10_FONT_ID, x + 70, cy + 30, buf);
        }
      }
    }
  }

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow("Về"), "Làm mới", "Trang", "Trang");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DeskActivity::renderPhoto(const uint32_t nowUnix) {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  char line1[64];
  char line2[64];

  if (photos.empty()) {
    renderer.clearScreen();
    drawStatusBar("Khung ảnh", "");
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 60, "Chưa có ảnh", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 16, "Chép ảnh .bmp hoặc .png vào");
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 + 14, "/desk/photos trên thẻ nhớ", true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow("Về"), "", "Trang", "Trang");
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
    char wdName[8];
    if (wd == 0) {
      snprintf(wdName, sizeof(wdName), "CN");
    } else {
      snprintf(wdName, sizeof(wdName), "T%d", wd + 1);
    }
    snprintf(line1, sizeof(line1), "%s %d/%d · %d/%d ÂL", wdName, shown.day, shown.month, lunar.day, lunar.month);
  }
  if (weather.valid && desk::weatherFresh(weather, nowUnix)) {
    snprintf(line2, sizeof(line2), "%d° %s · ảnh %d/%d", weather.tempC, desk::skyLabel(weather.sky), photoIndex + 1,
             static_cast<int>(photos.size()));
  } else {
    snprintf(line2, sizeof(line2), "Ảnh %d/%d", photoIndex + 1, static_cast<int>(photos.size()));
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

  const std::string path = std::string(kPhotoDir) + "/" + photos[photoIndex];
  if (photo::render(renderer, path, false, overlay) != photo::Result::Ok) {
    renderer.clearScreen();
    drawStatusBar("Khung ảnh", "");
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 30, "Không đọc được ảnh", true, EpdFontFamily::BOLD);
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
  renderer.drawCenteredText(UI_12_FONT_ID, y, desk::pomodoroPhaseLabel(pomodoro.phase()), true, EpdFontFamily::BOLD);

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
  renderer.drawCenteredText(UI_10_FONT_ID, cy + 58, pomodoro.paused() ? "đã tạm dừng" : "phút còn lại");

  y = cy + radius + 40;
  // Round dots: done rounds filled.
  snprintf(buf, sizeof(buf), "Lượt %d / %d", pomodoro.round(), pomodoro.rounds());
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
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Bấm Chọn để bắt đầu 25 phút tập trung");
  } else if (shown.valid && !pomodoro.paused()) {
    const int endMin = (shown.hour * 60 + shown.minute + static_cast<int>(minutes)) % 1440;
    snprintf(buf, sizeof(buf), "Kết thúc lúc %02d:%02d", endMin / 60, endMin % 60);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
  }
  y += 40;
  renderer.drawLine(kMargin, y, w - kMargin, y, 1, true);
  y += 12;
  snprintf(buf, sizeof(buf), "Hôm nay: %d lượt · %lu phút tập trung", pomodoro.focusRoundsToday(),
           static_cast<unsigned long>(pomodoro.focusMinutesToday()));
  renderer.drawCenteredText(UI_10_FONT_ID, y, buf);

  const char* selectLabel = idle ? "Bắt đầu" : (pomodoro.paused() ? "Tiếp tục" : "Tạm dừng");
  const auto labels = idle ? mappedInput.mapLabels(mappedInput.withBackArrow("Về"), selectLabel, "Trang", "Trang")
                           : mappedInput.mapLabels(mappedInput.withBackArrow("Về"), selectLabel, "Bỏ qua", "Đặt lại");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DeskActivity::render(RenderLock&&) {
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
