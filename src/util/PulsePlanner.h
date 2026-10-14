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
// works on eight pixels at once - 32 when rows are whole 32-bit words and the
// buffers are word-aligned. The caller owns the storage: 5 * rows * widthBytes
// bytes (four planes and the target the panel is heading to).
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
    words_ = widthBytes % 4 == 0 && aligned(storage) && aligned(shown);
    active0_ = 1;
    active1_ = 0;
    dirtyRow0_ = 1;
    dirtyRow1_ = 0;
  }

  // Pulses per change toward black and toward white (1-15).
  void setBudgets(const uint8_t black, const uint8_t white) {
    black_ = clampBudget(black);
    white_ = clampBudget(white);
  }

  // A new picture: every pixel whose target changed restarts its budget.
  // Returns whether any did.
  bool retarget(const uint8_t* target) {
    bool any = false;
    for (uint16_t r = 0; r < rows_; ++r) {
      const uint32_t at = static_cast<uint32_t>(r) * wb_;
      if (memcmp(target_ + at, target + at, wb_) == 0) continue;
      any = true;
      if (words_ && aligned(target)) {
        retargetWords(target, at);
      } else {
        retargetBytes(target, at);
      }
      // The bytes of this row that changed join the rectangle polish() takes.
      const uint16_t b0 = pendingFirst_;
      const uint16_t b1 = pendingLast_ < wb_ ? pendingLast_ : static_cast<uint16_t>(wb_ - 1);
      if (dirtyRow0_ > dirtyRow1_) {
        dirtyRow0_ = dirtyRow1_ = r;
        dirtyByte0_ = b0;
        dirtyByte1_ = b1;
      } else {
        if (r < dirtyRow0_) dirtyRow0_ = r;
        if (r > dirtyRow1_) dirtyRow1_ = r;
        if (b0 < dirtyByte0_) dirtyByte0_ = b0;
        if (b1 > dirtyByte1_) dirtyByte1_ = b1;
      }
      if (active0_ > active1_) {
        active0_ = r;
        active1_ = r;
      } else {
        if (r < active0_) active0_ = r;
        if (r > active1_) active1_ = r;
      }
    }
    return any;
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
    const bool words = words_ && aligned(drive);
    for (uint16_t r = active0_; r <= active1_; ++r) {
      const uint32_t at = static_cast<uint32_t>(r) * wb_;
      int first = -1;
      int last = -1;
      if (words) {
        const uint32_t* p0 = word(plane_[0] + at);
        const uint32_t* p1 = word(plane_[1] + at);
        const uint32_t* p2 = word(plane_[2] + at);
        const uint32_t* p3 = word(plane_[3] + at);
        uint32_t* d = reinterpret_cast<uint32_t*>(drive + at);
        for (uint16_t i = 0; i < wb_ / 4; ++i) {
          d[i] = p0[i] | p1[i] | p2[i] | p3[i];
          if (!d[i]) continue;
          if (first < 0) first = i;
          last = i;
        }
        if (first >= 0) {
          // Down to bytes at the two ends of the row's run.
          const uint8_t* db = drive + at;
          first *= 4;
          while (!db[first]) ++first;
          last = last * 4 + 3;
          while (!db[last]) --last;
        }
      } else {
        for (uint16_t i = 0; i < wb_; ++i) {
          const uint8_t d = static_cast<uint8_t>(plane_[0][at + i] | plane_[1][at + i] | plane_[2][at + i] |
                                                 plane_[3][at + i]);
          drive[at + i] = d;
          if (!d) continue;
          if (first < 0) first = i;
          last = i;
        }
      }
      if (first < 0) continue;
      if (row0 < 0) row0 = r;
      row1 = r;
      if (first < byte0) byte0 = first;
      if (last > byte1) byte1 = last;
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
    const bool words = words_ && aligned(drive);
    for (uint16_t r = w.row0; r <= w.row1; ++r) {
      const uint32_t at = static_cast<uint32_t>(r) * wb_;
      if (words) {
        // Whole words around the window's bytes.
        const uint32_t* d = word(drive + at);
        uint32_t* p0 = reinterpret_cast<uint32_t*>(plane_[0] + at);
        uint32_t* p1 = reinterpret_cast<uint32_t*>(plane_[1] + at);
        uint32_t* p2 = reinterpret_cast<uint32_t*>(plane_[2] + at);
        uint32_t* p3 = reinterpret_cast<uint32_t*>(plane_[3] + at);
        for (uint16_t i = w.byte0 / 4; i <= w.byte1 / 4; ++i) {
          uint32_t borrow = d[i];
          if (!borrow) continue;
          uint32_t p = p0[i];
          p0[i] = p ^ borrow;
          borrow &= ~p;
          p = p1[i];
          p1[i] = p ^ borrow;
          borrow &= ~p;
          p = p2[i];
          p2[i] = p ^ borrow;
          borrow &= ~p;
          p3[i] ^= borrow;
        }
        continue;
      }
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

  // Every idle pixel of the rectangle that changed since the last polish -
  // black and white alike - gets `pulses` more toward its target: run when
  // the picture has stopped, it pushes what a cut-short change left behind
  // (a pixel that reversed mid-way) the rest of the way without a flash.
  // False when nothing changed since.
  bool polish(const uint8_t pulses) {
    if (dirtyRow0_ > dirtyRow1_) return false;
    const uint8_t n = clampBudget(pulses);
    for (uint16_t r = dirtyRow0_; r <= dirtyRow1_; ++r) {
      const uint32_t at = static_cast<uint32_t>(r) * wb_;
      for (uint16_t i = dirtyByte0_; i <= dirtyByte1_; ++i) {
        const uint8_t idle = static_cast<uint8_t>(
            ~(plane_[0][at + i] | plane_[1][at + i] | plane_[2][at + i] | plane_[3][at + i]));
        for (int k = 0; k < 4; ++k)
          if ((n >> k) & 1) plane_[k][at + i] = static_cast<uint8_t>(plane_[k][at + i] | idle);
      }
    }
    if (active0_ > active1_) {
      active0_ = dirtyRow0_;
      active1_ = dirtyRow1_;
    } else {
      if (dirtyRow0_ < active0_) active0_ = dirtyRow0_;
      if (dirtyRow1_ > active1_) active1_ = dirtyRow1_;
    }
    dirtyRow0_ = 1;
    dirtyRow1_ = 0;
    return true;
  }

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
  static bool aligned(const void* p) { return (reinterpret_cast<uintptr_t>(p) & 3) == 0; }
  static const uint32_t* word(const uint8_t* p) { return reinterpret_cast<const uint32_t*>(p); }

  // One row of retarget(): eight pixels per step...
  void retargetBytes(const uint8_t* target, const uint32_t at) {
    pendingFirst_ = wb_;
    pendingLast_ = 0;
    for (uint16_t i = 0; i < wb_; ++i) {
      const uint8_t t = target[at + i];
      const uint8_t changed = static_cast<uint8_t>(target_[at + i] ^ t);
      if (!changed) continue;
      if (i < pendingFirst_) pendingFirst_ = i;
      pendingLast_ = i;
      const uint8_t toBlack = static_cast<uint8_t>(changed & ~t);
      const uint8_t toWhite = static_cast<uint8_t>(changed & t);
      for (int k = 0; k < 4; ++k) {
        const uint8_t set = static_cast<uint8_t>(((black_ >> k) & 1 ? toBlack : 0) | ((white_ >> k) & 1 ? toWhite : 0));
        plane_[k][at + i] = static_cast<uint8_t>((plane_[k][at + i] & ~changed) | set);
      }
      target_[at + i] = t;
    }
  }

  // ...or 32 when the row is whole words.
  void retargetWords(const uint8_t* target, const uint32_t at) {
    const uint32_t* t = word(target + at);
    uint32_t* cur = reinterpret_cast<uint32_t*>(target_ + at);
    uint32_t* p[4];
    for (int k = 0; k < 4; ++k) p[k] = reinterpret_cast<uint32_t*>(plane_[k] + at);
    pendingFirst_ = wb_;
    pendingLast_ = 0;
    for (uint16_t i = 0; i < wb_ / 4; ++i) {
      const uint32_t changed = cur[i] ^ t[i];
      if (!changed) continue;
      if (i * 4 < pendingFirst_) pendingFirst_ = static_cast<uint16_t>(i * 4);
      pendingLast_ = static_cast<uint16_t>(i * 4 + 3);
      const uint32_t toBlack = changed & ~t[i];
      const uint32_t toWhite = changed & t[i];
      for (int k = 0; k < 4; ++k) {
        const uint32_t set = ((black_ >> k) & 1 ? toBlack : 0) | ((white_ >> k) & 1 ? toWhite : 0);
        p[k][i] = (p[k][i] & ~changed) | set;
      }
      cur[i] = t[i];
    }
  }

  uint8_t* plane_[4] = {};
  uint8_t* target_ = nullptr;
  uint16_t wb_ = 0;
  uint16_t rows_ = 0;
  bool words_ = false;
  uint8_t black_ = 8;
  uint8_t white_ = 8;
  // Rows that may still have pulses left (empty when active0_ > active1_).
  uint16_t active0_ = 1;
  uint16_t active1_ = 0;
  // The rectangle changed since the last polish (empty when row0 > row1),
  // and the byte span the last retargeted row changed in.
  uint16_t dirtyRow0_ = 1;
  uint16_t dirtyRow1_ = 0;
  uint16_t dirtyByte0_ = 0;
  uint16_t dirtyByte1_ = 0;
  uint16_t pendingFirst_ = 0;
  uint16_t pendingLast_ = 0;
};
