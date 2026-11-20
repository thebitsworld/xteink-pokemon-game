#include "DeskActivity.h"

#if CROSSINK_DESK

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "DeskFonts.h"
#include "Lunar.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr uint32_t kPollMs = 1000;
constexpr int kHalfRefreshEveryMinutes = 60;          // the USB default of "Full refresh" (plan)
constexpr uint32_t kWeatherEveryMs = 10 * 60 * 1000;  // the USB default of "Weather" (plan)
constexpr uint32_t kWeatherRetryMs = 2 * 60 * 1000;   // after a failed attempt
constexpr int kMargin = 16;

}  // namespace

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

void DeskActivity::onEnter() {
  Activity::onEnter();
  shown = readLocalTime();
  forceHalfRefresh = true;
  minutesSinceFull = 0;
  lastPollMs = millis();

  deskweather::loadConfig(weatherConfig);
  deskweather::loadCache(weather);
  uint32_t now = 0;
  const bool haveNow = readUtcUnix(now);
  // Fetch at once unless the cached forecast is younger than the interval.
  weatherFetchPending = !(weather.valid && haveNow && now >= weather.fetchedAt &&
                          now - weather.fetchedAt < kWeatherEveryMs / 1000);
  weatherAttempted = false;
  requestUpdate();
}

bool DeskActivity::preventAutoSleep() { return gpio.isUsbConnected() || weatherFetching; }

void DeskActivity::runWeatherFetch() {
  weatherFetchPending = false;
  weatherFetching = true;
  requestUpdateAndWait();  // shows "Đang cập nhật..." before the blocking fetch
  uint32_t now = 0;
  if (!readUtcUnix(now)) now = weather.fetchedAt;
  weatherResult = deskweather::refresh(weatherConfig, weather, now);
  weatherFetching = false;
  weatherAttempted = true;
  lastWeatherAttemptMs = millis();
  forceHalfRefresh = true;
  requestUpdate();
}

void DeskActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    shown = readLocalTime();
    runWeatherFetch();
    return;
  }

  const uint32_t nowMs = millis();
  if (weatherFetchPending) {
    runWeatherFetch();
    return;
  }
  if (gpio.isUsbConnected() && weatherAttempted) {
    const uint32_t wait = weatherResult == deskweather::Result::Ok ? kWeatherEveryMs : kWeatherRetryMs;
    if (nowMs - lastWeatherAttemptMs >= wait) {
      runWeatherFetch();
      return;
    }
  }

  if (nowMs - lastPollMs < kPollMs) return;
  lastPollMs = nowMs;
  const LocalTime t = readLocalTime();
  if (t.valid == shown.valid && t.minute == shown.minute && t.hour == shown.hour && t.day == shown.day) return;
  const bool newDay = t.day != shown.day || t.month != shown.month || t.year != shown.year;
  shown = t;
  if (newDay || ++minutesSinceFull >= kHalfRefreshEveryMinutes) forceHalfRefresh = true;
  requestUpdate();
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
        for (int i = 0; i < 3; ++i) disc(x + s * (28 + i * 22) / 100, dropTop + s * (i % 2 ? 18 : 10) / 100, 4, Color::Black);
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
  char hint[48];
  desk::rainHint(weather, nowUnix, hint, sizeof(hint));
  if (!fresh) {
    snprintf(hint, sizeof(hint), "Dữ liệu cũ%s%s", weatherAttempted ? " · " : "",
             weatherAttempted ? deskweather::resultText(weatherResult) : "");
  } else if (weatherFetching) {
    snprintf(hint, sizeof(hint), "Đang cập nhật...");
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

void DeskActivity::render(RenderLock&&) {
  const int w = renderer.getScreenWidth();
  char buf[96];
  renderer.clearScreen();
  uint32_t nowUnix = 0;
  readUtcUnix(nowUnix);

  // Status bar: Desk · place and update time · battery.
  renderer.drawText(UI_10_FONT_ID, kMargin, 14, "Desk", true, EpdFontFamily::BOLD);
  const bool usb = gpio.isUsbConnected();
  if (usb) {
    snprintf(buf, sizeof(buf), "Đang sạc");
  } else {
    snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
  }
  renderer.drawText(UI_10_FONT_ID, w - kMargin - renderer.getTextWidth(UI_10_FONT_ID, buf), 14, buf);
  if (weather.valid && weatherConfig.place.valid) {
    const int localMin = (static_cast<int>((weather.fetchedAt / 60) % 1440) + utcOffsetMinutes() + 2 * 1440) % 1440;
    snprintf(buf, sizeof(buf), "%s · %02d:%02d", weatherConfig.place.name, localMin / 60, localMin % 60);
    renderer.drawCenteredText(UI_10_FONT_ID, 14, buf);
  }
  renderer.fillRect(0, 44, w, 2, true);

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

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow("Thoát"), "Làm mới", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const bool half = forceHalfRefresh;
  forceHalfRefresh = false;
  if (half) minutesSinceFull = 0;
  renderer.displayBuffer(half ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

#endif  // CROSSINK_DESK
