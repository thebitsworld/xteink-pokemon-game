#include "DeskWeatherService.h"

#if CROSSINK_DESK

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "DeskPower.h"
#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"
#include "network/WifiUtils.h"

namespace deskweather {

namespace {

constexpr char TAG[] = "DESKWX";
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr size_t kMaxBody = 8 * 1024;
constexpr int kFetchAttempts = 3;       // forecast tries per update
constexpr uint32_t kRetryGapMs = 3000;  // between them

void copyText(char* dst, const size_t size, const char* src) {
  std::snprintf(dst, size, "%s", src == nullptr ? "" : src);
}

std::atomic<bool> cancelFlag{false};
FetchInfo lastInfo;

bool fetch(const std::string& url, std::string& body) {
  body.clear();
  // The heap as the request starts: an HTTPS handshake needs a large free block, and on the X3
  // forecasts failed in ~2 s for hours while the ones right after a restart worked (battery log,
  // 2026-11-23).
#ifndef SIMULATOR
  lastInfo.freeKb = static_cast<int>(ESP.getFreeHeap() / 1024);
  lastInfo.maxBlockKb = static_cast<int>(ESP.getMaxAllocHeap() / 1024);
#endif
  HttpRequestDiagnostics diag;
  HttpDownloader::DownloadOptions options(false, false, [] { return cancelFlag.load(); });
  const auto err = HttpDownloader::streamUrl(
      url,
      [&body](const uint8_t* data, const size_t len) {
        body.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      nullptr, "", "", std::move(options), &diag);
  lastInfo.status = diag.status;
  const bool ok = err == HttpDownloader::OK;
  if (!ok) {
    LOG_ERR(TAG, "Fetch failed (error %d, HTTP %d, heap %d KB, block %d KB): %s", static_cast<int>(err), diag.status,
            lastInfo.freeKb, lastInfo.maxBlockKb, url.c_str());
  }
  if (body.size() > kMaxBody) body.resize(kMaxBody);
  return ok && !body.empty();
}

}  // namespace

FetchInfo lastFetchInfo() { return lastInfo; }

void setCancel(const bool on) { cancelFlag = on; }
bool cancelled() { return cancelFlag; }

Result wifiUp(bool& started) {
  started = false;
#ifdef SIMULATOR
  return Result::Ok;  // the simulator uses the host's network directly
#endif
  if (hasActiveStationWifiConnection()) return Result::Ok;
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  const auto credential = ssid.empty() ? std::nullopt : WIFI_STORE.findCredential(ssid);
  if (!credential) return Result::NoSavedWifi;
  WiFi.mode(WIFI_STA);
  WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
  started = true;
  const uint32_t t0 = millis();
  while (!hasActiveStationWifiConnection() && millis() - t0 < kWifiTimeoutMs && !cancelled()) delay(50);
  if (!hasActiveStationWifiConnection()) {
    LOG_ERR(TAG, "Wi-Fi %s did not connect", ssid.c_str());
    return Result::WifiFailed;
  }
  LOG_INF(TAG, "Wi-Fi up in %lu ms", static_cast<unsigned long>(millis() - t0));
  return Result::Ok;
}

void wifiDown() {
  WiFi.disconnect(true);
  delay(30);
  WiFi.mode(WIFI_OFF);
}

bool loadConfig(Config& out) {
  out = Config{};
  std::string text;
  if (!Storage.readFileToString(TAG, kConfigPath, 2048, text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  copyText(out.city, sizeof(out.city), doc["city"].as<const char*>());
  copyText(out.resolvedFor, sizeof(out.resolvedFor), doc["resolvedFor"].as<const char*>());
  copyText(out.agent, sizeof(out.agent), doc["agent"].as<const char*>());
  const char* unit = doc["unit"].as<const char*>();
  out.fahrenheit = unit != nullptr && (unit[0] == 'F' || unit[0] == 'f');
  if (!doc["lat"].isNull() && !doc["lon"].isNull()) {
    out.place.valid = true;
    out.place.lat = doc["lat"].as<double>();
    out.place.lon = doc["lon"].as<double>();
    copyText(out.place.name, sizeof(out.place.name), doc["place"].as<const char*>());
  }
  return true;
}

bool saveConfig(const Config& cfg) {
  JsonDocument doc;
  // City, PC address and unit are settings now (Settings > System > Desk): desk.json keeps the
  // resolved place only.
  doc["resolvedFor"] = cfg.resolvedFor;
  if (cfg.place.valid) {
    doc["lat"] = cfg.place.lat;
    doc["lon"] = cfg.place.lon;
    doc["place"] = cfg.place.name;
  }
  String text;
  serializeJsonPretty(doc, text);
  return Storage.writeFile(kConfigPath, text);
}

bool loadCache(desk::WeatherReport& out) {
  std::string text;
  if (!Storage.readFileToString(TAG, kCachePath, kMaxBody + 32, text)) return false;
  const size_t nl = text.find('\n');
  if (nl == std::string::npos) return false;
  char* end = nullptr;
  const uint32_t fetchedAt = static_cast<uint32_t>(std::strtoul(text.c_str(), &end, 10));
  long aqi = -1;
  long pm25 = -1;
  if (end != nullptr && *end == ' ') {
    aqi = std::strtol(end, &end, 10);
    if (end != nullptr && *end == ' ') pm25 = std::strtol(end, &end, 10);
  }
  if (!desk::parseForecast(text.c_str() + nl + 1, text.size() - nl - 1, fetchedAt, out)) return false;
  out.aqi = static_cast<int16_t>(aqi);
  out.pm25 = static_cast<int16_t>(pm25);
  return true;
}

Result refresh(Config& cfg, desk::WeatherReport& out, const uint32_t nowUnix, const bool keepWifi) {
  bool started = false;
  Result r = wifiUp(started);
  if (r != Result::Ok) {
    if (started) wifiDown();
    return r;
  }
  if (keepWifi) started = false;  // leave it up for the next round
  const auto giveUp = [&]() {
    if (started) wifiDown();
    return Result::FetchFailed;
  };
  if (cancelled()) return giveUp();

  // Set the clock while Wi-Fi is up: on entry, then every 6 hours. A Desk is a clock first.
  static uint32_t lastNtpMs = 0;
  static bool ntpDone = false;
  if (!ntpDone || millis() - lastNtpMs > 6UL * 3600UL * 1000UL) {
    if (cancelled()) return giveUp();
    if (halClock.syncFromNTP()) {
      if (!SETTINGS.clockHasBeenSynced || !SETTINGS.clockDateHasBeenSynced) {
        SETTINGS.clockHasBeenSynced = 1;
        SETTINGS.clockDateHasBeenSynced = 1;
        SETTINGS.saveToFile();
      }
      LOG_INF(TAG, "Clock set from NTP");
    }
    ntpDone = true;
    lastNtpMs = millis();
  }

  std::string body;
  const bool placeStale = !cfg.place.valid || std::strcmp(cfg.city, cfg.resolvedFor) != 0;
  if (cancelled()) return giveUp();
  if (placeStale) {
    desk::Place place;
    const bool byName = cfg.city[0] != '\0';
    const bool ok = byName ? fetch(desk::geocodingUrl(cfg.city), body) &&
                                 desk::parseGeocoding(body.data(), body.size(), place)
                           : fetch(desk::kIpLocateUrl, body) && desk::parseIpLocate(body.data(), body.size(), place);
    if (!ok) {
      if (started) wifiDown();
      return Result::PlaceFailed;
    }
    // Keep the name as typed when there is one: the geocoder may return a different spelling.
    if (byName) copyText(place.name, sizeof(place.name), cfg.city);
    cfg.place = place;
    copyText(cfg.resolvedFor, sizeof(cfg.resolvedFor), cfg.city);
    saveConfig(cfg);
    LOG_INF(TAG, "Place %s: %.3f, %.3f", cfg.place.name, cfg.place.lat, cfg.place.lon);
  }

  if (cancelled()) return giveUp();
  // The X3 took 3-4 tries for a forecast over plain HTTP (owner's report, 2026-11-25), and each
  // failed try on battery waits 15 minutes: try again at once, a few seconds apart.
  bool fetched = false;
  desk::WeatherReport report;
  bool parsed = false;
  for (int attempt = 0; attempt < kFetchAttempts && !cancelled(); ++attempt) {
    // Each try can take up to a request timeout: keep the battery loop's watchdog (60 s) fed.
    deskpower::watchdogFeed();
    if (attempt > 0) {
      LOG_INF(TAG, "Forecast try %d failed, again", attempt);
      const uint32_t t0 = millis();
      while (millis() - t0 < kRetryGapMs && !cancelled()) delay(50);
    }
    fetched = fetch(desk::forecastUrl(cfg.place.lat, cfg.place.lon), body);
    parsed = fetched && desk::parseForecast(body.data(), body.size(), nowUnix, report);
    if (parsed) break;
  }
  // Air quality (Settings > System > Desk): its own small request; a failure only leaves it out.
  int aqi = -1;
  int pm25 = -1;
  if (parsed && SETTINGS.deskAirQuality && !cancelled()) {
    std::string air;
    if (fetch(desk::airQualityUrl(cfg.place.lat, cfg.place.lon), air)) {
      desk::parseAirQuality(air.data(), air.size(), aqi, pm25);
    }
  }
  if (started) wifiDown();
  if (!fetched) return Result::FetchFailed;
  if (!parsed) return Result::ParseFailed;
  report.aqi = static_cast<int16_t>(aqi);
  report.pm25 = static_cast<int16_t>(pm25);
  out = report;

  // First line: fetched-at, AQI, PM2.5; then Open-Meteo's forecast as received.
  char header[40];
  std::snprintf(header, sizeof(header), "%lu %d %d\n", static_cast<unsigned long>(nowUnix), aqi, pm25);
  Storage.writeFile(kCachePath, String(header) + String(body.c_str()));
  return Result::Ok;
}

const char* resultText(const Result r) {
  switch (r) {
    case Result::Ok:
      return "";
    case Result::NoSavedWifi:
      return tr(STR_DESK_ERR_NO_WIFI);
    case Result::WifiFailed:
      return tr(STR_DESK_ERR_WIFI);
    case Result::PlaceFailed:
      return tr(STR_DESK_ERR_PLACE);
    case Result::FetchFailed:
      return tr(STR_DESK_ERR_FETCH);
    case Result::ParseFailed:
      return tr(STR_DESK_ERR_PARSE);
    case Result::NoAgent:
      return tr(STR_DESK_ERR_AGENT);
  }
  return "";
}

}  // namespace deskweather

#endif  // CROSSINK_DESK
