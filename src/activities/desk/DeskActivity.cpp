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
constexpr int kHalfRefreshEveryMinutes = 60;  // the USB default of "Full refresh" (plan)
constexpr int kMargin = 16;

}  // namespace

DeskActivity::LocalTime DeskActivity::readLocalTime() {
  LocalTime t;
  uint16_t y = 0;
  uint8_t mo = 0, d = 0, h = 0, mi = 0;
  if (!halClock.getDateTime(y, mo, d, h, mi)) return t;
  // The RTC keeps UTC; apply the reader's UTC offset (quarter hours, biased by 48).
  const uint8_t biased = SETTINGS.clockUtcOffsetQ > 104 ? 104 : SETTINGS.clockUtcOffsetQ;
  const int offsetMin = (static_cast<int>(biased) - 48) * 15;
  int total = h * 60 + mi + offsetMin;
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
  requestUpdate();
}

bool DeskActivity::preventAutoSleep() { return gpio.isUsbConnected(); }

void DeskActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    shown = readLocalTime();
    forceHalfRefresh = true;
    requestUpdate();
    return;
  }

  const uint32_t now = millis();
  if (now - lastPollMs < kPollMs) return;
  lastPollMs = now;
  const LocalTime t = readLocalTime();
  if (t.valid == shown.valid && t.minute == shown.minute && t.hour == shown.hour && t.day == shown.day) return;
  const bool newDay = t.day != shown.day || t.month != shown.month || t.year != shown.year;
  shown = t;
  if (newDay || ++minutesSinceFull >= kHalfRefreshEveryMinutes) forceHalfRefresh = true;
  requestUpdate();
}

void DeskActivity::render(RenderLock&&) {
  const int w = renderer.getScreenWidth();
  char buf[96];
  renderer.clearScreen();

  // Status bar.
  renderer.drawText(UI_10_FONT_ID, kMargin, 14, "Desk", true, EpdFontFamily::BOLD);
  const bool usb = gpio.isUsbConnected();
  if (usb) {
    snprintf(buf, sizeof(buf), "Đang sạc");
  } else {
    snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
  }
  renderer.drawText(UI_10_FONT_ID, w - kMargin - renderer.getTextWidth(UI_10_FONT_ID, buf), 14, buf);
  renderer.fillRect(0, 44, w, 2, true);

  int y = 66;
  if (!shown.valid) {
    renderer.drawCenteredText(DESK_CLOCK_FONT_ID, y, "--:--");
    y += renderer.getLineHeight(DESK_CLOCK_FONT_ID) + 10;
    renderer.drawCenteredText(UI_12_FONT_ID, y, "Chưa có giờ", true, EpdFontFamily::BOLD);
    y += 34;
    renderer.drawCenteredText(UI_10_FONT_ID, y, "Settings > Clock > Sync now (cần Wi-Fi)");
  } else {
    snprintf(buf, sizeof(buf), "%02d:%02d", shown.hour, shown.minute);
    renderer.drawCenteredText(DESK_CLOCK_FONT_ID, y, buf);
    y += renderer.getLineHeight(DESK_CLOCK_FONT_ID) + 6;

    const int32_t jd = desk::julianDay(shown.day, shown.month, shown.year);
    snprintf(buf, sizeof(buf), "%s, %d/%d/%d", desk::weekdayName(desk::weekdayOf(jd)), shown.day, shown.month,
             shown.year);
    renderer.drawCenteredText(UI_12_FONT_ID, y, buf, true, EpdFontFamily::BOLD);
    y += 40;

    const desk::LunarDate lunar = desk::solarToLunar(shown.day, shown.month, shown.year);
    char yearName[32];
    desk::canChiOfYear(lunar.year, yearName, sizeof(yearName));
    snprintf(buf, sizeof(buf), "Âm lịch %d/%d%s · năm %s", lunar.day, lunar.month, lunar.leap ? " nhuận" : "",
             yearName);
    renderer.drawCenteredText(UI_12_FONT_ID, y, buf);
    y += 36;

    char dayName[32];
    char monthName[32];
    desk::canChiOfDay(jd, dayName, sizeof(dayName));
    desk::canChiOfMonth(lunar, monthName, sizeof(monthName));
    snprintf(buf, sizeof(buf), "Ngày %s · tháng %s", dayName, monthName);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf);
    y += 40;

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
    y += 48;
  }

  // Weather card (phase 2): an outline with a placeholder until the fetcher exists.
  const int cardTop = y;
  const int cardH = 170;
  renderer.drawRoundedRect(kMargin, cardTop, w - 2 * kMargin, cardH, 2, 10, true);
  renderer.drawCenteredText(UI_12_FONT_ID, cardTop + cardH / 2 - 26, "Thời tiết", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, cardTop + cardH / 2 + 6, "Chưa có dữ liệu");

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow("Thoát"), "Làm mới", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  const bool half = forceHalfRefresh;
  forceHalfRefresh = false;
  if (half) minutesSinceFull = 0;
  renderer.displayBuffer(half ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

#endif  // CROSSINK_DESK
