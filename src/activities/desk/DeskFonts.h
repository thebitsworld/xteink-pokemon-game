#pragma once

// Digit-only Inter Bold fonts for Desk mode (CROSSINK_DESK builds): characters
// " %-./0123456789:" and "°". Generated with lib/EpdFont/scripts/fontconvert.py:
//   fontconvert.py desk_clock_72 72 Inter-Bold.ttf --no-default-intervals
//     --additional-intervals 0x20,0x20 --additional-intervals 0x25,0x25
//     --additional-intervals 0x2D,0x3A --additional-intervals 0xB0,0xB0
// and the same at size 28 for desk_digits_28. Registered in main.cpp's setup.

#define DESK_CLOCK_FONT_ID (0x44534B43)   // ~150 px em: the clock and Pomodoro minutes
#define DESK_DIGITS_FONT_ID (0x44534B44)  // ~58 px em: temperature
