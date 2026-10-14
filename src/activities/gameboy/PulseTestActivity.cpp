#if CROSSINK_GAMEBOY
#include "PulseTestActivity.h"

#include <AppVersion.h>
#include <Arduino.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>

#ifndef SIMULATOR
#include <BoardConfig.h>
#include <esp_heap_caps.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "fontIds.h"

namespace {

constexpr char TAG[] = "PULSE";
constexpr char kLogPath[] = "/.crosspoint/pulse-test.txt";
constexpr uint8_t kBudgetChoices[] = {2, 4, 6, 8, 10, 12, 15};
constexpr uint32_t kMotionUs = 10000000;  // the motion test runs ten seconds
constexpr int kMotionSpeed = 300;         // pixels per second
constexpr int kSquare = 64;
constexpr int kResultTop = 600;  // the results area at the bottom of the screen

// The ladder's sixteen boxes: eight black-going on white (top band), eight
// white-going on black (bottom band); box i gets i + 1 pulses.
struct Ladder {
  int boxW, boxH, gap, left, topA, bandB0, bandB1, topB;
};

Ladder ladderLayout(const GfxRenderer& r) {
  Ladder l{};
  const int w = r.getScreenWidth();
  l.gap = 8;
  l.boxW = (w - 32 - 7 * l.gap) / 8;
  l.left = (w - (8 * l.boxW + 7 * l.gap)) / 2;
  l.boxH = 140;
  l.topA = 90;
  l.bandB0 = 300;
  l.bandB1 = 520;
  l.topB = 340;
  return l;
}

int boxX(const Ladder& l, const int i) { return l.left + i * (l.boxW + l.gap); }

// Draws, black on white, the interiors of the ladder boxes still driven in
// scan `from` (boxes from - 1 .. 7): the drive mask of that scan.
void drawLadderDrive(GfxRenderer& r, const int from) {
  const Ladder l = ladderLayout(r);
  for (int i = from - 1; i < 8; ++i) {
    r.fillRect(boxX(l, i), l.topA, l.boxW, l.boxH, true);
    r.fillRect(boxX(l, i), l.topB, l.boxW, l.boxH, true);
  }
}

}  // namespace

bool PulseTestActivity::supported() {
#ifdef SIMULATOR
  return false;
#else
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279 &&
         BoardConfig::ACTIVE.displayControllerVariant == 0x68;
#endif
}

void PulseTestActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void PulseTestActivity::onExit() {
  endDrive();
  release();
  Activity::onExit();
}

bool PulseTestActivity::allocate() {
  if (target) return true;
#ifdef SIMULATOR
  return false;
#else
  bufferSize = static_cast<uint32_t>(renderer.getBufferSize());
  widthBytes = display.getDisplayWidthBytes();
  rows = display.getDisplayHeight();
  target = static_cast<uint8_t*>(heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  drive = static_cast<uint8_t*>(heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  plannerStorage = static_cast<uint8_t*>(heap_caps_malloc(5 * bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!target || !drive || !plannerStorage) {
    LOG_ERR(TAG, "no memory for %u-byte buffers", static_cast<unsigned>(bufferSize));
    release();
    return false;
  }
  memset(drive, 0, bufferSize);
  return true;
#endif
}

void PulseTestActivity::release() {
#ifndef SIMULATOR
  heap_caps_free(target);
  heap_caps_free(drive);
  heap_caps_free(plannerStorage);
#endif
  target = drive = plannerStorage = nullptr;
}

// The Game Boy's fast settings: frame rate 0x0F, display SPI at 20 MHz.
void PulseTestActivity::beginDrive() {
#ifndef SIMULATOR
  display.setFastRefreshDuScale(0);
  display.setFastRefreshDiffWindow(false);
  display.setFastRefreshTuning(0, 0x0F);
  display.setDisplaySpiHz(20000000);
#endif
}

void PulseTestActivity::endDrive() {
#ifndef SIMULATOR
  if (pulsed) display.endPulseDrive();
  pulsed = false;
  display.setFastRefreshTuning(0, 0);
  display.setDisplaySpiHz(0);
#endif
}

// One pulse scan of `drive` toward `target`, with its timings added up.
bool PulseTestActivity::scan(const PulsePlanner::Window& w, Totals& totals) {
#ifdef SIMULATOR
  (void)w;
  (void)totals;
  return false;
#else
  const uint8_t* picture = renderer.getFrameBuffer();
  if (!display.pulseScan(picture, drive, w.row0, w.row1, w.byte0, w.byte1, frames)) return false;
  pulsed = true;
  uint32_t ph[8];
  if (display.lastRefreshTimings(ph)) {
    totals.sendUs += ph[1];
    totals.setupUs += ph[2];
    totals.waveUs += ph[3];
    totals.bytes += ph[5];
  }
  ++totals.scans;
  return true;
#endif
}

// The rectangle (framebuffer rows and bytes) of the set bits of `drive`.
bool PulseTestActivity::maskWindow(PulsePlanner::Window& w) const {
  int r0 = -1, r1 = -1, b0 = widthBytes, b1 = -1;
  for (uint16_t r = 0; r < rows; ++r) {
    const uint8_t* row = drive + static_cast<uint32_t>(r) * widthBytes;
    for (uint16_t i = 0; i < widthBytes; ++i) {
      if (!row[i]) continue;
      if (r0 < 0) r0 = r;
      r1 = r;
      b0 = std::min<int>(b0, i);
      b1 = std::max<int>(b1, i);
    }
  }
  if (r0 < 0) return false;
  w.row0 = static_cast<uint16_t>(r0);
  w.row1 = static_cast<uint16_t>(r1);
  w.byte0 = static_cast<uint16_t>(b0);
  w.byte1 = static_cast<uint16_t>(b1);
  return true;
}

// Builds `drive` from shapes drawn black (1 = drive) without touching the
// picture in the framebuffer.
void PulseTestActivity::captureDrive(void (*draw)(GfxRenderer&, int), const int arg) {
  uint8_t* fb = renderer.getFrameBuffer();
  memcpy(target, fb, bufferSize);
  renderer.clearScreen(0xFF);
  draw(renderer, arg);
  for (uint32_t i = 0; i < bufferSize; ++i) drive[i] = static_cast<uint8_t>(~fb[i]);
  memcpy(fb, target, bufferSize);
}

// Brings the panel to the framebuffer's picture with planned pulses.
void PulseTestActivity::showWithPulses(Totals& totals) {
  planner.setBudgets(blackPulses, whitePulses);
  planner.retarget(renderer.getFrameBuffer());
  PulsePlanner::Window w;
  while (planner.plan(drive, w)) {
    if (!scan(w, totals)) return;
    planner.delivered(drive, w);
  }
}

void PulseTestActivity::runLadder() {
  const Ladder l = ladderLayout(renderer);
  const int w = renderer.getScreenWidth();
  // The base picture, cleaned with an ordinary full refresh.
  renderer.clearScreen(0xFF);
  char title[64];
  snprintf(title, sizeof(title), "Ladder: 1-8 pulses of %u frame%s", frames, frames == 1 ? "" : "s");
  renderer.drawText(UI_12_FONT_ID, 16, 20, title, true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, 16, 56, "To black on white:", true);
  renderer.fillRect(8, l.bandB0, w - 16, l.bandB1 - l.bandB0, true);
  renderer.drawText(UI_10_FONT_ID, 16, l.bandB0 + 8, "To white on black:", false);
  char label[4];
  for (int i = 0; i < 8; ++i) {
    snprintf(label, sizeof(label), "%d", i + 1);
    const int lx = boxX(l, i) + (l.boxW - renderer.getTextWidth(UI_10_FONT_ID, label)) / 2;
    renderer.drawRect(boxX(l, i) - 2, l.topA - 2, l.boxW + 4, l.boxH + 4, true);
    renderer.drawText(UI_10_FONT_ID, lx, l.topA + l.boxH + 8, label, true);
    renderer.drawRect(boxX(l, i) - 2, l.topB - 2, l.boxW + 4, l.boxH + 4, false);
    renderer.drawText(UI_10_FONT_ID, lx, l.topB + l.boxH + 8, label, false);
  }
#ifndef SIMULATOR
  if (pulsed) display.endPulseDrive();
  pulsed = false;
#endif
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);

  // The target: every box changed; scan k drives the boxes k..8.
  for (int i = 0; i < 8; ++i) {
    renderer.fillRect(boxX(l, i), l.topA, l.boxW, l.boxH, true);
    renderer.fillRect(boxX(l, i), l.topB, l.boxW, l.boxH, false);
  }
  Totals totals;
  const uint32_t start = micros();
  for (int k = 1; k <= 8 && !abortRun.load(std::memory_order_acquire); ++k) {
    captureDrive(drawLadderDrive, k);
    PulsePlanner::Window win;
    if (!maskWindow(win) || !scan(win, totals)) break;
  }
  totals.elapsedUs = micros() - start;
  planner.init(plannerStorage, widthBytes, rows, renderer.getFrameBuffer());
  summarize("ladder", totals, 0);
  resultLines[1][0] = '\0';
  logResult("ladder", totals);
}

void PulseTestActivity::runMotion() {
  const int w = renderer.getScreenWidth();
  const int a0 = 90, a1 = 290, b0 = 320, b1 = 520;
  renderer.clearScreen(0xFF);
  char title[64];
  snprintf(title, sizeof(title), "Motion: %u/%u pulses of %u frame%s", blackPulses, whitePulses, frames,
           frames == 1 ? "" : "s");
  renderer.drawText(UI_12_FONT_ID, 16, 20, title, true, EpdFontFamily::BOLD);
  renderer.drawRect(8, a0, w - 16, a1 - a0, true);
  renderer.fillRect(8, b0, w - 16, b1 - b0, true);
#ifndef SIMULATOR
  if (pulsed) display.endPulseDrive();
  pulsed = false;
#endif
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  planner.init(plannerStorage, widthBytes, rows, renderer.getFrameBuffer());
  planner.setBudgets(blackPulses, whitePulses);

  Totals totals;
  const int span = w - 16 - 2 * 8 - kSquare;
  const uint32_t start = micros();
  PulsePlanner::Window win;
  while (!abortRun.load(std::memory_order_acquire)) {
    const uint32_t elapsed = micros() - start;
    if (elapsed >= kMotionUs) break;
    // Ping-pong across the band; the white square runs the other way.
    const int travel = static_cast<int>(static_cast<uint64_t>(elapsed) * kMotionSpeed / 1000000) % (2 * span);
    const int x = 16 + (travel < span ? travel : 2 * span - travel);
    renderer.fillRect(9, a0 + 1, w - 18, a1 - a0 - 2, false);
    renderer.fillRect(x, (a0 + a1 - kSquare) / 2, kSquare, kSquare, true);
    renderer.fillRect(8, b0, w - 16, b1 - b0, true);
    renderer.fillRect(w - kSquare - x, (b0 + b1 - kSquare) / 2, kSquare, kSquare, false);
    const uint32_t planStart = micros();
    planner.retarget(renderer.getFrameBuffer());
    const bool any = planner.plan(drive, win);
    totals.planUs += micros() - planStart;
    if (!any) continue;
    if (!scan(win, totals)) break;
    planner.delivered(drive, win);
  }
  totals.elapsedUs = micros() - start;
  summarize("motion", totals, 0);
  // The pulses still due, counted apart.
  Totals rest;
  while (planner.plan(drive, win) && scan(win, rest)) planner.delivered(drive, win);
  logResult("motion", totals);
}

void PulseTestActivity::summarize(const char* test, const Totals& t, const int line) {
  const float n = t.scans ? static_cast<float>(t.scans) : 1.0f;
  const float seconds = t.elapsedUs / 1e6f;
  snprintf(resultLines[line], sizeof(resultLines[line]), "%s: %u scans, %.1f/s, %.1f ms each", test,
           static_cast<unsigned>(t.scans), seconds > 0 ? t.scans / seconds : 0.0f,
           t.scans ? t.elapsedUs / 1000.0f / n : 0.0f);
  snprintf(resultLines[2], sizeof(resultLines[2]), "send %.1f  setup %.1f  wave %.1f  plan %.1f ms", t.sendUs / 1000.0f / n,
           t.setupUs / 1000.0f / n, t.waveUs / 1000.0f / n, t.planUs / 1000.0f / n);
  snprintf(resultLines[3], sizeof(resultLines[3]), "%.1f KB a plane on average", t.bytes / 1024.0f / n);
}

void PulseTestActivity::logResult(const char* test, const Totals& t) const {
  const float n = t.scans ? static_cast<float>(t.scans) : 1.0f;
  const float seconds = t.elapsedUs / 1e6f;
  char line[200];
  const int len = snprintf(line, sizeof(line), "%s,%s,%u,%u,%u,%u,%.2f,%.1f,%.2f,%.2f,%.2f,%.2f,%.1f\n",
                           AppVersion::version(), test, frames, blackPulses, whitePulses,
                           static_cast<unsigned>(t.scans), seconds, seconds > 0 ? t.scans / seconds : 0.0f,
                           t.sendUs / 1000.0f / n, t.setupUs / 1000.0f / n, t.waveUs / 1000.0f / n,
                           t.planUs / 1000.0f / n, t.bytes / 1024.0f / n);
  if (len <= 0 || static_cast<size_t>(len) >= sizeof(line)) return;
  static constexpr char kHeader[] =
      "# version,test,frames,black_pulses,white_pulses,scans,seconds,scans_per_s,send_ms,setup_ms,wave_ms,plan_ms,"
      "plane_kb\n";
  HalFile file = Storage.open(kLogPath, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return;
  if (file.size() == 0) file.write(kHeader, sizeof(kHeader) - 1);
  if (file.size() < 64 * 1024) file.write(line, static_cast<size_t>(len));
  file.close();
}

// The results under the test picture, drawn with pulses (the panel is in
// pulse mode until the menu comes back).
void PulseTestActivity::drawResultArea() {
  const int w = renderer.getScreenWidth();
  renderer.fillRect(0, kResultTop, w, renderer.getScreenHeight() - kResultTop, false);
  int y = kResultTop + 8;
  for (const auto& line : resultLines) {
    if (!line[0]) continue;
    renderer.drawText(UI_10_FONT_ID, 16, y, line, true);
    y += 30;
  }
  renderer.drawText(UI_10_FONT_ID, 16, y + 10, "Tap or Select: menu.  Back: leave.", true, EpdFontFamily::BOLD);
  Totals ignored;
  showWithPulses(ignored);
}

void PulseTestActivity::rowRect(const int row, int& x, int& y, int& w, int& h) const {
  x = 16;
  w = renderer.getScreenWidth() - 32;
  h = 64;
  y = 230 + row * (h + 8);
}

void PulseTestActivity::drawMenu() {
  renderer.clearScreen(0xFF);
  renderer.drawText(UI_12_FONT_ID, 16, 20, "Pulse test", true, EpdFontFamily::BOLD);
  static const char* const kIntro[] = {
      "Tries the per-pixel pulse drive on this screen.",
      "Ladder: boxes get 1 to 8 pulses. Look at how dark",
      "each gets and that nothing else changes.",
      "Motion: squares move for ten seconds.",
      "Results: /.crosspoint/pulse-test.txt",
  };
  int y = 64;
  for (const char* line : kIntro) {
    renderer.drawText(UI_10_FONT_ID, 16, y, line, true);
    y += 28;
  }
  for (int row = 0; row < static_cast<int>(Row::Count); ++row) {
    char value[24] = "";
    const char* label = "";
    switch (static_cast<Row>(row)) {
      case Row::Frames:
        label = "Frames per pulse";
        snprintf(value, sizeof(value), "%u", frames);
        break;
      case Row::Black:
        label = "Pulses to black (motion)";
        snprintf(value, sizeof(value), "%u", blackPulses);
        break;
      case Row::White:
        label = "Pulses to white (motion)";
        snprintf(value, sizeof(value), "%u", whitePulses);
        break;
      case Row::Ladder:
        label = "Run the ladder test";
        break;
      case Row::Motion:
        label = "Run the motion test";
        break;
      case Row::Back:
        label = "Back";
        break;
      default:
        break;
    }
    int x, ry, w, h;
    rowRect(row, x, ry, w, h);
    const bool sel = row == selected;
    renderer.fillRect(x, ry, w, h, sel);
    renderer.drawRect(x, ry, w, h, true);
    const int ty = ry + h / 2 - 10;
    renderer.drawText(UI_10_FONT_ID, x + 12, ty, label, !sel, EpdFontFamily::BOLD);
    const int vw = renderer.getTextWidth(UI_10_FONT_ID, value);
    renderer.drawText(UI_10_FONT_ID, x + w - 12 - vw, ty, value, !sel);
  }
}

static uint8_t nextBudget(const uint8_t current) {
  for (const uint8_t choice : kBudgetChoices)
    if (choice > current) return choice;
  return kBudgetChoices[0];
}

void PulseTestActivity::activate(const Row row) {
  switch (row) {
    case Row::Frames:
      frames = static_cast<uint8_t>(frames % 3 + 1);
      break;
    case Row::Black:
      blackPulses = nextBudget(blackPulses);
      break;
    case Row::White:
      whitePulses = nextBudget(whitePulses);
      break;
    case Row::Ladder:
    case Row::Motion:
      pending = row == Row::Ladder ? Run::Ladder : Run::Motion;
      abortRun.store(false, std::memory_order_release);
      running.store(true, std::memory_order_release);
      break;
    case Row::Back:
      finish();
      return;
    default:
      break;
  }
  requestUpdate();
}

void PulseTestActivity::loop() {
  using Button = MappedInputManager::Button;
  if (running.load(std::memory_order_acquire)) {
    if (mappedInput.wasReleased(Button::Back)) abortRun.store(true, std::memory_order_release);
    return;
  }
  int tx = 0, ty = 0;
  const bool tapped = mappedInput.wasScreenTapped(tx, ty);
  if (showingResult) {
    if (mappedInput.wasReleased(Button::Back)) {
      finish();
    } else if (tapped || mappedInput.wasReleased(Button::Confirm)) {
      showingResult = false;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  const int count = static_cast<int>(Row::Count);
  if (mappedInput.wasReleased(Button::Up) || mappedInput.wasReleased(Button::Left)) {
    selected = (selected + count - 1) % count;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Down) || mappedInput.wasReleased(Button::Right)) {
    selected = (selected + 1) % count;
    requestUpdate();
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    activate(static_cast<Row>(selected));
  } else if (tapped) {
    for (int row = 0; row < count; ++row) {
      int x, y, w, h;
      rowRect(row, x, y, w, h);
      if (tx >= x && tx < x + w && ty >= y && ty < y + h) {
        selected = row;
        activate(static_cast<Row>(row));
        break;
      }
    }
  }
}

void PulseTestActivity::render(RenderLock&&) {
  if (pending != Run::None) {
    if (!supported() || !allocate()) {
      pending = Run::None;
      running.store(false, std::memory_order_release);
      snprintf(resultLines[0], sizeof(resultLines[0]), "This screen cannot run the pulse test.");
      resultLines[1][0] = resultLines[2][0] = resultLines[3][0] = '\0';
    } else {
      beginDrive();
      memset(resultLines, 0, sizeof(resultLines));
      if (pending == Run::Ladder) {
        runLadder();
      } else {
        runMotion();
      }
      pending = Run::None;
      drawResultArea();
      running.store(false, std::memory_order_release);
      showingResult = true;
      return;
    }
  }
  drawMenu();
#ifndef SIMULATOR
  if (pulsed) {
    display.endPulseDrive();
    pulsed = false;
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    return;
  }
#endif
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
#endif  // CROSSINK_GAMEBOY
