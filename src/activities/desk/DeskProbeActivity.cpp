#include "DeskProbeActivity.h"

#if CROSSINK_DESK_PROBE

#include <AppVersion.h>
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <Wire.h>

#include <HalDisplay.h>
#include <HalPowerManager.h>

#ifndef SIMULATOR
#include <BoardConfig.h>
#include <driver/gpio.h>
#include <esp_private/esp_clk.h>
#include <esp_rtc_time.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <hal/wdt_hal.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr char TAG[] = "DESKPROBE";
constexpr char kLogPath[] = "/.crosspoint/desk-probe.csv";
constexpr char kTracePath[] = "/.crosspoint/desk-probe-trace.txt";
constexpr size_t kMaxTraceBytes = 256 * 1024;
constexpr uint32_t kTickMs = 60000;
constexpr uint32_t kHalfRefreshEvery = 60;  // ticks
constexpr uint32_t kWifiEvery = 30;         // ticks
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr size_t kMaxLogBytes = 512 * 1024;

// BQ27220 standard commands (TI TRM): little-endian 16-bit registers.
constexpr uint8_t kRegVoltage = 0x08;
constexpr uint8_t kRegCurrent = 0x0C;
constexpr uint8_t kRegRemainingCapacity = 0x10;
constexpr uint8_t kRegAverageCurrent = 0x14;
constexpr uint8_t kRegStateOfCharge = 0x2C;

#ifndef SIMULATOR
// Survive a software reset (not a power cut): set before light sleep, updated right after the
// wake before any SD or display access, so a hang after the wake can be told from no wake.
constexpr uint32_t kCrumbMagic = 0xDE5C0003;
RTC_NOINIT_ATTR uint32_t crumbMagic;
RTC_NOINIT_ATTR uint32_t crumbStage;  // 1 = entered light sleep, 2 = woke from it
RTC_NOINIT_ATTR uint32_t crumbTick;
RTC_NOINIT_ATTR uint32_t crumbMode;
RTC_NOINIT_ATTR uint32_t crumbWake;
RTC_NOINIT_ATTR uint32_t crumbSleepMs;

void holdPins(const bool hold) {
  const auto& b = BoardConfig::ACTIVE;
  const int8_t pins[] = {b.display.sclk, b.display.mosi, b.display.cs,   b.display.dc,
                         b.display.rst,  b.sd.cs,        b.sd.powerEnable};
  for (const int8_t p : pins) {
    if (p < 0) continue;
    if (hold) {
      gpio_hold_en(static_cast<gpio_num_t>(p));
    } else {
      gpio_hold_dis(static_cast<gpio_num_t>(p));
    }
  }
}

bool readReg16(const uint8_t addr, const uint8_t reg, uint16_t& out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<uint16_t>(addr), static_cast<size_t>(2)) != 2) return false;
  const uint8_t lo = static_cast<uint8_t>(Wire.read());
  const uint8_t hi = static_cast<uint8_t>(Wire.read());
  out = static_cast<uint16_t>(lo | (hi << 8));
  return true;
}
#endif

const char* wakeName(const int cause) {
#ifndef SIMULATOR
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return "timer";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "gpio";
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "none";
    default:
      break;
  }
#endif
  (void)cause;
  return "other";
}

}  // namespace

const char* DeskProbeActivity::modeName() const {
  switch (sleepMode) {
    case SleepMode::LightSleepKeepPins:
      return "light-keep";
    case SleepMode::LightSleep:
      return "light";
    case SleepMode::DeepSleep:
      return "deep";
    case SleepMode::LightWatchdog:
      return "light-wdt";
    case SleepMode::Delay:
      break;
  }
  return "delay";
}

void DeskProbeActivity::readBreadcrumbs() {
  lastRunNote[0] = '\0';
#ifndef SIMULATOR
  if (crumbMagic != kCrumbMagic) return;
  static const char* const kModes[] = {"light-keep", "light", "delay"};
  const char* mode = crumbMode < 3 ? kModes[crumbMode] : "?";
  if (crumbStage == 1) {
    snprintf(lastRunNote, sizeof(lastRunNote), "Last run (%s): slept at tick %lu, NO wake", mode,
             static_cast<unsigned long>(crumbTick));
  } else if (crumbStage == 2) {
    snprintf(lastRunNote, sizeof(lastRunNote), "Last run (%s): woke at tick %lu, cause %s", mode,
             static_cast<unsigned long>(crumbTick), wakeName(static_cast<int>(crumbWake)));
  }
  trace("previous-run", static_cast<long>(crumbStage * 1000 + crumbTick));
  crumbMagic = 0;
#endif
}

void DeskProbeActivity::onEnter() {
  Activity::onEnter();
  readBreadcrumbs();
  running = false;
  stoppedByButton = false;
  halfThisTick = true;
  gauge = readGauge();
  requestUpdate();
}

void DeskProbeActivity::onExit() {
  Activity::onExit();
  running = false;
}

DeskProbeActivity::Gauge DeskProbeActivity::readGauge() {
  Gauge g;
#ifndef SIMULATOR
  const uint8_t addr = BoardConfig::ACTIVE.batteryGauge.gaugeAddr;
  if (addr == 0) return g;
  uint16_t v = 0;
  bool ok = true;
  if (readReg16(addr, kRegStateOfCharge, v)) g.soc = v; else ok = false;
  if (readReg16(addr, kRegVoltage, v)) g.mv = v; else ok = false;
  if (readReg16(addr, kRegCurrent, v)) g.curMa = static_cast<int16_t>(v); else ok = false;
  if (readReg16(addr, kRegAverageCurrent, v)) g.avgMa = static_cast<int16_t>(v);
  if (readReg16(addr, kRegRemainingCapacity, v)) g.remMah = v;
  g.ok = ok;
#endif
  return g;
}

uint32_t DeskProbeActivity::wifiRound() {
  lastWifiOk = false;
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) return 0;
  const auto credential = WIFI_STORE.findCredential(ssid);
  if (!credential) return 0;
  const uint32_t t0 = millis();
  WiFi.mode(WIFI_STA);
  WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < kWifiTimeoutMs) delay(50);
  lastWifiOk = WiFi.status() == WL_CONNECTED;
  const uint32_t ms = millis() - t0;
  WiFi.disconnect(true);
  delay(30);
  WiFi.mode(WIFI_OFF);
  return ms;
}

void DeskProbeActivity::appendLog(const bool usb) const {
  static constexpr char kHeader[] =
      "# version,tick,uptime_s,clock,wake,awake_ms,refresh_ms,refresh,soc,mv,cur_ma,avg_ma,rem_mah,gauge_ok,usb,"
      "wifi_ms,wifi_ok\n";
  char line[200];
  const int len = snprintf(line, sizeof(line), "%s,%lu,%lu,%s,%s,%lu,%lu,%s,%d,%d,%d,%d,%d,%d,%d,%lu,%d\n",
                           AppVersion::version(), static_cast<unsigned long>(tick),
                           static_cast<unsigned long>(millis() / 1000), timeText, wakeName(lastWake),
                           static_cast<unsigned long>(lastAwakeMs), static_cast<unsigned long>(lastRefreshMs),
                           halfThisTick ? "half" : "fast", gauge.soc, gauge.mv, gauge.curMa, gauge.avgMa,
                           gauge.remMah, gauge.ok ? 1 : 0, usb ? 1 : 0, static_cast<unsigned long>(lastWifiMs),
                           lastWifiOk ? 1 : 0);
  if (len <= 0 || static_cast<size_t>(len) >= sizeof(line)) return;
  HalFile file = Storage.open(kLogPath, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR(TAG, "Cannot open %s", kLogPath);
    return;
  }
  if (file.size() == 0) file.write(kHeader, sizeof(kHeader) - 1);
  if (file.size() < kMaxLogBytes) file.write(line, static_cast<size_t>(len));
  file.close();
}

void DeskProbeActivity::trace(const char* step, const long value) const {
  char line[96];
  const int len = snprintf(line, sizeof(line), "%lu,%lu,%s,%s,%ld\n", static_cast<unsigned long>(millis()),
                           static_cast<unsigned long>(tick), modeName(), step, value);
  if (len <= 0 || static_cast<size_t>(len) >= sizeof(line)) return;
  HalFile file = Storage.open(kTracePath, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return;
  if (file.size() < kMaxTraceBytes) file.write(line, static_cast<size_t>(len));
  file.close();
}

void DeskProbeActivity::runTick() {
  const uint32_t start = millis();
  ++tick;
  trace("tick-start");
  halfThisTick = (tick % kHalfRefreshEvery) == 1;
  if (!halClock.formatTime(timeText, sizeof(timeText), SETTINGS.clockUtcOffsetQ, false)) {
    snprintf(timeText, sizeof(timeText), "--:--");
  }
  trace("clock-read");
  gauge = readGauge();
  trace("gauge-read", gauge.ok ? gauge.curMa : -9999);
  lastWifiMs = 0;
  lastWifiOk = false;
  if (wifiRounds && tick % kWifiEvery == 0) {
    lastWifiMs = wifiRound();
    trace("wifi-done", static_cast<long>(lastWifiMs));
  }
  trace("render-request");
  requestUpdateAndWait();  // render() times the refresh into lastRefreshMs
  trace("render-done", static_cast<long>(lastRefreshMs));
  const bool usb = gpio.isUsbConnected();
  lastAwakeMs = millis() - start;
  appendLog(usb);
  trace("log-written", usb ? 1 : 0);
  sleepUntilNextTick(start);
}

void DeskProbeActivity::sleepUntilNextTick(const uint32_t tickStartMs) {
#ifndef SIMULATOR
  const uint32_t elapsed = millis() - tickStartMs;
  // The first sleep is short so a failed wake shows within seconds.
  const uint32_t sleepMs = tick == 1 ? 10000 : (elapsed < kTickMs ? kTickMs - elapsed : 1000);
  if (WiFi.getMode() != WIFI_MODE_NULL) WiFi.mode(WIFI_OFF);

  if (sleepMode == SleepMode::Delay) {
    // Control run: wait awake in short steps, still stoppable with the power button.
    trace("delay-enter", static_cast<long>(sleepMs));
    // Input state is only refreshed by the main loop, which this wait blocks: read the power
    // pin directly.
    const int8_t pin = BoardConfig::ACTIVE.input.power;
    const int pressedLevel = BoardConfig::ACTIVE.input.powerActiveHigh ? HIGH : LOW;
    const uint32_t t0 = millis();
    while (millis() - t0 < sleepMs) {
      delay(100);
      if (pin >= 0 && digitalRead(pin) == pressedLevel) {
        running = false;
        stoppedByButton = true;
        trace("delay-stopped");
        requestUpdate();
        return;
      }
    }
    lastWake = 0;
    trace("delay-exit");
    return;
  }

  const auto powerPin = static_cast<gpio_num_t>(BoardConfig::ACTIVE.input.power);
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  const bool hasPowerPin = BoardConfig::ACTIVE.input.power >= 0;
  // A press still held (the one that started the run) would wake us at once.
  const uint32_t waitStart = millis();
  while (hasPowerPin && digitalRead(powerPin) == (activeHigh ? HIGH : LOW) && millis() - waitStart < 3000) delay(10);

  // Mask the normal edge interrupt first, as HalPowerManager::holdWhileUsbPowered() does, so
  // the level wake cannot trap the CPU in an interrupt storm.
  const bool gpioWake = hasPowerPin && gpio_intr_disable(powerPin) == ESP_OK;
  if (gpioWake) {
    gpio_wakeup_enable(powerPin, activeHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }
  const bool keepPins = sleepMode == SleepMode::LightSleepKeepPins;
  trace("sleep-enter", static_cast<long>(sleepMs));
  crumbMagic = kCrumbMagic;
  crumbStage = 1;
  crumbTick = tick;
  crumbMode = static_cast<uint32_t>(sleepMode);
  crumbSleepMs = sleepMs;
  if (keepPins) {
    esp_sleep_enable_gpio_switch(false);
    holdPins(true);
  }
  // v4: sleep in chunks of at most kChunkMs. Probe v3 woke from a 10 s light sleep but not
  // from a 59 s one (2026-11-20); chunks tell a duration limit from anything else.
  constexpr uint32_t kChunkMs = 10000;
  const uint32_t sleepStart = millis();
  uint32_t chunks = 0;
  uint32_t errors = 0;
  esp_err_t sleepErr = ESP_OK;
  lastWake = 0;
  while (millis() - sleepStart + 20 < sleepMs) {
    const uint32_t left = sleepMs - (millis() - sleepStart);
    const uint32_t chunk = left < kChunkMs ? left : kChunkMs;
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(chunk) * 1000ULL);
    sleepErr = esp_light_sleep_start();
    lastWake = static_cast<int>(esp_sleep_get_wakeup_cause());
    ++chunks;
    crumbStage = 2;
    crumbWake = static_cast<uint32_t>(lastWake);
    crumbSleepMs = chunks;  // after a wake: chunks done so far
    if (lastWake == ESP_SLEEP_WAKEUP_GPIO) break;
    if (sleepErr != ESP_OK) {
      ++errors;
      if (errors > 20) break;
      delay(50);  // rejected (e.g. a wake source already pending): try again shortly
    }
  }
  if (keepPins) {
    holdPins(false);
    esp_sleep_enable_gpio_switch(true);
  }
  trace("sleep-exit", static_cast<long>(lastWake));
  trace("sleep-chunks", static_cast<long>(chunks * 100 + errors));
  if (sleepErr != ESP_OK) trace("sleep-error", static_cast<long>(sleepErr));
  if (gpioWake) {
    gpio_wakeup_disable(powerPin);
    gpio_set_intr_type(powerPin, GPIO_INTR_ANYEDGE);  // InputWake's own setting
    gpio_intr_enable(powerPin);
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  if (lastWake == ESP_SLEEP_WAKEUP_GPIO) {
    running = false;
    stoppedByButton = true;
    LOG_INF(TAG, "Stopped by the power button after %lu ticks", static_cast<unsigned long>(tick));
    requestUpdate();
  }
#else
  (void)tickStartMs;
  delay(kTickMs);
#endif
}

void DeskProbeActivity::loop() {
  if (running) {
    runTick();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && sleepMode == SleepMode::DeepSleep) {
    deskprobe::startDeepRun(renderer);  // does not return
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && sleepMode == SleepMode::LightWatchdog) {
    deskprobe::startWatchdogRun(renderer);  // does not return
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    running = true;
    stoppedByButton = false;
    tick = 0;
    lastWake = 0;
    LOG_INF(TAG, "Run started, Wi-Fi rounds %s", wifiRounds ? "on" : "off");
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    wifiRounds = !wifiRounds;
    requestUpdate();
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    sleepMode = sleepMode == SleepMode::LightWatchdog      ? SleepMode::DeepSleep
                : sleepMode == SleepMode::DeepSleep          ? SleepMode::LightSleepKeepPins
                : sleepMode == SleepMode::LightSleepKeepPins ? SleepMode::LightSleep
                : sleepMode == SleepMode::LightSleep         ? SleepMode::Delay
                                                             : SleepMode::LightWatchdog;
    requestUpdate();
  }
}

void DeskProbeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int left = 24;
  int y = 40;
  char buf[96];

  renderer.drawCenteredText(UI_12_FONT_ID, y, "Desk probe", true, EpdFontFamily::BOLD);
  y += 60;
  renderer.drawCenteredText(UI_12_FONT_ID, y, timeText, true, EpdFontFamily::BOLD);
  y += 50;

  snprintf(buf, sizeof(buf), "Tick %lu   wake: %s", static_cast<unsigned long>(tick), wakeName(lastWake));
  renderer.drawText(UI_10_FONT_ID, left, y, buf);
  y += 32;
  snprintf(buf, sizeof(buf), "Awake %lu ms   refresh %lu ms (%s)", static_cast<unsigned long>(lastAwakeMs),
           static_cast<unsigned long>(lastRefreshMs), halfThisTick ? "half" : "fast");
  renderer.drawText(UI_10_FONT_ID, left, y, buf);
  y += 32;
  snprintf(buf, sizeof(buf), "Battery %d%%   %d mV   gauge %s", gauge.soc, gauge.mv, gauge.ok ? "ok" : "ERR");
  renderer.drawText(UI_10_FONT_ID, left, y, buf);
  y += 32;
  snprintf(buf, sizeof(buf), "Current %d mA   avg %d mA", gauge.curMa, gauge.avgMa);
  renderer.drawText(UI_10_FONT_ID, left, y, buf);
  y += 32;
  snprintf(buf, sizeof(buf), "Remaining %d mAh", gauge.remMah);
  renderer.drawText(UI_10_FONT_ID, left, y, buf);
  y += 32;
  snprintf(buf, sizeof(buf), "Wi-Fi rounds: %s   last %lu ms %s", wifiRounds ? "every 30 min" : "off",
           static_cast<unsigned long>(lastWifiMs), lastWifiMs == 0 ? "" : (lastWifiOk ? "ok" : "FAIL"));
  renderer.drawText(UI_10_FONT_ID, left, y, buf);
  y += 32;
  snprintf(buf, sizeof(buf), "Between ticks: %s",
           sleepMode == SleepMode::LightWatchdog        ? "LIGHT SLEEP + WATCHDOG"
           : sleepMode == SleepMode::DeepSleep          ? "DEEP SLEEP (reboot each tick)"
           : sleepMode == SleepMode::LightSleepKeepPins ? "LIGHT SLEEP, KEEP PINS"
           : sleepMode == SleepMode::LightSleep         ? "LIGHT SLEEP (plain)"
                                                        : "DELAY (control, no sleep)");
  renderer.drawText(UI_10_FONT_ID, left, y, buf, true, EpdFontFamily::BOLD);
  y += 32;
  if (lastRunNote[0] != '\0') renderer.drawText(UI_10_FONT_ID, left, y, lastRunNote);
  y += 40;

  if (running) {
    renderer.drawText(UI_10_FONT_ID, left, y, "Running. Unplug USB and leave it.", true, EpdFontFamily::BOLD);
    y += 32;
    renderer.drawText(UI_10_FONT_ID, left, y,
                      sleepMode == SleepMode::Delay ? "Hold the power button to stop."
                                                    : "Power button stops the run.");
    y += 32;
    renderer.drawText(UI_10_FONT_ID, left, y, "Log: /.crosspoint/desk-probe.csv");
  } else {
    renderer.drawText(UI_10_FONT_ID, left, y, stoppedByButton ? "Stopped." : "Ready.", true, EpdFontFamily::BOLD);
    y += 32;
    renderer.drawText(UI_10_FONT_ID, left, y, "Confirm: start   Up: Wi-Fi   Down: sleep mode");
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow("Back"), "Start", "Wi-Fi", "Mode");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  const uint32_t t0 = millis();
  renderer.displayBuffer(halfThisTick ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  lastRefreshMs = millis() - t0;
}

// ---- deep-sleep ticks (probe v5) ---------------------------------------------------------------

namespace deskprobe {

namespace {

constexpr char kDeepLogPath[] = "/.crosspoint/desk-probe-deep.csv";
constexpr uint32_t kDeepMagic = 0xDE5C0D05;

#ifndef SIMULATOR
// RTC slow memory keeps these through deep sleep; garbage after a power cut (magic check).
RTC_NOINIT_ATTR uint32_t deepMagic;
RTC_NOINIT_ATTR uint32_t deepTick;
RTC_NOINIT_ATTR uint32_t deepSleepMs;
RTC_NOINIT_ATTR uint64_t deepSleptAtUs;  // esp_rtc_get_time_us() when the last sleep began
#endif

void appendLine(const char* path, const char* header, const char* line) {
  HalFile file = Storage.open(path, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return;
  if (file.size() == 0 && header != nullptr) file.write(header, strlen(header));
  if (file.size() < kMaxLogBytes) file.write(line, strlen(line));
  file.close();
}

[[noreturn]] void runDeepTick(GfxRenderer& renderer, const bool first) {
#ifndef SIMULATOR
  // millis() counts from this boot's app start; the RTC time also covers the sleep and the
  // bootloader, so "late" is how long after the timer fired this code ran.
  const uint32_t hookMs = millis();
  const uint64_t nowUs = esp_rtc_get_time_us();
  long lateMs = 0;
  if (!first) {
    const uint64_t dueUs = deepSleptAtUs + static_cast<uint64_t>(deepSleepMs) * 1000ULL;
    lateMs = nowUs > dueUs ? static_cast<long>((nowUs - dueUs) / 1000ULL) : -1;
  }
  if (first) deepTick = 0;
  ++deepTick;
  gpio.stopButtonCapture();

  char timeText[16];
  if (!halClock.formatTime(timeText, sizeof(timeText), SETTINGS.clockUtcOffsetQ, false)) {
    snprintf(timeText, sizeof(timeText), "--:--");
  }
  const DeskProbeActivity::Gauge g = DeskProbeActivity::readGauge();
  const bool half = deepTick % kHalfRefreshEvery == 1;

  char buf[96];
  renderer.clearScreen();
  int y = 40;
  renderer.drawCenteredText(UI_12_FONT_ID, y, "Desk probe · deep sleep", true, EpdFontFamily::BOLD);
  y += 60;
  renderer.drawCenteredText(UI_12_FONT_ID, y, timeText, true, EpdFontFamily::BOLD);
  y += 50;
  snprintf(buf, sizeof(buf), "Tick %lu", static_cast<unsigned long>(deepTick));
  renderer.drawText(UI_10_FONT_ID, 24, y, buf);
  y += 32;
  snprintf(buf, sizeof(buf), "Boot to tick %lu ms   late %ld ms", static_cast<unsigned long>(hookMs), lateMs);
  renderer.drawText(UI_10_FONT_ID, 24, y, buf);
  y += 32;
  snprintf(buf, sizeof(buf), "Battery %d%%   %d mV   %d / %d mA", g.soc, g.mv, g.curMa, g.avgMa);
  renderer.drawText(UI_10_FONT_ID, 24, y, buf);
  y += 56;
  renderer.drawText(UI_10_FONT_ID, 24, y, "Unplug USB and leave it.", true, EpdFontFamily::BOLD);
  y += 32;
  renderer.drawText(UI_10_FONT_ID, 24, y, "Power button stops the run.");
  y += 32;
  renderer.drawText(UI_10_FONT_ID, 24, y, "Log: /.crosspoint/desk-probe-deep.csv");
  const uint32_t refreshStart = millis();
  renderer.displayBuffer(half ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  const uint32_t refreshMs = millis() - refreshStart;

  const uint32_t awakeMs = millis();
  char line[200];
  snprintf(line, sizeof(line), "%s,%lu,%s,%lu,%ld,%lu,%lu,%s,%d,%d,%d,%d,%d,%d\n", AppVersion::version(),
           static_cast<unsigned long>(deepTick), timeText, static_cast<unsigned long>(hookMs), lateMs,
           static_cast<unsigned long>(refreshMs), static_cast<unsigned long>(awakeMs), half ? "half" : "fast", g.soc,
           g.mv, g.curMa, g.avgMa, g.remMah, gpio.isUsbConnected() ? 1 : 0);
  appendLine(kDeepLogPath,
             "# version,tick,clock,boot_ms,late_ms,refresh_ms,awake_ms,refresh,soc,mv,cur_ma,avg_ma,rem_mah,usb\n",
             line);

  // Sleep for the rest of the minute: this boot's app time plus last tick's pre-app time.
  const long preApp = lateMs > 0 ? lateMs : 0;
  const long used = static_cast<long>(millis()) + preApp;
  deepSleepMs = used < 59000 ? static_cast<uint32_t>(kTickMs - used) : 1000;
  deepMagic = kDeepMagic;
  if (WiFi.getMode() != WIFI_MODE_NULL) WiFi.mode(WIFI_OFF);
  Storage.shutdown();
  display.deepSleep();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(deepSleepMs) * 1000ULL);
  deepSleptAtUs = esp_rtc_get_time_us();
  powerManager.startDeepSleep(gpio);  // also arms the power-button wake
#else
  (void)renderer;
  (void)first;
#endif
  for (;;) {
    delay(1000);
  }
}

// ---- light sleep + RTC watchdog (v6) ----

constexpr char kWdtLogPath[] = "/.crosspoint/desk-probe-wdt.csv";
constexpr uint32_t kWdtMagic = 0xDE5C0D06;
constexpr uint32_t kChunkMs = 10000;
constexpr uint32_t kWatchdogMs = 30000;  // > one chunk plus a tick's work

#ifndef SIMULATOR
RTC_NOINIT_ATTR uint32_t wdtMagic;
RTC_NOINIT_ATTR uint32_t wdtTick;
RTC_NOINIT_ATTR uint32_t wdtResets;  // watchdog (or other) resets the run came back from
RTC_NOINIT_ATTR uint32_t wdtStage;   // 1 = in a light sleep chunk, 2 = awake

wdt_hal_context_t rwdt = RWDT_HAL_CONTEXT_DEFAULT();

uint32_t msToSlowTicks(const uint32_t ms) {
  // esp_clk_slowclk_cal_get(): microseconds per slow clock cycle, Q13.19.
  const uint64_t cal = esp_clk_slowclk_cal_get();
  if (cal == 0) return ms * 136;  // ~136 kHz internal RC
  return static_cast<uint32_t>((static_cast<uint64_t>(ms) * 1000ULL << 19) / cal);
}

void watchdogArm() {
  wdt_hal_init(&rwdt, WDT_RWDT, 0, false);
  wdt_hal_write_protect_disable(&rwdt);
  // Stage 0 resets the digital system (RTC memory kept: the run resumes); stage 1, 10 s
  // later, resets the RTC domain too in case stage 0 cannot.
  wdt_hal_config_stage(&rwdt, WDT_STAGE0, msToSlowTicks(kWatchdogMs), WDT_STAGE_ACTION_RESET_SYSTEM);
  wdt_hal_config_stage(&rwdt, WDT_STAGE1, msToSlowTicks(10000), WDT_STAGE_ACTION_RESET_RTC);
  wdt_hal_enable(&rwdt);
  wdt_hal_write_protect_enable(&rwdt);
}

void watchdogFeed() {
  wdt_hal_write_protect_disable(&rwdt);
  wdt_hal_feed(&rwdt);
  wdt_hal_write_protect_enable(&rwdt);
}

void watchdogDisarm() {
  wdt_hal_write_protect_disable(&rwdt);
  wdt_hal_disable(&rwdt);
  wdt_hal_write_protect_enable(&rwdt);
}

const char* resetName(const esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_SW:
      return "sw";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "rtc_wdt";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    default:
      return "other";
  }
}

[[noreturn]] void stopWatchdogRun(const char* why) {
  watchdogDisarm();
  wdtMagic = 0;
  char line[96];
  snprintf(line, sizeof(line), "# stopped after tick %lu (%s), resets %lu\n", static_cast<unsigned long>(wdtTick), why,
           static_cast<unsigned long>(wdtResets));
  appendLine(kWdtLogPath, nullptr, line);
  esp_restart();
  for (;;) {
  }
}

[[noreturn]] void runWatchdogLoop(GfxRenderer& renderer) {
  gpio.stopButtonCapture();
  if (WiFi.getMode() != WIFI_MODE_NULL) WiFi.mode(WIFI_OFF);
  watchdogArm();
  const auto powerPin = static_cast<gpio_num_t>(BoardConfig::ACTIVE.input.power);
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  const bool hasPowerPin = BoardConfig::ACTIVE.input.power >= 0;
  if (hasPowerPin) {
    pinMode(powerPin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
    const uint32_t t0 = millis();
    while (digitalRead(powerPin) == (activeHigh ? HIGH : LOW) && millis() - t0 < 3000) delay(10);
  }

  for (;;) {
    const uint32_t tickStart = millis();
    watchdogFeed();
    wdtStage = 2;
    ++wdtTick;
    char timeText[16];
    if (!halClock.formatTime(timeText, sizeof(timeText), SETTINGS.clockUtcOffsetQ, false)) {
      snprintf(timeText, sizeof(timeText), "--:--");
    }
    const DeskProbeActivity::Gauge g = DeskProbeActivity::readGauge();
    const bool half = wdtTick % kHalfRefreshEvery == 1;

    char buf[96];
    renderer.clearScreen();
    int y = 40;
    renderer.drawCenteredText(UI_12_FONT_ID, y, "Desk probe · light + watchdog", true, EpdFontFamily::BOLD);
    y += 60;
    renderer.drawCenteredText(UI_12_FONT_ID, y, timeText, true, EpdFontFamily::BOLD);
    y += 50;
    snprintf(buf, sizeof(buf), "Tick %lu   watchdog resets %lu", static_cast<unsigned long>(wdtTick),
             static_cast<unsigned long>(wdtResets));
    renderer.drawText(UI_10_FONT_ID, 24, y, buf);
    y += 32;
    snprintf(buf, sizeof(buf), "Battery %d%%   %d mV   %d / %d mA", g.soc, g.mv, g.curMa, g.avgMa);
    renderer.drawText(UI_10_FONT_ID, 24, y, buf);
    y += 56;
    renderer.drawText(UI_10_FONT_ID, 24, y, "Unplug USB and leave it.", true, EpdFontFamily::BOLD);
    y += 32;
    renderer.drawText(UI_10_FONT_ID, 24, y, "Power button stops the run.");
    y += 32;
    renderer.drawText(UI_10_FONT_ID, 24, y, "Log: /.crosspoint/desk-probe-wdt.csv");
    const uint32_t refreshStart = millis();
    renderer.displayBuffer(half ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    const uint32_t refreshMs = millis() - refreshStart;

    char line[200];
    snprintf(line, sizeof(line), "%s,%lu,%s,%lu,%lu,%s,%lu,%d,%d,%d,%d,%d\n", AppVersion::version(),
             static_cast<unsigned long>(wdtTick), timeText, static_cast<unsigned long>(millis() - tickStart),
             static_cast<unsigned long>(refreshMs), half ? "half" : "fast", static_cast<unsigned long>(wdtResets),
             g.soc, g.mv, g.curMa, g.avgMa, gpio.isUsbConnected() ? 1 : 0);
    appendLine(kWdtLogPath, "# version,tick,clock,awake_ms,refresh_ms,refresh,resets,soc,mv,cur_ma,avg_ma,usb\n",
               line);

    // Light sleep for the rest of the minute in chunks, pins held, the watchdog fed per chunk.
    const uint32_t sleepStart = millis();
    const uint32_t used = sleepStart - tickStart;
    const uint32_t sleepMs = used < kTickMs - 1000 ? kTickMs - used : 1000;
    const bool gpioWake = hasPowerPin && gpio_intr_disable(powerPin) == ESP_OK;
    if (gpioWake) {
      gpio_wakeup_enable(powerPin, activeHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
      esp_sleep_enable_gpio_wakeup();
    }
    esp_sleep_enable_gpio_switch(false);
    holdPins(true);
    bool stop = false;
    while (millis() - sleepStart + 20 < sleepMs) {
      const uint32_t left = sleepMs - (millis() - sleepStart);
      esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(left < kChunkMs ? left : kChunkMs) * 1000ULL);
      watchdogFeed();
      wdtStage = 1;
      const esp_err_t err = esp_light_sleep_start();
      wdtStage = 2;
      if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        stop = true;
        break;
      }
      if (err != ESP_OK) delay(50);
    }
    holdPins(false);
    esp_sleep_enable_gpio_switch(true);
    if (gpioWake) gpio_wakeup_disable(powerPin);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (stop) stopWatchdogRun("power button");
  }
}
#endif

}  // namespace

bool bootContinuesRun(const int wakeupCause) {
#ifndef SIMULATOR
  return (deepMagic == kDeepMagic && wakeupCause == ESP_SLEEP_WAKEUP_TIMER) || wdtMagic == kWdtMagic;
#else
  (void)wakeupCause;
  return false;
#endif
}

void startWatchdogRun(GfxRenderer& renderer) {
#ifndef SIMULATOR
  wdtMagic = kWdtMagic;
  wdtTick = 0;
  wdtResets = 0;
  wdtStage = 2;
  runWatchdogLoop(renderer);
#else
  (void)renderer;
  for (;;) {
    delay(1000);
  }
#endif
}

bool handleBoot(const int wakeupCause, GfxRenderer& renderer) {
#ifndef SIMULATOR
  if (wdtMagic == kWdtMagic) {
    // Back from a reset inside a run (the RTC watchdog, a panic...): count it and go on.
    ++wdtResets;
    const esp_reset_reason_t reason = esp_reset_reason();
    char line[96];
    snprintf(line, sizeof(line), "# reset (%s) at tick %lu while %s\n", resetName(reason),
             static_cast<unsigned long>(wdtTick), wdtStage == 1 ? "sleeping" : "awake");
    appendLine(kWdtLogPath, nullptr, line);
    runWatchdogLoop(renderer);
  }
  if (deepMagic != kDeepMagic) return false;
  if (wakeupCause != ESP_SLEEP_WAKEUP_TIMER) {
    deepMagic = 0;  // any other wake ends the run
    char line[96];
    snprintf(line, sizeof(line), "# stopped after tick %lu (wake cause %d)\n", static_cast<unsigned long>(deepTick),
             wakeupCause);
    appendLine(kDeepLogPath, nullptr, line);
    return false;
  }
  runDeepTick(renderer, false);
#else
  (void)wakeupCause;
  (void)renderer;
  return false;
#endif
}

void startDeepRun(GfxRenderer& renderer) { runDeepTick(renderer, true); }

}  // namespace deskprobe

#endif  // CROSSINK_DESK_PROBE
