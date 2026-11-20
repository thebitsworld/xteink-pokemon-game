#pragma once

// Pomodoro timer for Desk mode (docs/development/desk-mode-plan.md, phase 4): focus / short
// break, a long break after the last round, pause and resume, skip, reset, and today's totals.
// Pure state machine on a seconds clock the caller supplies (monotonic), tested on the host.

#include <cstdint>

namespace desk {

class Pomodoro {
 public:
  enum class Phase : uint8_t { Idle, Focus, ShortBreak, LongBreak };
  enum class Event : uint8_t { None, PhaseEnded };

  struct Config {
    uint16_t focusMin = 25;
    uint16_t shortBreakMin = 5;
    uint16_t longBreakMin = 15;
    uint8_t rounds = 4;  // focus rounds before the long break
  };

  Pomodoro() = default;
  explicit Pomodoro(const Config& c) : cfg(c) {}

  // Select: start from Idle, else pause / resume.
  void toggle(uint32_t nowSec);
  // Ends the current phase now and moves on (counts a focus round only if it ran its course).
  void skip(uint32_t nowSec);
  // Back to Idle at round 1; today's totals stay.
  void reset();
  // Call often: moves to the next phase when the current one is over.
  Event update(uint32_t nowSec);

  Phase phase() const { return phase_; }
  bool running() const { return phase_ != Phase::Idle && !paused_; }
  bool paused() const { return paused_; }
  int round() const { return round_; }  // 1..rounds within the current set
  int rounds() const { return cfg.rounds; }
  uint32_t remainingSec(uint32_t nowSec) const;
  uint32_t phaseLengthSec() const;
  // Whole minutes left, rounded up (what the screen shows): 25 right after starting.
  uint32_t remainingMinutes(uint32_t nowSec) const { return (remainingSec(nowSec) + 59) / 60; }
  int focusRoundsToday() const { return focusRoundsToday_; }
  uint32_t focusMinutesToday() const { return focusMinutesToday_; }
  // A new day clears today's totals.
  void newDay() {
    focusRoundsToday_ = 0;
    focusMinutesToday_ = 0;
  }

 private:
  Config cfg;
  Phase phase_ = Phase::Idle;
  bool paused_ = false;
  int round_ = 1;
  uint32_t endsAt_ = 0;     // while running
  uint32_t leftAtPause_ = 0;
  int focusRoundsToday_ = 0;
  uint32_t focusMinutesToday_ = 0;

  void enter(Phase p, uint32_t nowSec);
  void finishPhase(uint32_t nowSec, bool completed);
};

const char* pomodoroPhaseLabel(Pomodoro::Phase phase);  // "Tập trung", "Nghỉ ngắn", ...

}  // namespace desk
