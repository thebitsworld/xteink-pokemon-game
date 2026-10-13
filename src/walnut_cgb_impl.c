// The one translation unit that compiles the Walnut-CGB core (lib/WalnutCGB)
// for Game Boy Color games, in builds with the Game Boy screen
// (CROSSINK_GAMEBOY: the X4 Pro). Its public names are renamed so they do not
// clash with Peanut-GB's in src/peanut_gb_impl.c; C++ uses walnut_cgb_port.h.
#if defined(CROSSINK_GAMEBOY) && CROSSINK_GAMEBOY

// The core is the Game Boy Color's CPU: compiled for speed rather than the
// firmware's default size (-Os). At -Os it ran Pokemon Gold at ~61% speed.
#pragma GCC optimize("O2")

#include <stdlib.h>
#include <string.h>

// The context (~50 KB: work RAM, video RAM and the CPU's registers, touched
// on every instruction) goes to internal RAM when there is room to spare,
// PSRAM otherwise (several times slower to reach). The simulator's
// esp_heap_caps.h is C++ only.
#ifdef SIMULATOR
#define walnutAlloc(size) calloc(1, (size))
#define walnutFree(ptr) free(ptr)
#else
#include <esp_heap_caps.h>
// Internal RAM left for the rest of the firmware with the context there.
#define WALNUT_INTERNAL_RESERVE (48 * 1024)
static void* walnutAlloc(const size_t size) {
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  void* p = NULL;
  if (largest >= size && freeInternal >= size + WALNUT_INTERNAL_RESERVE) {
    p = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!p) p = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : calloc(1, size);
}
#define walnutFree(ptr) heap_caps_free(ptr)
#endif

#include "walnut_cgb_port.h"

#define __gb_read walnut_core___gb_read
#define __gb_read16 walnut_core___gb_read16
#define __gb_read32 walnut_core___gb_read32
#define __gb_write walnut_core___gb_write
#define __gb_write16 walnut_core___gb_write16
#define __gb_write32 walnut_core___gb_write32
#define __gb_execute_cb walnut_core___gb_execute_cb
#define __gb_draw_line walnut_core___gb_draw_line
#define __gb_step_cpu walnut_core___gb_step_cpu
#define __gb_step_cpu_x walnut_core___gb_step_cpu_x
#define gb_run_frame walnut_core_gb_run_frame
#define gb_run_frame_dualfetch walnut_core_gb_run_frame_dualfetch
#define gb_get_save_size_s walnut_core_gb_get_save_size_s
#define gb_get_save_size walnut_core_gb_get_save_size
#define gb_init_serial walnut_core_gb_init_serial
#define gb_colour_hash walnut_core_gb_colour_hash
#define gb_reset walnut_core_gb_reset
#define gb_init walnut_core_gb_init
#define gb_get_rom_name walnut_core_gb_get_rom_name
#define gb_init_lcd walnut_core_gb_init_lcd
#define gb_set_bootrom walnut_core_gb_set_bootrom
#define gb_tick_rtc walnut_core_gb_tick_rtc
#define gb_set_rtc walnut_core_gb_set_rtc

#define WALNUT_GB_IS_LITTLE_ENDIAN 1
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define WALNUT_GB_12_COLOUR 1
#define WALNUT_FULL_GBC_SUPPORT 1
#include <walnut_cgb.h>

struct WalnutCgb {
  struct gb_s gb;
  const uint8_t* rom;
  size_t romSize;
  uint8_t* ram;
  size_t ramSize;
  WalnutCgbLineFn line;
  WalnutCgbErrorFn error;
  void (*ramWritten)(void* user);
  void* user;
  uint8_t shades[LCD_WIDTH];
};

static struct WalnutCgb* owner(struct gb_s* gb) { return (struct WalnutCgb*)gb->direct.priv; }

static uint8_t romRead(struct gb_s* gb, const uint_fast32_t addr) {
  const struct WalnutCgb* c = owner(gb);
  return addr < c->romSize ? c->rom[addr] : 0xFF;
}

static uint16_t romRead16(struct gb_s* gb, const uint_fast32_t addr) {
  return (uint16_t)(romRead(gb, addr) | (romRead(gb, addr + 1) << 8));
}

static uint32_t romRead32(struct gb_s* gb, const uint_fast32_t addr) {
  return (uint32_t)romRead16(gb, addr) | ((uint32_t)romRead16(gb, addr + 2) << 16);
}

static uint8_t cartRamRead(struct gb_s* gb, const uint_fast32_t addr) {
  const struct WalnutCgb* c = owner(gb);
  return c->ram && addr < c->ramSize ? c->ram[addr] : 0xFF;
}

static void cartRamWrite(struct gb_s* gb, const uint_fast32_t addr, const uint8_t value) {
  struct WalnutCgb* c = owner(gb);
  if (!c->ram || addr >= c->ramSize) return;
  if (c->ram[addr] == value) return;
  c->ram[addr] = value;
  if (c->ramWritten) c->ramWritten(c->user);
}

static void coreError(struct gb_s* gb, const enum gb_error_e error, const uint16_t addr) {
  struct WalnutCgb* c = owner(gb);
  if (c->error) c->error(c->user, (int)error, addr);
}

// Game Boy Color pixels index the colour palettes (RGB565 in fixPalette); a
// colour becomes the shade of its brightness, 0 white to 3 black, so the
// reader's dot-pattern greys show it. Game Boy (DMG) games on this core pass
// their shade through as Peanut-GB does (0 white).
static void drawLine(struct gb_s* gb, const uint8_t* pixels, const uint_fast8_t line) {
  struct WalnutCgb* c = owner(gb);
  if (gb->cgb.cgbMode) {
    for (unsigned x = 0; x < LCD_WIDTH; ++x) {
      const uint16_t rgb = gb->cgb.fixPalette[pixels[x] & 0x3F];
      const unsigned r = (rgb >> 11) & 0x1F;
      const unsigned g = (rgb >> 5) & 0x3F;
      const unsigned b = rgb & 0x1F;
      // Luma (0.299 R + 0.587 G + 0.114 B) on 0..255.
      const unsigned luma = (r * 299u * 255u / 31u + g * 587u * 255u / 63u + b * 114u * 255u / 31u) / 1000u;
      c->shades[x] = (uint8_t)(luma >= 192u ? 0 : luma >= 128u ? 1 : luma >= 64u ? 2 : 3);
    }
  } else {
    for (unsigned x = 0; x < LCD_WIDTH; ++x) c->shades[x] = (uint8_t)(pixels[x] & 0x03);
  }
  if (c->line) c->line(c->user, c->shades, line);
}

WalnutCgb* walnut_cgb_create(const uint8_t* rom, const size_t romSize, const WalnutCgbLineFn line,
                             const WalnutCgbErrorFn error, void* user, int* errorOut) {
  struct WalnutCgb* c = (struct WalnutCgb*)walnutAlloc(sizeof(struct WalnutCgb));
  if (!c) {
    if (errorOut) *errorOut = -1;
    return NULL;
  }
  c->rom = rom;
  c->romSize = romSize;
  c->line = line;
  c->error = error;
  c->user = user;
  const enum gb_init_error_e result =
      gb_init(&c->gb, romRead, romRead16, romRead32, cartRamRead, cartRamWrite, coreError, c);
  if (result != GB_INIT_NO_ERROR) {
    walnutFree(c);
    if (errorOut) *errorOut = (int)result;
    return NULL;
  }
  // gb_reset() leaves the colour cache unset: white until the game loads its
  // palettes.
  for (unsigned i = 0; i < 0x40; ++i) c->gb.cgb.fixPalette[i] = 0xFFFF;
  gb_init_lcd(&c->gb, drawLine);
  if (errorOut) *errorOut = 0;
  return c;
}

void walnut_cgb_destroy(WalnutCgb* c) {
  if (c) walnutFree(c);
}

int walnut_cgb_save_size(WalnutCgb* c, size_t* size) { return gb_get_save_size_s(&c->gb, size); }

void walnut_cgb_set_cart_ram(WalnutCgb* c, uint8_t* ram, const size_t size) {
  c->ram = ram;
  c->ramSize = size;
}

void walnut_cgb_set_ram_written(WalnutCgb* c, void (*written)(void* user)) { c->ramWritten = written; }

void walnut_cgb_run_frame(WalnutCgb* c) { gb_run_frame_dualfetch(&c->gb); }

void walnut_cgb_set_joypad(WalnutCgb* c, const uint8_t activeLow) { c->gb.direct.joypad = activeLow; }

void walnut_cgb_set_rtc(WalnutCgb* c, const struct tm* time) { gb_set_rtc(&c->gb, time); }

void walnut_cgb_rom_name(WalnutCgb* c, char out[17]) {
  memset(out, 0, 17);
  gb_get_rom_name(&c->gb, out);
}

int walnut_cgb_is_color(const WalnutCgb* c) { return c->gb.cgb.cgbMode != 0; }

size_t walnut_cgb_state_size(void) { return sizeof(struct gb_s); }

void walnut_cgb_save_state(const WalnutCgb* c, void* out) { memcpy(out, &c->gb, sizeof(struct gb_s)); }

void walnut_cgb_load_state(WalnutCgb* c, const void* in) {
  // The context's only pointers: the callbacks and the front end's context
  // (kept one by one; the context itself is ~50 KB, too big for the stack).
  uint8_t (*romRead8)(struct gb_s*, const uint_fast32_t) = c->gb.gb_rom_read;
  uint16_t (*rom16)(struct gb_s*, const uint_fast32_t) = c->gb.gb_rom_read_16bit;
  uint32_t (*rom32)(struct gb_s*, const uint_fast32_t) = c->gb.gb_rom_read_32bit;
  uint8_t (*ramRead)(struct gb_s*, const uint_fast32_t) = c->gb.gb_cart_ram_read;
  void (*ramWrite)(struct gb_s*, const uint_fast32_t, const uint8_t) = c->gb.gb_cart_ram_write;
  void (*errorFn)(struct gb_s*, const enum gb_error_e, const uint16_t) = c->gb.gb_error;
  void (*serialTx)(struct gb_s*, const uint8_t) = c->gb.gb_serial_tx;
  enum gb_serial_rx_ret_e (*serialRx)(struct gb_s*, uint8_t*) = c->gb.gb_serial_rx;
  uint8_t (*bootrom)(struct gb_s*, const uint_fast16_t) = c->gb.gb_bootrom_read;
  void (*drawLineFn)(struct gb_s*, const uint8_t*, const uint_fast8_t) = c->gb.display.lcd_draw_line;
  void* priv = c->gb.direct.priv;
  memcpy(&c->gb, in, sizeof(struct gb_s));
  c->gb.gb_rom_read = romRead8;
  c->gb.gb_rom_read_16bit = rom16;
  c->gb.gb_rom_read_32bit = rom32;
  c->gb.gb_cart_ram_read = ramRead;
  c->gb.gb_cart_ram_write = ramWrite;
  c->gb.gb_error = errorFn;
  c->gb.gb_serial_tx = serialTx;
  c->gb.gb_serial_rx = serialRx;
  c->gb.gb_bootrom_read = bootrom;
  c->gb.display.lcd_draw_line = drawLineFn;
  c->gb.direct.priv = priv;
}

#endif
