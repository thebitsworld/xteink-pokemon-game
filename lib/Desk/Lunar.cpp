#include "Lunar.h"

#include <cmath>
#include <cstdio>

namespace desk {

namespace {

constexpr double kPi = 3.14159265358979323846;

const char* const kCan[10] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
const char* const kChi[12] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};
const char* const kMonthNames[12] = {"Giêng", "Hai", "Ba", "Tư", "Năm", "Sáu",
                                     "Bảy", "Tám", "Chín", "Mười", "Mười Một", "Chạp"};

int32_t floorToInt(const double v) { return static_cast<int32_t>(std::floor(v)); }

// Julian day (fractional) of the k-th new moon after 1900-01-01 (Meeus).
double newMoon(const int k) {
  const double T = k / 1236.85;
  const double T2 = T * T;
  const double T3 = T2 * T;
  const double dr = kPi / 180.0;
  double jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
  jd1 += 0.00033 * std::sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
  const double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
  const double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
  const double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
  double c1 = (0.1734 - 0.000393 * T) * std::sin(M * dr) + 0.0021 * std::sin(2 * dr * M);
  c1 = c1 - 0.4068 * std::sin(Mpr * dr) + 0.0161 * std::sin(dr * 2 * Mpr);
  c1 = c1 - 0.0004 * std::sin(dr * 3 * Mpr);
  c1 = c1 + 0.0104 * std::sin(dr * 2 * F) - 0.0051 * std::sin(dr * (M + Mpr));
  c1 = c1 - 0.0074 * std::sin(dr * (M - Mpr)) + 0.0004 * std::sin(dr * (2 * F + M));
  c1 = c1 - 0.0004 * std::sin(dr * (2 * F - M)) - 0.0006 * std::sin(dr * (2 * F + Mpr));
  c1 = c1 + 0.0010 * std::sin(dr * (2 * F - Mpr)) + 0.0005 * std::sin(dr * (2 * Mpr + M));
  const double deltaT = T < -11 ? 0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T * T3
                                : -0.000278 + 0.000265 * T + 0.000262 * T2;
  return jd1 + c1 - deltaT;
}

// Sun longitude at the start of a day, as one of 12 sectors of 30 degrees (0 = from 0 deg).
int sunLongitudeSector(const int32_t jdn, const double timeZone) {
  const double T = (jdn - 2451545.5 - timeZone / 24.0) / 36525.0;
  const double T2 = T * T;
  const double dr = kPi / 180.0;
  const double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
  const double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
  double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * std::sin(dr * M);
  DL += (0.019993 - 0.000101 * T) * std::sin(dr * 2 * M) + 0.000290 * std::sin(dr * 3 * M);
  double L = (L0 + DL) * dr;
  L -= kPi * 2 * floorToInt(L / (kPi * 2));
  return floorToInt(L / kPi * 6);
}

int32_t newMoonDay(const int k, const double timeZone) { return floorToInt(newMoon(k) + 0.5 + timeZone / 24.0); }

// Day of the new moon that starts lunar month 11 (the month holding the winter solstice).
int32_t lunarMonth11(const int year, const double timeZone) {
  const int32_t off = julianDay(31, 12, year) - 2415021;
  const int k = floorToInt(off / 29.530588853);
  int32_t nm = newMoonDay(k, timeZone);
  if (sunLongitudeSector(nm, timeZone) >= 9) nm = newMoonDay(k - 1, timeZone);
  return nm;
}

// Index (after month 11) of the leap month in a year with 13 lunar months.
int leapMonthOffset(const int32_t a11, const double timeZone) {
  const int k = floorToInt((a11 - 2415021.076998695) / 29.530588853 + 0.5);
  int last = 0;
  int i = 1;
  int arc = sunLongitudeSector(newMoonDay(k + i, timeZone), timeZone);
  do {
    last = arc;
    ++i;
    arc = sunLongitudeSector(newMoonDay(k + i, timeZone), timeZone);
  } while (arc != last && i < 14);
  return i - 1;
}

void copyText(char* out, const size_t outSize, const char* a, const char* b) {
  if (out == nullptr || outSize == 0) return;
  std::snprintf(out, outSize, "%s %s", a, b);
}

}  // namespace

int32_t julianDay(const int day, const int month, const int year) {
  const int a = (14 - month) / 12;
  const int y = year + 4800 - a;
  const int m = month + 12 * a - 3;
  int32_t jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
  if (jd < 2299161) jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
  return jd;
}

void gregorianFromJulianDay(const int32_t jdn, int& day, int& month, int& year) {
  const int32_t a = jdn + 32044;
  const int32_t b = (4 * a + 3) / 146097;
  const int32_t c = a - 146097 * b / 4;
  const int32_t d = (4 * c + 3) / 1461;
  const int32_t e = c - 1461 * d / 4;
  const int32_t m = (5 * e + 2) / 153;
  day = static_cast<int>(e - (153 * m + 2) / 5 + 1);
  month = static_cast<int>(m + 3 - 12 * (m / 10));
  year = static_cast<int>(100 * b + d - 4800 + m / 10);
}

int weekdayOf(const int32_t jdn) { return static_cast<int>((jdn + 1) % 7); }

const char* weekdayName(const int weekday) {
  static const char* const kNames[7] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
  if (weekday < 0 || weekday > 6) return "";
  return kNames[weekday];
}

LunarDate solarToLunar(const int day, const int month, const int year, const double timeZone) {
  const int32_t dayNumber = julianDay(day, month, year);
  const int k = floorToInt((dayNumber - 2415021.076998695) / 29.530588853);
  int32_t monthStart = newMoonDay(k + 1, timeZone);
  if (monthStart > dayNumber) monthStart = newMoonDay(k, timeZone);

  int32_t a11 = lunarMonth11(year, timeZone);
  int32_t b11 = a11;
  int lunarYear;
  if (a11 >= monthStart) {
    lunarYear = year;
    a11 = lunarMonth11(year - 1, timeZone);
  } else {
    lunarYear = year + 1;
    b11 = lunarMonth11(year + 1, timeZone);
  }

  LunarDate out;
  out.day = dayNumber - monthStart + 1;
  const int diff = floorToInt((monthStart - a11) / 29.0);
  int lunarMonth = diff + 11;
  if (b11 - a11 > 365) {
    const int leapOffset = leapMonthOffset(a11, timeZone);
    if (diff >= leapOffset) {
      lunarMonth = diff + 10;
      if (diff == leapOffset) out.leap = true;
    }
  }
  if (lunarMonth > 12) lunarMonth -= 12;
  if (lunarMonth >= 11 && diff < 4) lunarYear -= 1;
  out.month = lunarMonth;
  out.year = lunarYear;
  return out;
}

void canChiOfDay(const int32_t jdn, char* out, const size_t outSize) {
  copyText(out, outSize, kCan[(jdn + 9) % 10], kChi[(jdn + 1) % 12]);
}

void canChiOfMonth(const LunarDate& lunar, char* out, const size_t outSize) {
  copyText(out, outSize, kCan[(lunar.year * 12 + lunar.month + 3) % 10], kChi[(lunar.month + 1) % 12]);
}

void canChiOfYear(const int lunarYear, char* out, const size_t outSize) {
  copyText(out, outSize, kCan[(lunarYear + 6) % 10], kChi[(lunarYear + 8) % 12]);
}

const char* lunarMonthName(const int month) {
  if (month < 1 || month > 12) return "";
  return kMonthNames[month - 1];
}

CalendarEvent nextCalendarEvent(const int day, const int month, const int year, const int horizonDays,
                                const double timeZone) {
  CalendarEvent ev;
  const int32_t start = julianDay(day, month, year);
  for (int offset = 0; offset <= horizonDays; ++offset) {
    int sd = 0;
    int sm = 0;
    int sy = 0;
    gregorianFromJulianDay(start + offset, sd, sm, sy);

    const char* fixed = nullptr;
    EventKind fixedKind = EventKind::None;
    if (sd == 1 && sm == 1) fixed = "Tết Dương lịch", fixedKind = EventKind::NewYear;
    else if (sd == 30 && sm == 4) fixed = "Ngày Thống nhất", fixedKind = EventKind::Reunification;
    else if (sd == 1 && sm == 5) fixed = "Quốc tế Lao động", fixedKind = EventKind::Labour;
    else if (sd == 2 && sm == 9) fixed = "Quốc khánh", fixedKind = EventKind::National;

    const LunarDate l = solarToLunar(sd, sm, sy, timeZone);
    const char* lunarNamed = nullptr;
    EventKind lunarKind = EventKind::None;
    if (!l.leap) {
      if (l.month == 1 && l.day == 1) lunarNamed = "Tết Nguyên đán", lunarKind = EventKind::Tet;
      else if (l.month == 1 && l.day == 15) lunarNamed = "Rằm tháng Giêng", lunarKind = EventKind::Lantern;
      else if (l.month == 3 && l.day == 10) lunarNamed = "Giỗ Tổ Hùng Vương", lunarKind = EventKind::HungKings;
      else if (l.month == 5 && l.day == 5) lunarNamed = "Tết Đoan Ngọ", lunarKind = EventKind::DoanNgo;
      else if (l.month == 7 && l.day == 15) lunarNamed = "Vu Lan", lunarKind = EventKind::VuLan;
      else if (l.month == 8 && l.day == 15) lunarNamed = "Tết Trung Thu", lunarKind = EventKind::MidAutumn;
      else if (l.month == 12 && l.day == 23) lunarNamed = "Ông Công ông Táo", lunarKind = EventKind::KitchenGods;
    }

    if (lunarNamed != nullptr) {
      std::snprintf(ev.name, sizeof(ev.name), "%s", lunarNamed);
      ev.kind = lunarKind;
    } else if (fixed != nullptr) {
      std::snprintf(ev.name, sizeof(ev.name), "%s", fixed);
      ev.kind = fixedKind;
    } else if (l.day == 15 || l.day == 1) {
      std::snprintf(ev.name, sizeof(ev.name), "%s tháng %s%s", l.day == 15 ? "Rằm" : "Mùng 1", lunarMonthName(l.month),
                    l.leap ? " nhuận" : "");
      ev.kind = l.day == 15 ? EventKind::FullMoon : EventKind::FirstDay;
      ev.lunarMonth = l.month;
      ev.leap = l.leap;
    } else {
      continue;
    }
    ev.found = true;
    ev.daysUntil = offset;
    return ev;
  }
  return ev;
}

}  // namespace desk
