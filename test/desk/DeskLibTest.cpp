#include <gtest/gtest.h>

#include <string>

#include "DeskWeather.h"
#include "Lunar.h"

namespace {

struct SolarDate {
  int day, month, year;
};

// Gregorian date of Tết Nguyên đán (lunar 1/1) in Vietnam, 2000-2030. 2007 and 2030 differ
// from the Chinese New Year (Feb 18 2007, Feb 3 2030): those new moons fall before midnight at
// UTC+7 but after it at UTC+8.
constexpr SolarDate kTet[] = {
    {5, 2, 2000},  {24, 1, 2001}, {12, 2, 2002}, {1, 2, 2003},  {22, 1, 2004}, {9, 2, 2005},  {29, 1, 2006},
    {17, 2, 2007}, {7, 2, 2008},  {26, 1, 2009}, {14, 2, 2010}, {3, 2, 2011},  {23, 1, 2012}, {10, 2, 2013},
    {31, 1, 2014}, {19, 2, 2015}, {8, 2, 2016},  {28, 1, 2017}, {16, 2, 2018}, {5, 2, 2019},  {25, 1, 2020},
    {12, 2, 2021}, {1, 2, 2022},  {22, 1, 2023}, {10, 2, 2024}, {29, 1, 2025}, {17, 2, 2026}, {6, 2, 2027},
    {26, 1, 2028}, {13, 2, 2029}, {2, 2, 2030},
};

std::string canChiDay(int d, int m, int y) {
  char buf[32];
  desk::canChiOfDay(desk::julianDay(d, m, y), buf, sizeof(buf));
  return buf;
}

}  // namespace

TEST(Lunar, TetDates2000To2030) {
  for (const auto& t : kTet) {
    const desk::LunarDate l = desk::solarToLunar(t.day, t.month, t.year);
    EXPECT_EQ(l.day, 1) << t.day << "/" << t.month << "/" << t.year;
    EXPECT_EQ(l.month, 1) << t.day << "/" << t.month << "/" << t.year;
    EXPECT_EQ(l.year, t.year) << t.day << "/" << t.month << "/" << t.year;
    EXPECT_FALSE(l.leap);
  }
}

TEST(Lunar, DayBeforeTetIsInMonth12) {
  const desk::LunarDate l = desk::solarToLunar(16, 2, 2026);
  EXPECT_EQ(l.month, 12);
  EXPECT_EQ(l.year, 2025);
  EXPECT_GE(l.day, 29);
}

TEST(Lunar, LeapMonths) {
  // First day of a leap month.
  desk::LunarDate l = desk::solarToLunar(23, 5, 2020);
  EXPECT_EQ(l.day, 1);
  EXPECT_EQ(l.month, 4);
  EXPECT_TRUE(l.leap);
  l = desk::solarToLunar(22, 3, 2023);
  EXPECT_EQ(l.day, 1);
  EXPECT_EQ(l.month, 2);
  EXPECT_TRUE(l.leap);
  l = desk::solarToLunar(25, 7, 2025);
  EXPECT_EQ(l.day, 1);
  EXPECT_EQ(l.month, 6);
  EXPECT_TRUE(l.leap);
  // The ordinary month before it is not marked leap.
  l = desk::solarToLunar(25, 6, 2025);
  EXPECT_EQ(l.day, 1);
  EXPECT_EQ(l.month, 6);
  EXPECT_FALSE(l.leap);
}

TEST(Lunar, MockupDate) {
  // 20/11/2026 as on the Desk mode mockups: 12/10 năm Bính Ngọ, ngày Mậu Tuất, tháng Kỷ Hợi.
  const desk::LunarDate l = desk::solarToLunar(20, 11, 2026);
  EXPECT_EQ(l.day, 12);
  EXPECT_EQ(l.month, 10);
  EXPECT_EQ(l.year, 2026);
  EXPECT_FALSE(l.leap);
  char buf[32];
  desk::canChiOfYear(l.year, buf, sizeof(buf));
  EXPECT_STREQ(buf, "Bính Ngọ");
  desk::canChiOfMonth(l, buf, sizeof(buf));
  EXPECT_STREQ(buf, "Kỷ Hợi");
  EXPECT_EQ(canChiDay(20, 11, 2026), "Mậu Tuất");
}

TEST(Lunar, CanChi) {
  EXPECT_EQ(canChiDay(1, 1, 2000), "Mậu Ngọ");
  char buf[32];
  desk::canChiOfYear(2024, buf, sizeof(buf));
  EXPECT_STREQ(buf, "Giáp Thìn");
  desk::canChiOfYear(2025, buf, sizeof(buf));
  EXPECT_STREQ(buf, "Ất Tỵ");
  EXPECT_STREQ(desk::lunarMonthName(1), "Giêng");
  EXPECT_STREQ(desk::lunarMonthName(11), "Mười Một");
  EXPECT_STREQ(desk::lunarMonthName(12), "Chạp");
  EXPECT_STREQ(desk::lunarMonthName(13), "");
}

TEST(Lunar, GregorianRoundTripAndWeekday) {
  for (int32_t jd = desk::julianDay(1, 1, 1999); jd < desk::julianDay(1, 1, 2032); jd += 17) {
    int d = 0, m = 0, y = 0;
    desk::gregorianFromJulianDay(jd, d, m, y);
    EXPECT_EQ(desk::julianDay(d, m, y), jd);
  }
  EXPECT_EQ(desk::weekdayOf(desk::julianDay(20, 11, 2026)), 5);  // a Friday
  EXPECT_EQ(desk::weekdayOf(desk::julianDay(1, 1, 2000)), 6);    // a Saturday
  EXPECT_STREQ(desk::weekdayName(5), "Thứ Sáu");
  EXPECT_STREQ(desk::weekdayName(0), "Chủ Nhật");
}

TEST(Lunar, NextEvent) {
  desk::CalendarEvent e = desk::nextCalendarEvent(20, 11, 2026);
  ASSERT_TRUE(e.found);
  EXPECT_EQ(e.daysUntil, 3);
  EXPECT_STREQ(e.name, "Rằm tháng Mười");
  EXPECT_EQ(e.kind, desk::EventKind::FullMoon);
  EXPECT_EQ(e.lunarMonth, 10);
  EXPECT_FALSE(e.leap);

  e = desk::nextCalendarEvent(16, 2, 2026);
  ASSERT_TRUE(e.found);
  EXPECT_EQ(e.daysUntil, 1);
  EXPECT_STREQ(e.name, "Tết Nguyên đán");
  EXPECT_EQ(e.kind, desk::EventKind::Tet);

  e = desk::nextCalendarEvent(2, 9, 2026);  // Quốc khánh today
  ASSERT_TRUE(e.found);
  EXPECT_EQ(e.daysUntil, 0);
  EXPECT_STREQ(e.name, "Quốc khánh");

  e = desk::nextCalendarEvent(20, 11, 2026, 1);  // nothing within a day
  EXPECT_FALSE(e.found);
}

TEST(DeskWeather, WmoCodes) {
  EXPECT_EQ(desk::skyFromWmo(0), desk::Sky::Clear);
  EXPECT_EQ(desk::skyFromWmo(2), desk::Sky::PartlyCloudy);
  EXPECT_EQ(desk::skyFromWmo(3), desk::Sky::Cloudy);
  EXPECT_EQ(desk::skyFromWmo(45), desk::Sky::Fog);
  EXPECT_EQ(desk::skyFromWmo(53), desk::Sky::Drizzle);
  EXPECT_EQ(desk::skyFromWmo(63), desk::Sky::Rain);
  EXPECT_EQ(desk::skyFromWmo(81), desk::Sky::Rain);
  EXPECT_EQ(desk::skyFromWmo(75), desk::Sky::Snow);
  EXPECT_EQ(desk::skyFromWmo(95), desk::Sky::Storm);
  EXPECT_EQ(desk::skyFromWmo(4), desk::Sky::Unknown);
}

TEST(DeskWeather, FeelThresholds) {
  EXPECT_EQ(desk::feelFor(33), desk::Feel::Hot);
  EXPECT_EQ(desk::feelFor(32), desk::Feel::Mild);
  EXPECT_EQ(desk::feelFor(17), desk::Feel::Mild);
  EXPECT_EQ(desk::feelFor(16), desk::Feel::Cold);
}

TEST(DeskWeather, RainHint) {
  desk::WeatherReport w;
  w.valid = true;
  w.fetchedAt = 1000000;
  w.sky = desk::Sky::Cloudy;
  w.firstHour = 13;
  for (auto& h : w.hourly) h = desk::Sky::Cloudy;
  char buf[48];

  desk::rainHint(w, 1000000, buf, sizeof(buf));
  EXPECT_STREQ(buf, "12 giờ tới không mưa");
  EXPECT_EQ(desk::hoursUntilRain(w), -1);

  w.hourly[2] = desk::Sky::Rain;
  EXPECT_EQ(desk::hoursUntilRain(w), 2);
  desk::rainHint(w, 1000000, buf, sizeof(buf));
  EXPECT_STREQ(buf, "Khoảng 15h có mưa");

  w.firstHour = 23;  // wraps past midnight
  desk::rainHint(w, 1000000, buf, sizeof(buf));
  EXPECT_STREQ(buf, "Khoảng 1h có mưa");

  w.sky = desk::Sky::Drizzle;
  desk::rainHint(w, 1000000, buf, sizeof(buf));
  EXPECT_STREQ(buf, "Đang mưa");

  // Stale after three hours: no hint.
  desk::rainHint(w, 1000000 + desk::kWeatherStaleSec + 1, buf, sizeof(buf));
  EXPECT_STREQ(buf, "");
  EXPECT_FALSE(desk::weatherFresh(w, 1000000 + desk::kWeatherStaleSec + 1));
  EXPECT_TRUE(desk::weatherFresh(w, 1000000 + desk::kWeatherStaleSec));
}

TEST(DeskWeather, Labels) {
  EXPECT_STREQ(desk::skyLabel(desk::Sky::Drizzle), "Mưa nhẹ");
  EXPECT_STREQ(desk::skyLabel(desk::Sky::Unknown), "Chưa rõ");
}

TEST(Lunar, AuspiciousHours) {
  // 2026-11-23 is a Tân Sửu day: hoàng đạo Dần, Mão, Tỵ, Thân, Tuất, Hợi.
  const int32_t jd = desk::julianDay(23, 11, 2026);
  char canChi[32];
  desk::canChiOfDay(jd, canChi, sizeof(canChi));
  EXPECT_STREQ(canChi, "Tân Sửu");
  const uint16_t bits = desk::auspiciousHours(jd);
  EXPECT_EQ(bits, (1u << 2) | (1u << 3) | (1u << 5) | (1u << 8) | (1u << 10) | (1u << 11));
  // A Tý day (and a Ngọ day) share the row: Tý, Sửu, Mão, Ngọ, Thân, Dậu.
  int32_t ty = jd;
  while ((ty + 1) % 12 != 0) ++ty;
  EXPECT_EQ(desk::auspiciousHours(ty), (1u << 0) | (1u << 1) | (1u << 3) | (1u << 6) | (1u << 8) | (1u << 9));
  EXPECT_EQ(desk::auspiciousHours(ty + 6), desk::auspiciousHours(ty));
  for (int32_t d = jd; d < jd + 12; ++d) EXPECT_EQ(__builtin_popcount(desk::auspiciousHours(d)), 6);
  EXPECT_STREQ(desk::branchName(0), "Tý");
  EXPECT_STREQ(desk::branchName(11), "Hợi");
}
