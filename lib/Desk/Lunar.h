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

LunarDate solarToLunar(int day, int month, int year, double timeZone = 7.0);

// Heavenly stem + earthly branch, UTF-8, e.g. "Mậu Tuất". Writes into out (NUL-terminated,
// truncated to outSize).
void canChiOfDay(int32_t julianDayNumber, char* out, size_t outSize);
void canChiOfMonth(const LunarDate& lunar, char* out, size_t outSize);
void canChiOfYear(int lunarYear, char* out, size_t outSize);

// Lunar month name as used in Vietnamese: "Giêng", "Hai", ..., "Mười Một", "Chạp".
const char* lunarMonthName(int month);

// The nearest day from the given date (today included) carrying an event, within
// horizonDays: lunar festivals (Tết, Rằm tháng Giêng, Giỗ Tổ, Đoan Ngọ, Vu Lan, Trung Thu,
// ông Công ông Táo), each month's Rằm and Mùng 1, and the Gregorian public holidays.
struct CalendarEvent {
  bool found = false;
  int daysUntil = 0;  // 0 = today
  char name[48] = {};
};
CalendarEvent nextCalendarEvent(int day, int month, int year, int horizonDays = 45, double timeZone = 7.0);

}  // namespace desk
