#include "DeskWeather.h"

#include <cstdio>

namespace desk {

Sky skyFromWmo(const int code) {
  switch (code) {
    case 0:
      return Sky::Clear;
    case 1:
    case 2:
      return Sky::PartlyCloudy;
    case 3:
      return Sky::Cloudy;
    case 45:
    case 48:
      return Sky::Fog;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      return Sky::Drizzle;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82:
      return Sky::Rain;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
      return Sky::Snow;
    case 95:
    case 96:
    case 99:
      return Sky::Storm;
    default:
      return Sky::Unknown;
  }
}

Feel feelFor(const int tempC) {
  if (tempC >= kHotFromC) return Feel::Hot;
  if (tempC < kColdBelowC) return Feel::Cold;
  return Feel::Mild;
}

bool isWet(const Sky sky) { return sky == Sky::Drizzle || sky == Sky::Rain || sky == Sky::Storm; }

const char* skyLabel(const Sky sky) {
  switch (sky) {
    case Sky::Clear:
      return "Trời quang";
    case Sky::PartlyCloudy:
      return "Ít mây";
    case Sky::Cloudy:
      return "Nhiều mây";
    case Sky::Fog:
      return "Sương mù";
    case Sky::Drizzle:
      return "Mưa nhẹ";
    case Sky::Rain:
      return "Mưa";
    case Sky::Storm:
      return "Dông";
    case Sky::Snow:
      return "Tuyết";
    case Sky::Unknown:
      break;
  }
  return "Chưa rõ";
}

bool weatherFresh(const WeatherReport& w, const uint32_t now) {
  return w.valid && now >= w.fetchedAt && now - w.fetchedAt <= kWeatherStaleSec;
}

int hoursUntilRain(const WeatherReport& w) {
  if (!w.valid) return -1;
  if (isWet(w.sky)) return 0;
  for (int i = 1; i < kForecastHours; ++i) {
    if (isWet(w.hourly[i])) return i;
  }
  return -1;
}

void rainHint(const WeatherReport& w, const uint32_t now, char* out, const size_t outSize) {
  if (out == nullptr || outSize == 0) return;
  out[0] = '\0';
  if (!weatherFresh(w, now)) return;
  const int in = hoursUntilRain(w);
  if (in == 0) {
    std::snprintf(out, outSize, "Đang mưa");
  } else if (in > 0) {
    std::snprintf(out, outSize, "Khoảng %dh có mưa", (w.firstHour + in) % 24);
  } else {
    std::snprintf(out, outSize, "%d giờ tới không mưa", kForecastHours);
  }
}

}  // namespace desk
