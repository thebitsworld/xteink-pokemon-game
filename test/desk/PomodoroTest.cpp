#include <gtest/gtest.h>

#include "Pomodoro.h"

using desk::Pomodoro;

TEST(Pomodoro, StartsIdleWithFullFocusShown) {
  Pomodoro p;
  EXPECT_EQ(p.phase(), Pomodoro::Phase::Idle);
  EXPECT_FALSE(p.running());
  EXPECT_EQ(p.remainingMinutes(0), 25u);
  EXPECT_STREQ(desk::pomodoroPhaseLabel(p.phase()), "Sẵn sàng");
}

TEST(Pomodoro, FullCycleWithLongBreak) {
  Pomodoro p;
  uint32_t t = 1000;
  p.toggle(t);
  EXPECT_EQ(p.phase(), Pomodoro::Phase::Focus);
  EXPECT_EQ(p.remainingMinutes(t), 25u);
  EXPECT_EQ(p.remainingMinutes(t + 1), 25u);   // rounds up
  EXPECT_EQ(p.remainingMinutes(t + 61), 24u);
  for (int round = 1; round <= 4; ++round) {
    EXPECT_EQ(p.round(), round);
    EXPECT_EQ(p.update(t + 25 * 60 - 1), Pomodoro::Event::None);
    t += 25 * 60;
    EXPECT_EQ(p.update(t), Pomodoro::Event::PhaseEnded);
    if (round < 4) {
      EXPECT_EQ(p.phase(), Pomodoro::Phase::ShortBreak);
      t += 5 * 60;
    } else {
      EXPECT_EQ(p.phase(), Pomodoro::Phase::LongBreak);
      t += 15 * 60;
    }
    EXPECT_EQ(p.update(t), Pomodoro::Event::PhaseEnded);
  }
  EXPECT_EQ(p.phase(), Pomodoro::Phase::Focus);
  EXPECT_EQ(p.round(), 1);
  EXPECT_EQ(p.focusRoundsToday(), 4);
  EXPECT_EQ(p.focusMinutesToday(), 100u);
}

TEST(Pomodoro, PauseKeepsTheRemainingTime) {
  Pomodoro p;
  p.toggle(0);
  p.toggle(600);  // pause with 15 min left
  EXPECT_TRUE(p.paused());
  EXPECT_EQ(p.remainingSec(5000), 900u);
  EXPECT_EQ(p.update(5000), Pomodoro::Event::None);
  p.toggle(5000);  // resume
  EXPECT_EQ(p.remainingSec(5000), 900u);
  EXPECT_EQ(p.update(5899), Pomodoro::Event::None);
  EXPECT_EQ(p.update(5900), Pomodoro::Event::PhaseEnded);
}

TEST(Pomodoro, SkipDoesNotCountAndResetKeepsTotals) {
  Pomodoro p;
  p.toggle(0);
  p.skip(60);
  EXPECT_EQ(p.phase(), Pomodoro::Phase::ShortBreak);
  EXPECT_EQ(p.focusRoundsToday(), 0);
  p.skip(70);
  EXPECT_EQ(p.phase(), Pomodoro::Phase::Focus);
  EXPECT_EQ(p.round(), 2);
  p.update(70 + 25 * 60);
  EXPECT_EQ(p.focusRoundsToday(), 1);
  p.reset();
  EXPECT_EQ(p.phase(), Pomodoro::Phase::Idle);
  EXPECT_EQ(p.round(), 1);
  EXPECT_EQ(p.focusRoundsToday(), 1);
  p.newDay();
  EXPECT_EQ(p.focusRoundsToday(), 0);
}
