#include "OpenMeteo.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace desk {

namespace {

int8_t clampTemp(const double v) {
  const long r = std::lround(v);
  if (r > 127) return 127;
  if (r < -127) return -127;
  return static_cast<int8_t>(r);
}

// "2026-11-20T13:00" -> 13, or -1.
int hourOf(const char* isoTime) {
  if (isoTime == nullptr || std::strlen(isoTime) < 13 || isoTime[10] != 'T') return -1;
  const int h = (isoTime[11] - '0') * 10 + (isoTime[12] - '0');
  return h >= 0 && h < 24 ? h : -1;
}

void copyName(char* dst, const size_t size, const char* src) {
  std::snprintf(dst, size, "%s", src == nullptr ? "" : src);
}

}  // namespace

std::string urlEncode(const char* text) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  if (text == nullptr) return out;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p) {
    const unsigned char c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0F]);
    }
  }
  return out;
}

std::string forecastUrl(const double lat, const double lon) {
  char buf[400];
  std::snprintf(buf, sizeof(buf),
                "http://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
                "&current=temperature_2m,weather_code,is_day&hourly=weather_code,temperature_2m&forecast_hours=%d"
                "&daily=temperature_2m_max,temperature_2m_min&forecast_days=1&timezone=auto",
                lat, lon, kForecastHours);
  return buf;
}

std::string airQualityUrl(const double lat, const double lon) {
  char buf[200];
  std::snprintf(buf, sizeof(buf),
                "http://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.3f&longitude=%.3f"
                "&current=us_aqi,pm2_5",
                lat, lon);
  return buf;
}

bool parseAirQuality(const char* json, const size_t length, int& aqi, int& pm25) {
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  const JsonObjectConst current = doc["current"];
  if (current.isNull()) return false;
  aqi = current["us_aqi"].isNull() ? -1 : static_cast<int>(std::lround(current["us_aqi"].as<double>()));
  pm25 = current["pm2_5"].isNull() ? -1 : static_cast<int>(std::lround(current["pm2_5"].as<double>()));
  return aqi >= 0 || pm25 >= 0;
}

std::string geocodingUrl(const char* city) {
  return "http://geocoding-api.open-meteo.com/v1/search?count=1&language=vi&format=json&name=" + urlEncode(city);
}

bool parseForecast(const char* json, const size_t length, const uint32_t fetchedAt, WeatherReport& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  const JsonObjectConst current = doc["current"];
  if (current.isNull() || current["weather_code"].isNull() || current["temperature_2m"].isNull()) return false;

  WeatherReport w;
  w.valid = true;
  w.fetchedAt = fetchedAt;
  w.sky = skyFromWmo(current["weather_code"].as<int>());
  w.tempC = clampTemp(current["temperature_2m"].as<double>());
  w.isDay = current["is_day"].as<int>() != 0;

  const JsonArrayConst times = doc["hourly"]["time"];
  const JsonArrayConst codes = doc["hourly"]["weather_code"];
  const JsonArrayConst temps = doc["hourly"]["temperature_2m"];
  int firstHour = times.isNull() || times.size() == 0 ? -1 : hourOf(times[0].as<const char*>());
  if (firstHour < 0) firstHour = hourOf(current["time"].as<const char*>());
  w.firstHour = static_cast<uint8_t>(firstHour < 0 ? 0 : firstHour);
  for (int i = 0; i < kForecastHours; ++i) {
    const bool has = !codes.isNull() && static_cast<size_t>(i) < codes.size();
    w.hourly[i] = has ? skyFromWmo(codes[i].as<int>()) : (i == 0 ? w.sky : Sky::Unknown);
    w.hourlyTemp[i] = !temps.isNull() && static_cast<size_t>(i) < temps.size() ? clampTemp(temps[i].as<double>())
                                                                              : w.tempC;
  }
  w.hourly[0] = w.sky;  // the current block is fresher than the hourly slot

  const JsonVariantConst maxT = doc["daily"]["temperature_2m_max"][0];
  const JsonVariantConst minT = doc["daily"]["temperature_2m_min"][0];
  w.tempMax = maxT.isNull() ? w.tempC : clampTemp(maxT.as<double>());
  w.tempMin = minT.isNull() ? w.tempC : clampTemp(minT.as<double>());
  out = w;
  return true;
}

bool parseGeocoding(const char* json, const size_t length, Place& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  const JsonObjectConst first = doc["results"][0];
  if (first.isNull() || first["latitude"].isNull() || first["longitude"].isNull()) return false;
  Place p;
  p.valid = true;
  p.lat = first["latitude"].as<double>();
  p.lon = first["longitude"].as<double>();
  copyName(p.name, sizeof(p.name), first["name"].as<const char*>());
  out = p;
  return true;
}

bool parseIpLocate(const char* json, const size_t length, Place& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  const char* status = doc["status"].as<const char*>();
  if (status == nullptr || std::strcmp(status, "success") != 0) return false;
  if (doc["lat"].isNull() || doc["lon"].isNull()) return false;
  Place p;
  p.valid = true;
  p.lat = doc["lat"].as<double>();
  p.lon = doc["lon"].as<double>();
  copyName(p.name, sizeof(p.name), doc["city"].as<const char*>());
  out = p;
  return true;
}

}  // namespace desk
