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

#ifndef SIMULATOR
#include <BoardConfig.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#endif

#include <cstdio>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "WifiCredentialStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr char TAG[] = "DESKPROBE";
constexpr char kLogPath[] = "/.crosspoint/desk-probe.csv";
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

void DeskProbeActivity::onEnter() {
  Activity::onEnter();
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

void DeskProbeActivity::runTick() {
  const uint32_t start = millis();
  ++tick;
  halfThisTick = (tick % kHalfRefreshEvery) == 1;
  if (!halClock.formatTime(timeText, sizeof(timeText), SETTINGS.clockUtcOffsetQ, false)) {
    snprintf(timeText, sizeof(timeText), "--:--");
  }
  gauge = readGauge();
  lastWifiMs = 0;
  lastWifiOk = false;
  if (wifiRounds && tick % kWifiEvery == 0) lastWifiMs = wifiRound();
  requestUpdateAndWait();  // render() times the refresh into lastRefreshMs
  const bool usb = gpio.isUsbConnected();
  lastAwakeMs = millis() - start;
  appendLog(usb);
  sleepUntilNextTick(start);
}

void DeskProbeActivity::sleepUntilNextTick(const uint32_t tickStartMs) {
#ifndef SIMULATOR
  const uint32_t elapsed = millis() - tickStartMs;
  const uint32_t sleepMs = elapsed < kTickMs ? kTickMs - elapsed : 1000;
  if (WiFi.getMode() != WIFI_MODE_NULL) WiFi.mode(WIFI_OFF);

  const auto powerPin = static_cast<gpio_num_t>(BoardConfig::ACTIVE.input.power);
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  const bool hasPowerPin = BoardConfig::ACTIVE.input.power >= 0;
  // A press still held (the one that started the run) would wake us at once.
  const uint32_t waitStart = millis();
  while (hasPowerPin && digitalRead(powerPin) == (activeHigh ? HIGH : LOW) && millis() - waitStart < 3000) delay(10);

  // Mask the normal edge interrupt first, as HalPowerManager::holdWhileUsbPowered() does, so
  // the level wake cannot trap the CPU in an interrupt storm.
  const bool gpioWake = hasPowerPin && gpio_intr_disable(powerPin) == ESP_OK;
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleepMs) * 1000ULL);
  if (gpioWake) {
    gpio_wakeup_enable(powerPin, activeHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }
  esp_light_sleep_start();
  lastWake = static_cast<int>(esp_sleep_get_wakeup_cause());
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
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    running = true;
    stoppedByButton = false;
    tick = 0;
    lastWake = 0;
    LOG_INF(TAG, "Run started, Wi-Fi rounds %s", wifiRounds ? "on" : "off");
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    wifiRounds = !wifiRounds;
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
  y += 56;

  if (running) {
    renderer.drawText(UI_10_FONT_ID, left, y, "Running. Unplug USB and leave it.", true, EpdFontFamily::BOLD);
    y += 32;
    renderer.drawText(UI_10_FONT_ID, left, y, "Power button stops the run.");
    y += 32;
    renderer.drawText(UI_10_FONT_ID, left, y, "Log: /.crosspoint/desk-probe.csv");
  } else {
    renderer.drawText(UI_10_FONT_ID, left, y, stoppedByButton ? "Stopped." : "Ready.", true, EpdFontFamily::BOLD);
    y += 32;
    renderer.drawText(UI_10_FONT_ID, left, y, "Confirm: start   Up/Down: Wi-Fi rounds");
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow("Back"), "Start", "Wi-Fi", "Wi-Fi");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  const uint32_t t0 = millis();
  renderer.displayBuffer(halfThisTick ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  lastRefreshMs = millis() - t0;
}

#endif  // CROSSINK_DESK_PROBE
