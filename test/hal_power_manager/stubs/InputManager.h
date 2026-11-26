#pragma once
#include <cstdint>
class InputManager {
 public:
  static constexpr uint8_t MAX_TOUCH_CONTACTS = 5;
  // HalGPIO.h forwards these inline (Desk touch wake, Picco power button).
  int8_t touchWakeIrqPin() const { return -1; }
  bool touchWakeIrqActiveLow() const { return true; }
  bool isPowerButtonPhysicallyPressed() const { return false; }
};
