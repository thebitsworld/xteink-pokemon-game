#include <gtest/gtest.h>

#include <cstring>

#include "AgentStats.h"

TEST(AgentStats, ParsesAgentAnswer) {
  // Captured from tools/desk-agent/desk_agent.py on a Windows PC (2026-11-20).
  const char* json =
      R"({"v":1,"host":"PC-00273","ts":1795180480,"cpu":4,"ram":[13.8,31.7],"disks":[["C:",122.9,464.7]],)"
      R"("net":[482,352],"uptime":10358,"claude":null})";
  desk::AgentStats s;
  ASSERT_TRUE(desk::parseAgentStats(json, std::strlen(json), 99, s));
  EXPECT_TRUE(s.valid);
  EXPECT_EQ(s.receivedAt, 99u);
  EXPECT_STREQ(s.host, "PC-00273");
  EXPECT_EQ(s.cpuPct, 4);
  EXPECT_FLOAT_EQ(s.ramUsedGb, 13.8f);
  EXPECT_FLOAT_EQ(s.ramTotalGb, 31.7f);
  ASSERT_EQ(s.diskCount, 1);
  EXPECT_STREQ(s.disks[0].name, "C:");
  EXPECT_EQ(s.netDownBps, 482u);
  EXPECT_EQ(s.uptimeSec, 10358u);
  EXPECT_FALSE(s.claude.known);
}

TEST(AgentStats, ClaudeBlockAndLimits) {
  const char* json =
      R"({"v":1,"host":"H","cpu":140,"ram":[1,2],"disks":[["C:",1,2],["D:",1,2],["E:",1,2],["F:",1,2],["G:",1,2]],)"
      R"("net":[0,0],"uptime":0,"claude":{"session":[42,1795183200],"week":[18,1795417200]}})";
  desk::AgentStats s;
  ASSERT_TRUE(desk::parseAgentStats(json, std::strlen(json), 1, s));
  EXPECT_EQ(s.cpuPct, 100);  // clamped
  EXPECT_EQ(s.diskCount, desk::kMaxAgentDisks);
  ASSERT_TRUE(s.claude.known);
  EXPECT_EQ(s.claude.sessionPct, 42);
  EXPECT_EQ(s.claude.weekReset, 1795417200u);
}

TEST(AgentStats, RejectsOtherVersionsAndJunk) {
  desk::AgentStats s;
  const char* v2 = R"({"v":2,"cpu":1})";
  EXPECT_FALSE(desk::parseAgentStats(v2, std::strlen(v2), 1, s));
  EXPECT_FALSE(desk::parseAgentStats("<html>", 6, 1, s));
  EXPECT_FALSE(s.valid);
}

TEST(AgentStats, Formatting) {
  char buf[48];
  desk::formatSize(19.5f, 32.0f, buf, sizeof(buf));
  EXPECT_STREQ(buf, "19,5 / 32 GB");
  desk::formatSize(1638.0f, 2000.0f, buf, sizeof(buf));
  EXPECT_STREQ(buf, "1,6 / 2 TB");
  desk::formatSize(19.5f, 32.0f, buf, sizeof(buf), '.');
  EXPECT_STREQ(buf, "19.5 / 32 GB");
  desk::formatRate(2400000, buf, sizeof(buf), '.');
  EXPECT_STREQ(buf, "2.4 MB/s");
  desk::formatRate(2400000, buf, sizeof(buf));
  EXPECT_STREQ(buf, "2,4 MB/s");
  desk::formatRate(310000, buf, sizeof(buf));
  EXPECT_STREQ(buf, "310 KB/s");
  desk::formatRate(482, buf, sizeof(buf));
  EXPECT_STREQ(buf, "482 B/s");
  desk::formatUptime(274000, buf, sizeof(buf));
  EXPECT_STREQ(buf, "3 ngày 4 giờ");
  desk::formatUptime(10358, buf, sizeof(buf));
  EXPECT_STREQ(buf, "2 giờ 52 phút");
  desk::formatUptime(500, buf, sizeof(buf));
  EXPECT_STREQ(buf, "8 phút");
  EXPECT_EQ(desk::percentOf(13.8f, 31.7f), 44);
  EXPECT_EQ(desk::percentOf(1, 0), 0);
}
