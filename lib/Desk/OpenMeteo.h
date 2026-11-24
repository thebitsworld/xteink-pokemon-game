#pragma once

// Open-Meteo (forecast + geocoding, no API key) and ip-api.com responses for Desk mode's
// weather card (docs/development/desk-mode-plan.md, phase 2). Parsing and URL building only;
// the reader's fetcher does the network. Tested on the host (test/desk).

#include <cstddef>
#include <cstdint>
#include <string>

#include "DeskWeather.h"

namespace desk {

struct Place {
  bool valid = false;
  double lat = 0;
  double lon = 0;
  char name[48] = {};
};

// Percent-encodes a UTF-8 string for a URL query value (RFC 3986 unreserved kept).
std::string urlEncode(const char* text);

// Open-Meteo over plain HTTP: its HTTPS chain moved to Let's Encrypt's new YR1 / Root YR
// (2026-11), and since then every X3 fetch failed in ~2 s with no HTTP response (battery log,
// "-s0", with RAM to spare). Forecasts are public and need no key; HTTP also spares the RAM.
// http://api.open-meteo.com/v1/forecast?... for the card: current temperature, WMO code and
// day/night, 12 hourly codes and temperatures from the current hour, today's min/max, local
// time zone.
std::string forecastUrl(double lat, double lon);
// http://geocoding-api.open-meteo.com/v1/search?count=1&language=vi&format=json&name=<city>
std::string geocodingUrl(const char* city);
// ip-api.com only answers plain HTTP on its free tier; it sees the reader's public IP only.
constexpr const char* kIpLocateUrl = "http://ip-api.com/json/?fields=status,city,lat,lon";

// Fills `out` from a forecast response; fetchedAt is the Unix time of the fetch. False on a
// response without the current block.
bool parseForecast(const char* json, size_t length, uint32_t fetchedAt, WeatherReport& out);

// Open-Meteo air quality: current US AQI and PM2.5.
std::string airQualityUrl(double lat, double lon);
// Sets aqi / pm25 (rounded) from "current"; false when the answer has neither.
bool parseAirQuality(const char* json, size_t length, int& aqi, int& pm25);
// First geocoding result; false when there is none.
bool parseGeocoding(const char* json, size_t length, Place& out);
// ip-api.com answer with status "success".
bool parseIpLocate(const char* json, size_t length, Place& out);

}  // namespace desk
