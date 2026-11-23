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
  char agent[64] = {};  // PC agent "host:port"; empty = find it by mDNS (_xdesk._tcp)
  bool fahrenheit = false;  // "unit": "F" in desk.json; temperatures are fetched in °C
};

enum class Result : uint8_t { Ok, NoSavedWifi, WifiFailed, PlaceFailed, FetchFailed, ParseFailed, NoAgent };

bool loadConfig(Config& out);
bool saveConfig(const Config& cfg);
// The last good forecast body, so a restart shows weather before the next fetch.
bool loadCache(desk::WeatherReport& out);

// Wi-Fi with the last-used saved network. started: this call turned it on.
Result wifiUp(bool& started);
void wifiDown();

// Desk is closing: a job in progress gives up at its next step (the Wi-Fi wait, the clock sync,
// each request) instead of holding the exit until it is done. A cancelled step fails.
void setCancel(bool on);
bool cancelled();

// The last weather request: its HTTP status (0: none, the connection or TLS failed) and the
// heap as it started, for the battery log.
struct FetchInfo {
  int status = 0;
  int freeKb = -1;
  int maxBlockKb = -1;
};
FetchInfo lastFetchInfo();

// keepWifi: leave Wi-Fi on afterwards (on USB, for the next PC poll) even if this call
// started it.
Result refresh(Config& cfg, desk::WeatherReport& out, uint32_t nowUnix, bool keepWifi);
const char* resultText(Result r);  // Vietnamese, for the card

}  // namespace deskweather

#endif  // CROSSINK_DESK
