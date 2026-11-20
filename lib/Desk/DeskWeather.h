#pragma once

// Weather rules for Desk mode (docs/development/desk-mode-plan.md): Open-Meteo's WMO weather
// codes to a small set of skies, hot/cold for a Vietnamese climate, freshness, and when the
// next rain starts. Same design as the owner's esp32-pet (its D36 decision), written anew for
// this MIT-licensed project. Pure code, tested on the host (test/desk).

#include <cstddef>
#include <cstdint>

namespace desk {

enum class Sky : uint8_t { Unknown = 0, Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Storm, Snow };
enum class Feel : uint8_t { Mild = 0, Hot, Cold };

constexpr int kHotFromC = 33;     // >= 33 C is hot
constexpr int kColdBelowC = 17;   // < 17 C is cold
constexpr int kForecastHours = 12;
constexpr uint32_t kWeatherStaleSec = 3 * 3600;  // older data: show as unknown

// WMO codes as documented by Open-Meteo ("WMO Weather interpretation codes").
Sky skyFromWmo(int code);
Feel feelFor(int tempC);
bool isWet(Sky sky);  // drizzle, rain or storm
const char* skyLabel(Sky sky);  // Vietnamese, e.g. "Mưa nhẹ"

struct WeatherReport {
  bool valid = false;
  uint32_t fetchedAt = 0;  // Unix seconds
  Sky sky = Sky::Unknown;
  int8_t tempC = 0;
  int8_t tempMin = 0;
  int8_t tempMax = 0;
  bool isDay = true;
  uint8_t firstHour = 0;  // local hour of hourly[0] (the current hour)
  Sky hourly[kForecastHours] = {};
  int8_t hourlyTemp[kForecastHours] = {};
};

bool weatherFresh(const WeatherReport& w, uint32_t now);
// Hours until rain starts: 0 = raining now, 1..kForecastHours-1, -1 = none in the forecast.
int hoursUntilRain(const WeatherReport& w);
// One line for the card: "Đang mưa", "Khoảng 15h có mưa", "12 giờ tới không mưa", or "" when
// the report is not fresh. Writes into out (NUL-terminated).
void rainHint(const WeatherReport& w, uint32_t now, char* out, size_t outSize);

}  // namespace desk
