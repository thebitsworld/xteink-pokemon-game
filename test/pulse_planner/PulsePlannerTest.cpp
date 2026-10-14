#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "src/util/PulsePlanner.h"

namespace {

constexpr uint16_t kWb = 4;  // 32 pixels wide
constexpr uint16_t kRows = 6;
constexpr uint32_t kSize = kWb * kRows;

void setPixel(std::vector<uint8_t>& img, const int x, const int y, const bool white) {
  const uint8_t bit = static_cast<uint8_t>(0x80 >> (x & 7));
  uint8_t& b = img[y * kWb + x / 8];
  b = white ? static_cast<uint8_t>(b | bit) : static_cast<uint8_t>(b & ~bit);
}

bool driven(const std::vector<uint8_t>& drive, const int x, const int y) {
  return drive[y * kWb + x / 8] & (0x80 >> (x & 7));
}

struct Rig {
  std::vector<uint8_t> storage = std::vector<uint8_t>(5 * kSize);
  std::vector<uint8_t> shown = std::vector<uint8_t>(kSize, 0xFF);
  std::vector<uint8_t> drive = std::vector<uint8_t>(kSize, 0);
  PulsePlanner planner;
  Rig(const uint8_t black, const uint8_t white) {
    planner.init(storage.data(), kWb, kRows, shown.data());
    planner.setBudgets(black, white);
  }
  // Plans and delivers one scan; false when nothing was left.
  bool scan(PulsePlanner::Window& w) {
    if (!planner.plan(drive.data(), w)) return false;
    planner.delivered(drive.data(), w);
    return true;
  }
};

}  // namespace

TEST(PulsePlanner, NothingToDriveForTheShownPicture) {
  Rig rig(8, 8);
  PulsePlanner::Window w;
  EXPECT_FALSE(rig.planner.busy());
  rig.planner.retarget(rig.shown.data());
  EXPECT_FALSE(rig.planner.plan(rig.drive.data(), w));
}

TEST(PulsePlanner, DrivesAChangedPixelForItsBudget) {
  Rig rig(5, 3);
  std::vector<uint8_t> target = rig.shown;
  setPixel(target, 10, 2, false);  // to black: 5 pulses
  rig.planner.retarget(target.data());
  EXPECT_EQ(rig.planner.pulsesLeft(10, 2), 5);
  PulsePlanner::Window w;
  for (int n = 0; n < 5; ++n) {
    ASSERT_TRUE(rig.scan(w)) << n;
    EXPECT_EQ(w.row0, 2);
    EXPECT_EQ(w.row1, 2);
    EXPECT_EQ(w.byte0, 1);
    EXPECT_EQ(w.byte1, 1);
    EXPECT_TRUE(driven(rig.drive, 10, 2));
    EXPECT_FALSE(driven(rig.drive, 11, 2));
  }
  EXPECT_FALSE(rig.scan(w));
  EXPECT_FALSE(rig.planner.busy());

  setPixel(target, 10, 2, true);  // back to white: 3 pulses
  rig.planner.retarget(target.data());
  int scans = 0;
  while (rig.scan(w)) ++scans;
  EXPECT_EQ(scans, 3);
}

TEST(PulsePlanner, AChangeMidwayRestartsOnlyThatPixel) {
  Rig rig(6, 6);
  std::vector<uint8_t> target = rig.shown;
  setPixel(target, 0, 0, false);
  setPixel(target, 31, 5, false);
  rig.planner.retarget(target.data());
  PulsePlanner::Window w;
  ASSERT_TRUE(rig.scan(w));
  ASSERT_TRUE(rig.scan(w));
  EXPECT_EQ(w.row0, 0);
  EXPECT_EQ(w.row1, 5);
  EXPECT_EQ(w.byte0, 0);
  EXPECT_EQ(w.byte1, 3);
  setPixel(target, 0, 0, true);  // reverses after 2 pulses
  rig.planner.retarget(target.data());
  EXPECT_EQ(rig.planner.pulsesLeft(0, 0), 6);
  EXPECT_EQ(rig.planner.pulsesLeft(31, 5), 4);
  int scans = 0;
  while (rig.scan(w)) ++scans;
  EXPECT_EQ(scans, 6);
}

TEST(PulsePlanner, BudgetsAreClampedAndWholeBytesWork) {
  Rig rig(0, 40);
  std::vector<uint8_t> target(kSize, 0x00);  // everything to black
  rig.planner.retarget(target.data());
  EXPECT_EQ(rig.planner.pulsesLeft(7, 3), 1);
  PulsePlanner::Window w;
  ASSERT_TRUE(rig.scan(w));
  EXPECT_EQ(w.row0, 0);
  EXPECT_EQ(w.row1, kRows - 1);
  EXPECT_EQ(w.byte1, kWb - 1);
  EXPECT_FALSE(rig.scan(w));
  rig.planner.retarget(rig.shown.data());  // everything back to white: 15
  int scans = 0;
  while (rig.scan(w)) ++scans;
  EXPECT_EQ(scans, 15);
}

TEST(PulsePlanner, WordAndBytePathsAgree) {
  // The same pictures through word-aligned buffers and through buffers one
  // byte off (the byte path) must give the same drives.
  constexpr uint16_t wb = 8;
  constexpr uint16_t rows = 5;
  constexpr uint32_t size = wb * rows;
  std::vector<uint8_t> storeA(5 * size + 8), storeB(5 * size + 8), shown(size + 8, 0xFF);
  std::vector<uint8_t> driveA(size + 8, 0), driveB(size + 8, 0), picture(size + 8, 0xFF);
  PulsePlanner a, b;
  a.init(storeA.data(), wb, rows, shown.data());
  b.init(storeB.data() + 1, wb, rows, shown.data() + 1);
  a.setBudgets(3, 5);
  b.setBudgets(3, 5);
  uint32_t seed = 12345;
  for (int step = 0; step < 200; ++step) {
    seed = seed * 1103515245u + 12345u;
    if (step % 3 == 0) {
      for (uint32_t i = 0; i < size; ++i) {
        seed = seed * 1103515245u + 12345u;
        if ((seed >> 24) < 40) picture[i] = static_cast<uint8_t>(seed >> 8);
      }
      a.retarget(picture.data());
      std::vector<uint8_t> shifted(size + 8);
      memcpy(shifted.data() + 1, picture.data(), size);
      b.retarget(shifted.data() + 1);
    }
    PulsePlanner::Window wa, wbw;
    const bool ga = a.plan(driveA.data(), wa);
    const bool gb = b.plan(driveB.data() + 1, wbw);
    ASSERT_EQ(ga, gb) << step;
    if (!ga) continue;
    EXPECT_EQ(wa.row0, wbw.row0);
    EXPECT_EQ(wa.row1, wbw.row1);
    EXPECT_EQ(wa.byte0, wbw.byte0);
    EXPECT_EQ(wa.byte1, wbw.byte1);
    for (uint16_t r = wa.row0; r <= wa.row1; ++r)
      for (uint16_t i = wa.byte0; i <= wa.byte1; ++i) ASSERT_EQ(driveA[r * wb + i], driveB[1 + r * wb + i]) << step;
    a.delivered(driveA.data(), wa);
    b.delivered(driveB.data() + 1, wbw);
  }
}
