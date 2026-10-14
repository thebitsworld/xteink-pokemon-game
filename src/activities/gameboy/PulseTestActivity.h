#pragma once

#include <atomic>
#include <cstdint>

#include "activities/Activity.h"
#include "util/PulsePlanner.h"

// Applications > Pulse test (X4 Pro, UC8279-0x68 screens only): tries the
// pulse drive the Game Boy may use next (HalDisplay::pulseScan) on fixed
// pictures, before any game does. The ladder test gives eight boxes 1 to 8
// pulses toward black on white, and eight toward white on black, so the tester
// sees how dark one pulse gets and how many a full change takes, and whether
// the rest of the screen stays still. The motion test moves a black and a
// white square for ten seconds with the pulse budgets set here and counts the
// scans per second; it plans the next scan while the waveform runs, as a game
// would. The timing sweep times scans of one small box at several frame
// rates (PLL) and frame counts, to split each scan into its fixed part and
// its part per frame. Results show on screen and go to
// /.crosspoint/pulse-test.txt.
class PulseTestActivity final : public Activity {
 public:
  PulseTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PulseTest", renderer, mappedInput) {}

  // Whether this reader's screen can run the test.
  static bool supported();

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return running.load(std::memory_order_acquire); }

 private:
  enum class Row : uint8_t { Frames, Pll, Black, White, Ladder, Motion, Sweep, Back, Count };
  enum class Run : uint8_t { None, Ladder, Motion, Sweep };

  // Scan timings summed over a test, in microseconds.
  struct Totals {
    uint32_t scans = 0;
    uint64_t sendUs = 0;
    uint64_t setupUs = 0;
    uint64_t waveUs = 0;
    uint64_t planUs = 0;
    uint64_t bytes = 0;
    uint64_t elapsedUs = 0;
  };

  uint8_t frames = 2;
  uint8_t pll = 0x0F;  // the frame rate register (0x30) for the scans
  uint8_t blackPulses = 8;
  uint8_t whitePulses = 8;
  int selected = static_cast<int>(Row::Ladder);
  Run pending = Run::None;
  std::atomic<bool> running{false};
  std::atomic<bool> abortRun{false};
  bool showingResult = false;
  bool pulsed = false;  // the panel holds pulse masks: the next refresh must be full
  char resultLines[8][96] = {};
  int resultTop = 600;  // where the results start on screen

  uint8_t* target = nullptr;  // the picture the panel heads to (framebuffer layout)
  uint8_t* drive = nullptr;   // this scan's drive mask
  uint8_t* plannerStorage = nullptr;
  PulsePlanner planner;
  uint32_t bufferSize = 0;
  uint16_t widthBytes = 0;
  uint16_t rows = 0;

  void rowRect(int row, int& x, int& y, int& w, int& h) const;
  void drawMenu();
  void activate(Row row);
  bool allocate();
  void release();
  void beginDrive();
  void endDrive();
  bool scan(const PulsePlanner::Window& w, Totals& totals, bool wait = true);
  bool maskWindow(PulsePlanner::Window& w) const;
  void captureDrive(void (*draw)(GfxRenderer&, int), int arg);
  void showWithPulses(Totals& totals);
  void runLadder();
  void runMotion();
  void runSweep();
  void drawResultArea();
  void logResult(const char* test, const Totals& totals) const;
  void summarize(const char* test, const Totals& totals, int line);
};
