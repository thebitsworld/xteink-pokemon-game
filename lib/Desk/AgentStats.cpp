#include "AgentStats.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>

namespace desk {

namespace {

int clampPct(const int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

// "%.1f" with a decimal comma, and no ",0".
void decimal(const float v, char* out, const size_t size) {
  const long tenths = std::lround(v * 10.0f);
  if (tenths % 10 == 0) {
    std::snprintf(out, size, "%ld", tenths / 10);
  } else {
    std::snprintf(out, size, "%ld,%ld", tenths / 10, std::labs(tenths % 10));
  }
}

}  // namespace

bool parseAgentStats(const char* json, const size_t length, const uint32_t receivedAt, AgentStats& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  if (doc["v"].as<int>() != 1 || doc["cpu"].isNull()) return false;

  AgentStats s;
  s.valid = true;
  s.receivedAt = receivedAt;
  std::snprintf(s.host, sizeof(s.host), "%s", doc["host"].as<const char*>() ? doc["host"].as<const char*>() : "PC");
  s.cpuPct = clampPct(doc["cpu"].as<int>());
  s.ramUsedGb = doc["ram"][0].as<float>();
  s.ramTotalGb = doc["ram"][1].as<float>();
  for (const JsonVariantConst d : doc["disks"].as<JsonArrayConst>()) {
    if (s.diskCount >= kMaxAgentDisks) break;
    AgentDisk& disk = s.disks[s.diskCount++];
    std::snprintf(disk.name, sizeof(disk.name), "%s", d[0].as<const char*>() ? d[0].as<const char*>() : "?");
    disk.usedGb = d[1].as<float>();
    disk.totalGb = d[2].as<float>();
  }
  s.netDownBps = doc["net"][0].as<uint32_t>();
  s.netUpBps = doc["net"][1].as<uint32_t>();
  s.uptimeSec = doc["uptime"].as<uint32_t>();

  const JsonObjectConst claude = doc["claude"];
  if (!claude.isNull() && !claude["session"].isNull()) {
    s.claude.known = true;
    s.claude.sessionPct = clampPct(claude["session"][0].as<int>());
    s.claude.sessionReset = claude["session"][1].as<uint32_t>();
    s.claude.weekPct = clampPct(claude["week"][0].as<int>());
    s.claude.weekReset = claude["week"][1].as<uint32_t>();
  }
  out = s;
  return true;
}

int percentOf(const float used, const float total) {
  if (total <= 0) return 0;
  return clampPct(static_cast<int>(std::lround(used * 100.0f / total)));
}

void formatSize(const float usedGb, const float totalGb, char* out, const size_t outSize) {
  char a[16];
  char b[16];
  if (totalGb >= 1000.0f) {
    decimal(usedGb / 1000.0f, a, sizeof(a));
    decimal(totalGb / 1000.0f, b, sizeof(b));
    std::snprintf(out, outSize, "%s / %s TB", a, b);
  } else {
    decimal(usedGb, a, sizeof(a));
    decimal(totalGb, b, sizeof(b));
    std::snprintf(out, outSize, "%s / %s GB", a, b);
  }
}

void formatRate(const uint32_t bps, char* out, const size_t outSize) {
  char v[16];
  if (bps >= 1000000) {
    decimal(bps / 1000000.0f, v, sizeof(v));
    std::snprintf(out, outSize, "%s MB/s", v);
  } else if (bps >= 1000) {
    std::snprintf(out, outSize, "%lu KB/s", static_cast<unsigned long>(bps / 1000));
  } else {
    std::snprintf(out, outSize, "%lu B/s", static_cast<unsigned long>(bps));
  }
}

void formatUptime(const uint32_t seconds, char* out, const size_t outSize) {
  const uint32_t days = seconds / 86400;
  const uint32_t hours = (seconds % 86400) / 3600;
  const uint32_t minutes = (seconds % 3600) / 60;
  if (days > 0) {
    std::snprintf(out, outSize, "%lu ngày %lu giờ", static_cast<unsigned long>(days), static_cast<unsigned long>(hours));
  } else if (hours > 0) {
    std::snprintf(out, outSize, "%lu giờ %lu phút", static_cast<unsigned long>(hours),
                  static_cast<unsigned long>(minutes));
  } else {
    std::snprintf(out, outSize, "%lu phút", static_cast<unsigned long>(minutes));
  }
}

}  // namespace desk
