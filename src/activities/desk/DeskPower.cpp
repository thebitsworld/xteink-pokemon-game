#include "DeskPower.h"

#if CROSSINK_DESK

#include <Arduino.h>
#include <HalGPIO.h>
#include <HalStorage.h>

#include <cstdio>
#include <cstring>

#ifndef SIMULATOR
#include <BoardConfig.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <WiFi.h>
#include <Wire.h>
#include <Rtc.h>
#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include <esp_private/esp_clk.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/wdt_hal.h>
#endif

namespace deskpower {

namespace {

constexpr uint32_t kChunkMs = 10000;
constexpr uint32_t kRunMagic = 0xDE5C0B01;
constexpr uint32_t kOpenDeskMagic = 0xDE5C0B02;
constexpr uint32_t kSleepMagic = 0xDE5C0B03;  // in openDeskMagic: power off after the restart

#ifndef SIMULATOR
RTC_NOINIT_ATTR uint32_t runMagic;
RTC_NOINIT_ATTR uint32_t runResets;
RTC_NOINIT_ATTR uint32_t openDeskMagic;
RTC_NOINIT_ATTR uint32_t crumbWord;  // 0xC2B70000 | stage
constexpr uint32_t kCrumbMagic = 0xC2B70000;
// The battery loop's own state between deep-sleep ticks (see deepSleepTicks()).
constexpr uint32_t kTickMagic = 0xDE5C0B04;
RTC_NOINIT_ATTR uint32_t tickMagic;
RTC_NOINIT_ATTR TickState tickState;

wdt_hal_context_t rwdt = RWDT_HAL_CONTEXT_DEFAULT();
bool rwdtArmed = false;

// Light sleeps since the last takeSleepStats(): entered and returned OK, rejected, last error.
uint32_t sleepOk = 0;
uint32_t sleepRejected = 0;
int sleepLastErr = 0;
uint32_t sleptMs = 0;  // time spent in the OK sleeps
int sleepRejectCause = -1;  // esp_sleep_get_wakeup_cause() after the last rejected sleep

// Which task called esp_restart() during a battery run: the X3 restarted every 10-20 s inside
// its light sleep with a software-restart cause (battery log, 2026-11-25), and nothing in the
// loop restarts. Saved by a shutdown handler, logged by the next run's "resume" line.
constexpr uint32_t kRestartTaskMagic = 0xDE5C0B05;
RTC_NOINIT_ATTR uint32_t restartTaskMagic;
RTC_NOINIT_ATTR char restartTask[12];
bool shutdownHookAdded = false;

void onShutdown() {
  if (runMagic != kRunMagic) return;
  const char* name = pcTaskGetName(nullptr);
  std::snprintf(restartTask, sizeof(restartTask), "%s", name ? name : "?");
  restartTaskMagic = kRestartTaskMagic;
}

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
  auto set = [hold](const int8_t p) {
    if (p < 0) return;
    if (hold) {
      gpio_hold_en(static_cast<gpio_num_t>(p));
    } else {
      gpio_hold_dis(static_cast<gpio_num_t>(p));
    }
  };
  for (const int8_t p : pins) set(p);
  // Native SDMMC boards (X4 Pro, X4 Classic): the card's own lines.
  if (b.sdmmc.busWidth > 0) {
    for (const int8_t p : {b.sdmmc.clk, b.sdmmc.cmd, b.sdmmc.d0, b.sdmmc.d1, b.sdmmc.d2, b.sdmmc.d3}) set(p);
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

bool batteryLoopSupported() {
#if defined(CROSSINK_DESK_BATTERY_TEST)
  return true;  // a test build for a reader the loop is being tried on
#elif !defined(SIMULATOR)
  return gpio.deviceIsX3();
#else
  return false;
#endif
}

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
  if (BoardConfig::ACTIVE.batteryGauge.gaugeType == BoardConfig::GaugeType::Cw2017) {
    // CW2017: VCELL 0x02-0x03 big-endian, 14 bits of 312.5 uV; SOC 0x04 in %. No current reading.
    Wire.beginTransmission(addr);
    Wire.write(0x02);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(static_cast<uint16_t>(addr), static_cast<size_t>(3)) == 3) {
      const uint8_t hi = static_cast<uint8_t>(Wire.read());
      const uint8_t lo = static_cast<uint8_t>(Wire.read());
      g.soc = Wire.read();
      g.mv = static_cast<int>((((hi & 0x3F) << 8) | lo) * 5 / 16);
    }
    return g;
  }
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
  rwdtArmed = true;
#else
  (void)timeoutMs;
#endif
}

void watchdogFeed() {
#ifndef SIMULATOR
  if (!rwdtArmed) return;  // also called from the weather retries, armed or not
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
  rwdtArmed = false;
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
  // The touch controller's INT (X4 Pro test build): held asserted while a report waits, so an
  // asserted line now is a touch already.
  const int8_t touchPin = gpio.touchWakeIrqPin();
  const bool touchActiveLow = gpio.touchWakeIrqActiveLow();
  const auto touchGpio = static_cast<gpio_num_t>(touchPin);
  if (touchPin >= 0 && digitalRead(touchPin) == (touchActiveLow ? LOW : HIGH)) return Wake::Touch;
  const bool gpioWake = pin >= 0 && gpio_intr_disable(powerPin) == ESP_OK;
  if (gpioWake) gpio_wakeup_enable(powerPin, activeHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
  // A touch wake leaves the loop, which restarts the reader: the line's own interrupt setting
  // does not need restoring.
  const bool touchWake = touchPin >= 0 && gpio_intr_disable(touchGpio) == ESP_OK;
  if (touchWake) gpio_wakeup_enable(touchGpio, touchActiveLow ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  if (gpioWake || touchWake) esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_gpio_switch(false);
  holdPins(true);
  Wake result = Wake::Timer;
  const uint32_t start = millis();
  while (millis() - start + 20 < ms) {
    const uint32_t left = ms - (millis() - start);
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(left < kChunkMs ? left : kChunkMs) * 1000ULL);
    watchdogFeed();
    setCrumb(1);
    const uint32_t sleepStartMs = millis();
    const esp_err_t err = esp_light_sleep_start();
    setCrumb(2);
    if (err == ESP_OK) {
      ++sleepOk;
      sleptMs += millis() - sleepStartMs;  // a "sleep" that returns at once adds next to nothing
    } else {
      ++sleepRejected;
      sleepLastErr = static_cast<int>(err);
      // Which source was already pending: the X3 rejected every sleep (0x103) without it being
      // the power button (2026-11-25).
      sleepRejectCause = static_cast<int>(esp_sleep_get_wakeup_cause());
    }
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
      const bool powerDown = pin >= 0 && digitalRead(pin) == (activeHigh ? HIGH : LOW);
      result = touchWake && !powerDown ? Wake::Touch : Wake::PowerButton;
      break;
    }
    if (err != ESP_OK) {
      // Rejected: a wake source was pending; try again. The X3's battery runs drew about 22 mA
      // between ticks instead of 3 and short presses went unseen (2026-11-25), as if it kept
      // landing here: a press now counts here too.
      delay(50);
      if (pin >= 0 && digitalRead(pin) == (activeHigh ? HIGH : LOW)) {
        result = Wake::PowerButton;
        break;
      }
    }
  }
  holdPins(false);
  esp_sleep_enable_gpio_switch(true);
  if (gpioWake) {
    gpio_wakeup_disable(powerPin);
    gpio_set_intr_type(powerPin, GPIO_INTR_ANYEDGE);  // InputWake's own setting
    gpio_intr_enable(powerPin);
  }
  if (touchWake) gpio_wakeup_disable(touchGpio);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  watchdogFeed();
  return result;
#else
  delay(ms);
  return Wake::Timer;
#endif
}

uint32_t waitPowerButtonRelease() {
#ifndef SIMULATOR
  const int8_t pin = BoardConfig::ACTIVE.input.power;
  if (pin < 0) return 0;
  const int pressed = BoardConfig::ACTIVE.input.powerActiveHigh ? HIGH : LOW;
  const uint32_t t0 = millis();
  while (digitalRead(pin) == pressed && millis() - t0 < 5000) delay(10);
  const uint32_t held = millis() - t0;
  delay(50);  // let the contact settle
  return held;
#else
  return 0;
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
  if (armed && !shutdownHookAdded) shutdownHookAdded = esp_register_shutdown_handler(&onShutdown) == ESP_OK;
#else
  (void)armed;
#endif
}

void takeSleepStats(uint32_t& ok, uint32_t& rejected, int& lastErr, uint32_t& slept, int& rejectCause) {
#ifndef SIMULATOR
  ok = sleepOk;
  rejected = sleepRejected;
  lastErr = sleepLastErr;
  slept = sleptMs;
  rejectCause = sleepRejectCause;
  sleepOk = sleepRejected = sleptMs = 0;
  sleepLastErr = 0;
  sleepRejectCause = -1;
#else
  ok = rejected = slept = 0;
  lastErr = 0;
  rejectCause = -1;
#endif
}

bool takeRestartTask(char* out, const size_t size) {
#ifndef SIMULATOR
  if (restartTaskMagic != kRestartTaskMagic) return false;
  restartTaskMagic = 0;
  restartTask[sizeof(restartTask) - 1] = '\0';
  std::snprintf(out, size, "%s", restartTask);
  return true;
#else
  (void)out;
  (void)size;
  return false;
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

void setCrumb(const uint8_t stage) {
#ifndef SIMULATOR
  crumbWord = kCrumbMagic | stage;
#else
  (void)stage;
#endif
}

uint8_t lastCrumb() {
#ifndef SIMULATOR
  return (crumbWord & 0xFFFF0000u) == kCrumbMagic ? static_cast<uint8_t>(crumbWord & 0xFF) : 0;
#else
  return 0;
#endif
}

void releasePinHolds() {
#ifndef SIMULATOR
  holdPins(false);
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
  if (requested) openDeskMagic = 0;
  return requested;
#else
  return false;
#endif
}

bool deepSleepTicks() {
#if !defined(SIMULATOR) && CONFIG_IDF_TARGET_ESP32S3
  return true;
#else
  return false;
#endif
}

BootWake bootWake() {
#ifndef SIMULATOR
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return tickMagic == kTickMagic ? BootWake::Timer : BootWake::Reset;
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
    case ESP_SLEEP_WAKEUP_GPIO:
      return BootWake::PowerButton;
    default:
      return BootWake::Reset;
  }
#else
  return BootWake::Reset;
#endif
}

bool loadTickState(TickState& out) {
#ifndef SIMULATOR
  if (tickMagic != kTickMagic) return false;
  out = tickState;
  return true;
#else
  (void)out;
  return false;
#endif
}

void clearTickState() {
#ifndef SIMULATOR
  tickMagic = 0;
#endif
}

void deepSleepFor(const TickState& state, const uint32_t ms) {
#ifndef SIMULATOR
  tickState = state;
  tickMagic = kTickMagic;
  watchdogDisarm();  // it would keep counting through the sleep
  setCrumb(7);
  if (WiFi.getMode() != WIFI_MODE_NULL) WiFi.mode(WIFI_OFF);
  Storage.shutdown();
  display.deepSleep();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(ms) * 1000ULL);
  powerManager.startDeepSleep(gpio);  // also arms the power-button wake
#else
  (void)state;
  (void)ms;
#endif
  for (;;) {
    delay(1000);
  }
}

void requestSleepAfterRestart() {
#ifndef SIMULATOR
  openDeskMagic = kSleepMagic;
#endif
}

bool consumeSleepRequest() {
#ifndef SIMULATOR
  const bool requested = openDeskMagic == kSleepMagic;
  if (requested) openDeskMagic = 0;
  return requested;
#else
  return false;
#endif
}

}  // namespace deskpower

#endif  // CROSSINK_DESK
