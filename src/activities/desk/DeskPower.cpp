#include "DeskPower.h"

#if CROSSINK_DESK

#include <Arduino.h>
#include <HalGPIO.h>
#include <HalStorage.h>

#include <cstdio>
#include <cstring>

#ifndef SIMULATOR
#include <BoardConfig.h>
#include <Wire.h>
#include <Rtc.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include <esp_private/esp_clk.h>
#include <esp_sleep.h>
#include <hal/wdt_hal.h>
#endif

namespace deskpower {

namespace {

constexpr uint32_t kChunkMs = 10000;
constexpr uint32_t kRunMagic = 0xDE5C0B01;
constexpr uint32_t kOpenDeskMagic = 0xDE5C0B02;

#ifndef SIMULATOR
RTC_NOINIT_ATTR uint32_t runMagic;
RTC_NOINIT_ATTR uint32_t runResets;
RTC_NOINIT_ATTR uint32_t openDeskMagic;

wdt_hal_context_t rwdt = RWDT_HAL_CONTEXT_DEFAULT();

uint32_t msToSlowTicks(const uint32_t ms) {
  // esp_clk_slowclk_cal_get(): microseconds per slow clock cycle, Q13.19.
  const uint64_t cal = esp_clk_slowclk_cal_get();
  if (cal == 0) return ms * 136;  // ~136 kHz internal RC
  return static_cast<uint32_t>((static_cast<uint64_t>(ms) * 1000ULL << 19) / cal);
}

void holdPins(const bool hold) {
  const auto& b = BoardConfig::ACTIVE;
  const int8_t pins[] = {b.display.sclk, b.display.mosi, b.display.cs, b.display.dc,
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
#endif

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

constexpr char kBatteryLogPath[] = "/.crosspoint/desk-battery.csv";
constexpr size_t kMaxBatteryLog = 512 * 1024;

}  // namespace

bool externalPower() {
#ifndef SIMULATOR
  return gpio.isUsbConnected() || usb_serial_jtag_is_connected();
#else
  return gpio.isUsbConnected();
#endif
}

GaugeSample readGauge() {
  GaugeSample g;
#ifndef SIMULATOR
  const uint8_t addr = BoardConfig::ACTIVE.batteryGauge.gaugeAddr;
  if (addr == 0) return g;
  uint16_t v = 0;
  if (readReg16(addr, 0x2C, v)) g.soc = v;  // StateOfCharge
  if (readReg16(addr, 0x08, v)) g.mv = v;   // Voltage
  if (readReg16(addr, 0x0C, v)) g.curMa = static_cast<int16_t>(v);  // Current
#endif
  return g;
}

void logBattery(const char* event, const char* localTime, const GaugeSample& g, const uint32_t resets) {
  char line[128];
  const int len = snprintf(line, sizeof(line), "%s,%s,%d,%d,%d,%lu\n", localTime, event, g.soc, g.mv, g.curMa,
                           static_cast<unsigned long>(resets));
  if (len <= 0 || static_cast<size_t>(len) >= sizeof(line)) return;
  HalFile file = Storage.open(kBatteryLogPath, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return;
  if (file.size() == 0) {
    static constexpr char kHeader[] = "# time,event,soc,mv,cur_ma,resets\n";
    file.write(kHeader, sizeof(kHeader) - 1);
  }
  if (file.size() < kMaxBatteryLog) file.write(line, static_cast<size_t>(len));
  file.close();
}

void watchdogArm(const uint32_t timeoutMs) {
#ifndef SIMULATOR
  wdt_hal_init(&rwdt, WDT_RWDT, 0, false);
  wdt_hal_write_protect_disable(&rwdt);
  // Stage 0 resets the digital system (RTC memory kept: the run resumes); stage 1, 10 s later,
  // resets the RTC domain too in case stage 0 cannot.
  wdt_hal_config_stage(&rwdt, WDT_STAGE0, msToSlowTicks(timeoutMs), WDT_STAGE_ACTION_RESET_SYSTEM);
  wdt_hal_config_stage(&rwdt, WDT_STAGE1, msToSlowTicks(10000), WDT_STAGE_ACTION_RESET_RTC);
  wdt_hal_enable(&rwdt);
  wdt_hal_write_protect_enable(&rwdt);
#else
  (void)timeoutMs;
#endif
}

void watchdogFeed() {
#ifndef SIMULATOR
  wdt_hal_write_protect_disable(&rwdt);
  wdt_hal_feed(&rwdt);
  wdt_hal_write_protect_enable(&rwdt);
#endif
}

void watchdogDisarm() {
#ifndef SIMULATOR
  wdt_hal_write_protect_disable(&rwdt);
  wdt_hal_disable(&rwdt);
  wdt_hal_write_protect_enable(&rwdt);
#endif
}

Wake sleepFor(const uint32_t ms) {
#ifndef SIMULATOR
  const int8_t pin = BoardConfig::ACTIVE.input.power;
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  const auto powerPin = static_cast<gpio_num_t>(pin);
  // A press still held would wake the sleep at once.
  if (pin >= 0) {
    const uint32_t t0 = millis();
    while (digitalRead(pin) == (activeHigh ? HIGH : LOW) && millis() - t0 < 3000) delay(10);
  }
  const bool gpioWake = pin >= 0 && gpio_intr_disable(powerPin) == ESP_OK;
  if (gpioWake) {
    gpio_wakeup_enable(powerPin, activeHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }
  esp_sleep_enable_gpio_switch(false);
  holdPins(true);
  Wake result = Wake::Timer;
  const uint32_t start = millis();
  while (millis() - start + 20 < ms) {
    const uint32_t left = ms - (millis() - start);
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(left < kChunkMs ? left : kChunkMs) * 1000ULL);
    watchdogFeed();
    const esp_err_t err = esp_light_sleep_start();
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
      result = Wake::PowerButton;
      break;
    }
    if (err != ESP_OK) delay(50);  // rejected: a wake source was pending; try again
  }
  holdPins(false);
  esp_sleep_enable_gpio_switch(true);
  if (gpioWake) {
    gpio_wakeup_disable(powerPin);
    gpio_set_intr_type(powerPin, GPIO_INTR_ANYEDGE);  // InputWake's own setting
    gpio_intr_enable(powerPin);
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  watchdogFeed();
  return result;
#else
  delay(ms);
  return Wake::Timer;
#endif
}

uint32_t msToNextMinute() {
#ifndef SIMULATOR
  static freeink::Rtc rtc;
  static bool begun = false;
  if (!begun) begun = rtc.begin();
  freeink::Rtc::DateTime dt;
  if (begun && rtc.now(dt) && dt.second < 60) return (60u - dt.second) * 1000u + 300u;
#endif
  return 60000;
}

bool batteryRunArmed() {
#ifndef SIMULATOR
  return runMagic == kRunMagic;
#else
  return false;
#endif
}

void setBatteryRunArmed(const bool armed) {
#ifndef SIMULATOR
  if (armed && runMagic != kRunMagic) runResets = 0;
  runMagic = armed ? kRunMagic : 0;
#else
  (void)armed;
#endif
}

uint32_t batteryResets() {
#ifndef SIMULATOR
  return runMagic == kRunMagic ? runResets : 0;
#else
  return 0;
#endif
}

void countBatteryReset() {
#ifndef SIMULATOR
  ++runResets;
#endif
}

void requestOpenDeskAfterRestart() {
#ifndef SIMULATOR
  openDeskMagic = kOpenDeskMagic;
#endif
}

bool consumeOpenDeskRequest() {
#ifndef SIMULATOR
  const bool requested = openDeskMagic == kOpenDeskMagic;
  openDeskMagic = 0;
  return requested;
#else
  return false;
#endif
}

}  // namespace deskpower

#endif  // CROSSINK_DESK
