#include "Pomodoro.h"

namespace desk {

uint32_t Pomodoro::phaseLengthSec() const {
  switch (phase_) {
    case Phase::Focus:
      return cfg.focusMin * 60u;
    case Phase::ShortBreak:
      return cfg.shortBreakMin * 60u;
    case Phase::LongBreak:
      return cfg.longBreakMin * 60u;
    case Phase::Idle:
      break;
  }
  return cfg.focusMin * 60u;
}

void Pomodoro::enter(const Phase p, const uint32_t nowSec) {
  phase_ = p;
  paused_ = false;
  endsAt_ = nowSec + phaseLengthSec();
}

uint32_t Pomodoro::remainingSec(const uint32_t nowSec) const {
  if (phase_ == Phase::Idle) return phaseLengthSec();
  if (paused_) return leftAtPause_;
  return endsAt_ > nowSec ? endsAt_ - nowSec : 0;
}

void Pomodoro::toggle(const uint32_t nowSec) {
  if (phase_ == Phase::Idle) {
    enter(Phase::Focus, nowSec);
    return;
  }
  if (paused_) {
    paused_ = false;
    endsAt_ = nowSec + leftAtPause_;
  } else {
    leftAtPause_ = remainingSec(nowSec);
    paused_ = true;
  }
}

void Pomodoro::finishPhase(const uint32_t nowSec, const bool completed) {
  if (phase_ == Phase::Focus) {
    if (completed) {
      ++focusRoundsToday_;
      focusMinutesToday_ += cfg.focusMin;
    }
    enter(round_ >= cfg.rounds ? Phase::LongBreak : Phase::ShortBreak, nowSec);
  } else if (phase_ == Phase::LongBreak) {
    round_ = 1;
    enter(Phase::Focus, nowSec);
  } else {
    ++round_;
    enter(Phase::Focus, nowSec);
  }
}

void Pomodoro::skip(const uint32_t nowSec) {
  if (phase_ == Phase::Idle) return;
  finishPhase(nowSec, false);
}

void Pomodoro::reset() {
  phase_ = Phase::Idle;
  paused_ = false;
  round_ = 1;
}

Pomodoro::Event Pomodoro::update(const uint32_t nowSec) {
  if (!running() || nowSec < endsAt_) return Event::None;
  finishPhase(nowSec, true);
  return Event::PhaseEnded;
}

const char* pomodoroPhaseLabel(const Pomodoro::Phase phase) {
  switch (phase) {
    case Pomodoro::Phase::Focus:
      return "Tập trung";
    case Pomodoro::Phase::ShortBreak:
      return "Nghỉ ngắn";
    case Pomodoro::Phase::LongBreak:
      return "Nghỉ dài";
    case Pomodoro::Phase::Idle:
      break;
  }
  return "Sẵn sàng";
}

}  // namespace desk
