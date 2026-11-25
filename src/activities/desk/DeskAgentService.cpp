#include "DeskAgentService.h"

#if CROSSINK_DESK

#include <Arduino.h>
#include <Logging.h>

#ifndef SIMULATOR
#include <ESPmDNS.h>
#endif

#include <cstdio>
#include <string>

#include "network/HttpDownloader.h"

namespace deskagent {

namespace {

constexpr char TAG[] = "DESKPC";
constexpr size_t kMaxBody = 2048;

// The address found by mDNS, kept across polls; cleared when a poll fails.
std::string discovered;

bool discover(std::string& hostPort) {
#ifndef SIMULATOR
  // mDNS only for the query: left running, its task and buffers stayed on the heap after Desk
  // closed (the X4 Pro's folder picker then reported a memory error, 2026-11-23).
  if (!MDNS.begin("xteink-desk")) return false;
  const int n = MDNS.queryService("xdesk", "tcp");
  bool found = false;
  if (n <= 0) {
    LOG_INF(TAG, "No _xdesk._tcp agent on the network");
  } else {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s:%u", MDNS.address(0).toString().c_str(), static_cast<unsigned>(MDNS.port(0)));
    hostPort = buf;
    LOG_INF(TAG, "Agent %s at %s", MDNS.hostname(0).c_str(), buf);
    found = true;
  }
  MDNS.end();
  return found;
#else
  (void)hostPort;
  return false;  // the simulator has no mDNS queries: use "agent" in desk.json
#endif
}

// The agent's "host:port": the configured one, or the one mDNS found.
bool agentAddress(const deskweather::Config& cfg, std::string& hostPort) {
  hostPort = cfg.agent;
  if (!hostPort.empty()) {
    // An address typed on the reader: "192.168.1.20" means the agent's default port, and a
    // pasted "http://.../" works too (owner's question, 2026-11-25).
    if (hostPort.rfind("http://", 0) == 0) hostPort.erase(0, 7);
    while (!hostPort.empty() && (hostPort.back() == '/' || hostPort.back() == ' ')) hostPort.pop_back();
    while (!hostPort.empty() && hostPort.front() == ' ') hostPort.erase(0, 1);
    if (!hostPort.empty() && hostPort.find(':') == std::string::npos) hostPort += ":8765";  // desk_agent.py's port
    if (!hostPort.empty()) return true;
  }
  if (discovered.empty() && !discover(discovered)) return false;
  hostPort = discovered;
  return true;
}

}  // namespace

deskweather::Result downloadPhoto(const deskweather::Config& cfg, const int index, const char* destPath,
                                  const bool keepWifi) {
  bool started = false;
  const deskweather::Result w = deskweather::wifiUp(started);
  if (w != deskweather::Result::Ok) {
    if (started) deskweather::wifiDown();
    return w;
  }
  if (keepWifi) started = false;
  std::string hostPort;
  if (!agentAddress(cfg, hostPort)) {
    if (started) deskweather::wifiDown();
    return deskweather::Result::NoAgent;
  }
  char path[40];
  std::snprintf(path, sizeof(path), "/v1/photos/%d.png", index);
  const auto err = HttpDownloader::downloadToFile("http://" + hostPort + path, destPath);
  if (started) deskweather::wifiDown();
  if (err != HttpDownloader::OK) {
    LOG_ERR(TAG, "Photo %d: download error %d", index, static_cast<int>(err));
    return deskweather::Result::FetchFailed;
  }
  return deskweather::Result::Ok;
}

deskweather::Result poll(const deskweather::Config& cfg, desk::AgentStats& out, const uint32_t nowUnix,
                         const bool keepWifi) {
  bool started = false;
  const deskweather::Result w = deskweather::wifiUp(started);
  if (w != deskweather::Result::Ok) {
    if (started) deskweather::wifiDown();
    return w;
  }
  if (keepWifi) started = false;
  if (deskweather::cancelled()) {  // Desk is closing: no mDNS query or request now
    if (started) deskweather::wifiDown();
    return deskweather::Result::NoAgent;
  }

  std::string hostPort;
  if (!agentAddress(cfg, hostPort)) {
    if (started) deskweather::wifiDown();
    return deskweather::Result::NoAgent;
  }

  std::string body;
  const bool ok = HttpDownloader::fetchUrl("http://" + hostPort + "/v1/stats", body);
  if (started) deskweather::wifiDown();
  if (!ok || body.empty()) {
    discovered.clear();  // the PC may have a new address: look again next time
    return deskweather::Result::NoAgent;
  }
  if (body.size() > kMaxBody) body.resize(kMaxBody);
  desk::AgentStats stats;
  if (!desk::parseAgentStats(body.data(), body.size(), nowUnix, stats)) return deskweather::Result::ParseFailed;
  out = stats;
  return deskweather::Result::Ok;
}

}  // namespace deskagent

#endif  // CROSSINK_DESK
