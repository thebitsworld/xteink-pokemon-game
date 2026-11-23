#pragma once

// Vietnamese lunar calendar for Desk mode (docs/development/desk-mode-plan.md).
//
// Ho Ngoc Duc's algorithm (new moons and solar terms from Jean Meeus' "Astronomical
// Algorithms"), computed in double: the reader's Lua is 32-bit float, which is too coarse for
// the Julian day arithmetic, hence C++. Time zone UTC+7 (Vietnam) unless given. Pure code, no
// Arduino dependency: tested on the host (test/desk).

#include <cstddef>
#include <cstdint>

namespace desk {

struct LunarDate {
  int day = 0;    // 1-30
  int month = 0;  // 1-12
  int year = 0;
  bool leap = false;  // a leap (nhuận) month
};

// Julian day number of a Gregorian date (Julian calendar before 1582-10-15).
int32_t julianDay(int day, int month, int year);
// Gregorian date of a Julian day number (inverse of julianDay for Gregorian dates).
void gregorianFromJulianDay(int32_t jdn, int& day, int& month, int& year);
// 0 = Sunday .. 6 = Saturday.
int weekdayOf(int32_t jdn);
// "Chủ Nhật", "Thứ Hai", ..., "Thứ Bảy" for 0..6.
const char* weekdayName(int weekday);

LunarDate solarToLunar(int day, int month, int year, double timeZone = 7.0);

// Heavenly stem + earthly branch, UTF-8, e.g. "Mậu Tuất". Writes into out (NUL-terminated,
// truncated to outSize).
void canChiOfDay(int32_t julianDayNumber, char* out, size_t outSize);
void canChiOfMonth(const LunarDate& lunar, char* out, size_t outSize);
void canChiOfYear(int lunarYear, char* out, size_t outSize);

// Giờ hoàng đạo of a day: bit i set for earthly branch i (0 Tý 23-1h, 1 Sửu 1-3h, ... 11 Hợi
// 21-23h); six bits are set. From the day's branch, as in Ho Ngoc Duc's calendar.
uint16_t auspiciousHours(int32_t julianDayNumber);
// The earthly branch's name, UTF-8: "Tý" .. "Hợi" for 0..11.
const char* branchName(int branch);

// Lunar month name as used in Vietnamese: "Giêng", "Hai", ..., "Mười Một", "Chạp".
const char* lunarMonthName(int month);

// The nearest day from the given date (today included) carrying an event, within
// horizonDays: lunar festivals (Tết, Rằm tháng Giêng, Giỗ Tổ, Đoan Ngọ, Vu Lan, Trung Thu,
// ông Công ông Táo), each month's Rằm and Mùng 1, and the Gregorian public holidays.
enum class EventKind : uint8_t {
  None,
  Tet,
  Lantern,
  HungKings,
  DoanNgo,
  VuLan,
  MidAutumn,
  KitchenGods,
  NewYear,
  Reunification,
  Labour,
  National,
  FullMoon,  // Rằm of lunarMonth
  FirstDay,  // Mùng 1 of lunarMonth
};
struct CalendarEvent {
  bool found = false;
  int daysUntil = 0;  // 0 = today
  EventKind kind = EventKind::None;
  int lunarMonth = 0;  // FullMoon / FirstDay
  bool leap = false;   // FullMoon / FirstDay in a leap month
  char name[48] = {};  // Vietnamese name, for logs and tests; the UI translates `kind`
};
CalendarEvent nextCalendarEvent(int day, int month, int year, int horizonDays = 45, double timeZone = 7.0);

}  // namespace desk
