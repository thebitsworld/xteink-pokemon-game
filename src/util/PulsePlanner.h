#pragma once

#include <cstdint>
#include <cstring>

// Plans pulse-drive scans (HalDisplay::pulseScan) for a 1-bit picture in the
// framebuffer layout (1 = white). A pixel whose target changes gets a budget
// of pulses - one budget toward black, another toward white - and is driven
// once per delivered scan until it has used them; a pixel that changes again
// meanwhile starts over. Unchanged pixels are never driven.
//
// The pulses left are kept as four bit planes (0-15 per pixel), so every step
// works on eight pixels at once. The caller owns the storage: 5 * rows *
// widthBytes bytes (four planes and the target the panel is heading to).
class PulsePlanner {
 public:
  struct Window {
    uint16_t row0 = 0;
    uint16_t row1 = 0;
    uint16_t byte0 = 0;
    uint16_t byte1 = 0;
  };

  // `shown` is the picture already on the panel (nothing left to drive).
  void init(uint8_t* storage, const uint16_t widthBytes, const uint16_t rows, const uint8_t* shown) {
    wb_ = widthBytes;
    rows_ = rows;
    const uint32_t size = static_cast<uint32_t>(widthBytes) * rows;
    for (int k = 0; k < 4; ++k) {
      plane_[k] = storage + k * size;
      memset(plane_[k], 0, size);
    }
    target_ = storage + 4 * size;
    memcpy(target_, shown, size);
    active0_ = 1;
    active1_ = 0;
  }

  // Pulses per change toward black and toward white (1-15).
  void setBudgets(const uint8_t black, const uint8_t white) {
    black_ = clampBudget(black);
    white_ = clampBudget(white);
  }

  // A new picture: every pixel whose target changed restarts its budget.
  void retarget(const uint8_t* target) {
    for (uint16_t r = 0; r < rows_; ++r) {
      const uint32_t at = static_cast<uint32_t>(r) * wb_;
      if (memcmp(target_ + at, target + at, wb_) == 0) continue;
      for (uint16_t i = 0; i < wb_; ++i) {
        const uint8_t t = target[at + i];
        const uint8_t changed = static_cast<uint8_t>(target_[at + i] ^ t);
        if (!changed) continue;
        const uint8_t toBlack = static_cast<uint8_t>(changed & ~t);
        const uint8_t toWhite = static_cast<uint8_t>(changed & t);
        for (int k = 0; k < 4; ++k) {
          const uint8_t set = static_cast<uint8_t>(((black_ >> k) & 1 ? toBlack : 0) | ((white_ >> k) & 1 ? toWhite : 0));
          plane_[k][at + i] = static_cast<uint8_t>((plane_[k][at + i] & ~changed) | set);
        }
        target_[at + i] = t;
      }
      if (active0_ > active1_) {
        active0_ = r;
        active1_ = r;
      } else {
        if (r < active0_) active0_ = r;
        if (r > active1_) active1_ = r;
      }
    }
  }

  // The drive mask of the next scan (framebuffer layout, 1 = drive) and the
  // rectangle it covers; false when no pixel has pulses left. Only the
  // returned window of `drive` is written.
  bool plan(uint8_t* drive, Window& out) {
    if (active0_ > active1_) return false;
    int row0 = -1;
    int row1 = -1;
    int byte0 = wb_;
    int byte1 = -1;
    for (uint16_t r = active0_; r <= active1_; ++r) {
      const uint32_t at = static_cast<uint32_t>(r) * wb_;
      for (uint16_t i = 0; i < wb_; ++i) {
        const uint8_t d = static_cast<uint8_t>(plane_[0][at + i] | plane_[1][at + i] | plane_[2][at + i] |
                                               plane_[3][at + i]);
        drive[at + i] = d;
        if (!d) continue;
        if (row0 < 0) row0 = r;
        row1 = r;
        if (i < byte0) byte0 = i;
        if (i > byte1) byte1 = i;
      }
    }
    if (row0 < 0) {
      active0_ = 1;
      active1_ = 0;
      return false;
    }
    active0_ = static_cast<uint16_t>(row0);
    active1_ = static_cast<uint16_t>(row1);
    out.row0 = static_cast<uint16_t>(row0);
    out.row1 = static_cast<uint16_t>(row1);
    out.byte0 = static_cast<uint16_t>(byte0);
    out.byte1 = static_cast<uint16_t>(byte1);
    return true;
  }

  // The scan planned with `drive` reached the panel: its pixels used a pulse.
  void delivered(const uint8_t* drive, const Window& w) {
    for (uint16_t r = w.row0; r <= w.row1; ++r) {
      const uint32_t at = static_cast<uint32_t>(r) * wb_;
      for (uint16_t i = w.byte0; i <= w.byte1; ++i) {
        uint8_t borrow = drive[at + i];
        for (int k = 0; k < 4 && borrow; ++k) {
          const uint8_t p = plane_[k][at + i];
          plane_[k][at + i] = static_cast<uint8_t>(p ^ borrow);
          borrow = static_cast<uint8_t>(borrow & ~p);
        }
      }
    }
  }

  bool busy() const { return active0_ <= active1_; }

  // Pulses left for one pixel (tests and diagnostics).
  uint8_t pulsesLeft(const uint16_t x, const uint16_t row) const {
    const uint32_t at = static_cast<uint32_t>(row) * wb_ + x / 8;
    const uint8_t bit = static_cast<uint8_t>(0x80 >> (x & 7));
    uint8_t n = 0;
    for (int k = 0; k < 4; ++k)
      if (plane_[k][at] & bit) n = static_cast<uint8_t>(n | (1 << k));
    return n;
  }

 private:
  static uint8_t clampBudget(const uint8_t n) { return n < 1 ? 1 : (n > 15 ? 15 : n); }

  uint8_t* plane_[4] = {};
  uint8_t* target_ = nullptr;
  uint16_t wb_ = 0;
  uint16_t rows_ = 0;
  uint8_t black_ = 8;
  uint8_t white_ = 8;
  // Rows that may still have pulses left (empty when active0_ > active1_).
  uint16_t active0_ = 1;
  uint16_t active1_ = 0;
};
