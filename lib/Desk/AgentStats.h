#pragma once

// The PC agent's GET /v1/stats answer (tools/desk-agent) for Desk mode's PC page
// (docs/development/desk-mode-plan.md, phase 3). Parsing and formatting only; tested on the
// host (test/desk).

#include <cstddef>
#include <cstdint>

namespace desk {

constexpr int kMaxAgentDisks = 4;

struct AgentDisk {
  char name[12] = {};
  float usedGb = 0;
  float totalGb = 0;
};

struct ClaudeUsage {
  bool known = false;
  int sessionPct = 0;
  uint32_t sessionReset = 0;  // Unix seconds, 0 = unknown
  int weekPct = 0;
  uint32_t weekReset = 0;
};

struct AgentStats {
  bool valid = false;
  uint32_t receivedAt = 0;  // reader's Unix time of the fetch
  char host[32] = {};
  int cpuPct = 0;
  float ramUsedGb = 0;
  float ramTotalGb = 0;
  int diskCount = 0;
  AgentDisk disks[kMaxAgentDisks];
  uint32_t netDownBps = 0;
  uint32_t netUpBps = 0;
  uint32_t uptimeSec = 0;
  ClaudeUsage claude;
};

bool parseAgentStats(const char* json, size_t length, uint32_t receivedAt, AgentStats& out);

// 0..100 from used/total; 0 when total is 0.
int percentOf(float used, float total);
// "19,5 / 32 GB", "1,6 / 2 TB" (TB from 1000 GB); decimalMark ',' (Vietnamese) or '.'.
void formatSize(float usedGb, float totalGb, char* out, size_t outSize, char decimalMark = ',');
// "2,4 MB/s", "310 KB/s", "0 B/s".
void formatRate(uint32_t bytesPerSecond, char* out, size_t outSize, char decimalMark = ',');
// "3 ngày 4 giờ", "5 giờ 12 phút", "8 phút".
void formatUptime(uint32_t seconds, char* out, size_t outSize);

}  // namespace desk
