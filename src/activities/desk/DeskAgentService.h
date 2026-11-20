#pragma once

#if CROSSINK_DESK

#include <cstdint>

#include "AgentStats.h"
#include "DeskWeatherService.h"

// Desk mode's PC poll (docs/development/desk-mode-plan.md, phase 3): GET /v1/stats from the
// agent in tools/desk-agent, at the address in desk.json ("agent": "host:port") or found by
// mDNS (_xdesk._tcp, remembered until a poll fails). Blocking.
namespace deskagent {

deskweather::Result poll(const deskweather::Config& cfg, desk::AgentStats& out, uint32_t nowUnix, bool keepWifi);

}  // namespace deskagent

#endif  // CROSSINK_DESK
