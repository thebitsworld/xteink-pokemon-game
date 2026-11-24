#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "OpenMeteo.h"

namespace {

// Shape of a real Open-Meteo answer for the URL forecastUrl() builds (values made up).
constexpr char kForecast[] = R"({
  "latitude": 21.0, "longitude": 105.75, "timezone": "Asia/Bangkok",
  "current": {"time": "2026-11-20T13:00", "interval": 900, "temperature_2m": 26.4,
              "weather_code": 51, "is_day": 1},
  "hourly": {
    "time": ["2026-11-20T13:00","2026-11-20T14:00","2026-11-20T15:00","2026-11-20T16:00",
             "2026-11-20T17:00","2026-11-20T18:00","2026-11-20T19:00","2026-11-20T20:00",
             "2026-11-20T21:00","2026-11-20T22:00","2026-11-20T23:00","2026-11-21T00:00"],
    "weather_code": [3, 3, 61, 63, 61, 3, 2, 1, 0, 0, 0, 0],
    "temperature_2m": [28.0, 27.6, 25.1, 24.9, 24.0, 23.8, 23.5, 23.0, 22.8, 22.6, 22.5, 22.1]
  },
  "daily": {"time": ["2026-11-20"], "temperature_2m_max": [29.2], "temperature_2m_min": [21.6]}
})";

}  // namespace

TEST(OpenMeteo, ParsesForecast) {
  desk::WeatherReport w;
  ASSERT_TRUE(desk::parseForecast(kForecast, std::strlen(kForecast), 1795000000, w));
  EXPECT_TRUE(w.valid);
  EXPECT_EQ(w.fetchedAt, 1795000000u);
  EXPECT_EQ(w.sky, desk::Sky::Drizzle);
  EXPECT_EQ(w.tempC, 26);
  EXPECT_EQ(w.tempMin, 22);
  EXPECT_EQ(w.tempMax, 29);
  EXPECT_TRUE(w.isDay);
  EXPECT_EQ(w.firstHour, 13);
  EXPECT_EQ(w.hourly[0], desk::Sky::Drizzle);  // current block wins over hourly[0]
  EXPECT_EQ(w.hourly[2], desk::Sky::Rain);
  EXPECT_EQ(w.hourly[8], desk::Sky::Clear);
  EXPECT_EQ(w.hourlyTemp[3], 25);
  EXPECT_EQ(desk::hoursUntilRain(w), 0);  // drizzle now
}

TEST(OpenMeteo, RejectsBrokenForecast) {
  desk::WeatherReport w;
  EXPECT_FALSE(desk::parseForecast("{}", 2, 1, w));
  EXPECT_FALSE(desk::parseForecast("not json", 8, 1, w));
  EXPECT_FALSE(w.valid);
  const char* errorBody = R"({"error": true, "reason": "Latitude must be in range of -90 to 90"})";
  EXPECT_FALSE(desk::parseForecast(errorBody, std::strlen(errorBody), 1, w));
}

TEST(OpenMeteo, ShortHourlyArrays) {
  const char* json =
      R"({"current":{"time":"2026-11-20T23:00","temperature_2m":-3.6,"weather_code":71,"is_day":0},
          "hourly":{"time":["2026-11-20T23:00","2026-11-21T00:00"],"weather_code":[71,73],
                    "temperature_2m":[-3.6,-4.2]}})";
  desk::WeatherReport w;
  ASSERT_TRUE(desk::parseForecast(json, std::strlen(json), 5, w));
  EXPECT_EQ(w.tempC, -4);
  EXPECT_FALSE(w.isDay);
  EXPECT_EQ(w.firstHour, 23);
  EXPECT_EQ(w.hourly[1], desk::Sky::Snow);
  EXPECT_EQ(w.hourly[5], desk::Sky::Unknown);
  EXPECT_EQ(w.tempMax, -4);  // no daily block: falls back to the current temperature
}

TEST(OpenMeteo, Geocoding) {
  const char* json =
      R"({"results":[{"id":1581130,"name":"Hà Nội","latitude":21.0245,"longitude":105.84117,"country":"Việt Nam"}],
          "generationtime_ms":0.6})";
  desk::Place p;
  ASSERT_TRUE(desk::parseGeocoding(json, std::strlen(json), p));
  EXPECT_NEAR(p.lat, 21.0245, 1e-6);
  EXPECT_NEAR(p.lon, 105.84117, 1e-6);
  EXPECT_STREQ(p.name, "Hà Nội");
  const char* none = R"({"generationtime_ms":0.3})";
  desk::Place q;
  EXPECT_FALSE(desk::parseGeocoding(none, std::strlen(none), q));
  EXPECT_FALSE(q.valid);
}

TEST(OpenMeteo, IpLocate) {
  const char* ok = R"({"status":"success","city":"Hanoi","lat":21.0292,"lon":105.8526})";
  desk::Place p;
  ASSERT_TRUE(desk::parseIpLocate(ok, std::strlen(ok), p));
  EXPECT_STREQ(p.name, "Hanoi");
  const char* fail = R"({"status":"fail","message":"private range"})";
  desk::Place q;
  EXPECT_FALSE(desk::parseIpLocate(fail, std::strlen(fail), q));
}

TEST(OpenMeteo, Urls) {
  EXPECT_EQ(desk::urlEncode("Hà Nội"), "H%C3%A0%20N%E1%BB%99i");
  EXPECT_EQ(desk::urlEncode("Da-Nang_1.~"), "Da-Nang_1.~");
  EXPECT_EQ(desk::geocodingUrl("Huế"),
            "http://geocoding-api.open-meteo.com/v1/search?count=1&language=vi&format=json&name=Hu%E1%BA%BF");
  const std::string f = desk::forecastUrl(21.0245, 105.84117);
  EXPECT_NE(f.find("latitude=21.024&longitude=105.841"), std::string::npos) << f;  // 21.0245 is 21.02449.. in binary
  EXPECT_NE(f.find("forecast_hours=12"), std::string::npos);
  EXPECT_NE(f.find("timezone=auto"), std::string::npos);
}

TEST(OpenMeteo, AirQuality) {
  int aqi = -2, pm = -2;
  const char json[] = R"({"current":{"time":"2026-11-23T19:00","us_aqi":152.4,"pm2_5":56.6}})";
  ASSERT_TRUE(desk::parseAirQuality(json, sizeof(json) - 1, aqi, pm));
  EXPECT_EQ(aqi, 152);
  EXPECT_EQ(pm, 57);
  const char none[] = R"({"current":{"time":"2026-11-23T19:00"}})";
  EXPECT_FALSE(desk::parseAirQuality(none, sizeof(none) - 1, aqi, pm));
  EXPECT_NE(desk::airQualityUrl(21.03, 105.841).find("latitude=21.030&longitude=105.841&current=us_aqi,pm2_5"),
            std::string::npos);
}

TEST(DeskWeather, AqiLevels) {
  EXPECT_EQ(desk::aqiLevel(-1), -1);
  EXPECT_EQ(desk::aqiLevel(0), 0);
  EXPECT_EQ(desk::aqiLevel(50), 0);
  EXPECT_EQ(desk::aqiLevel(51), 1);
  EXPECT_EQ(desk::aqiLevel(150), 2);
  EXPECT_EQ(desk::aqiLevel(200), 3);
  EXPECT_EQ(desk::aqiLevel(300), 4);
  EXPECT_EQ(desk::aqiLevel(301), 5);
}
