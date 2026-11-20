#pragma once

#if CROSSINK_DESK

#include <cstdint>

#include "DeskWeather.h"
#include "OpenMeteo.h"

// Desk mode's weather fetch (docs/development/desk-mode-plan.md, phase 2): Wi-Fi up if needed
// (last-used saved network), place from /.crosspoint/desk.json (city name -> Open-Meteo
// geocoding, empty -> ip-api.com), forecast from Open-Meteo, Wi-Fi back off if it was off.
// Blocking; call from the activity loop after showing "updating".
namespace deskweather {

constexpr const char* kConfigPath = "/.crosspoint/desk.json";
constexpr const char* kCachePath = "/.crosspoint/desk-weather.json";

struct Config {
  char city[48] = {};      // as typed in desk.json; empty = locate by IP
  char resolvedFor[48] = {};  // the city the cached place was resolved from
  desk::Place place;
};

enum class Result : uint8_t { Ok, NoSavedWifi, WifiFailed, PlaceFailed, FetchFailed, ParseFailed };

bool loadConfig(Config& out);
bool saveConfig(const Config& cfg);
// The last good forecast body, so a restart shows weather before the next fetch.
bool loadCache(desk::WeatherReport& out);

Result refresh(Config& cfg, desk::WeatherReport& out, uint32_t nowUnix);
const char* resultText(Result r);  // Vietnamese, for the card

}  // namespace deskweather

#endif  // CROSSINK_DESK
