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
  // Colour index (0..63) -> shade, for the palettes in paletteSeen; rebuilt
  // when the game changes its palettes or the colour mode changes.
  uint16_t paletteSeen[0x40];
  uint8_t shadeOf[0x40];
  uint8_t shadesValid;
  uint8_t colourMode;  // WALNUT_COLOURS_* (walnut_cgb_port.h)
};

static struct WalnutCgb* owner(struct gb_s* gb) { return (struct WalnutCgb*)gb->direct.priv; }

static uint8_t romRead(struct gb_s* gb, const uint_fast32_t addr) {
  const struct WalnutCgb* c = owner(gb);
  return addr < c->romSize ? c->rom[addr] : 0xFF;
}

// Operand fetches read 2 or 4 ROM bytes for nearly every instruction: check
// the bound once and read them straight (idea from Ink-boy 1.2.0); only a read
// that runs past the end of the image goes byte by byte (0xFF beyond it).
static uint16_t romRead16(struct gb_s* gb, const uint_fast32_t addr) {
  const struct WalnutCgb* c = owner(gb);
  if (addr + 2u <= c->romSize) {
    const uint8_t* p = c->rom + addr;
    return (uint16_t)(p[0] | (p[1] << 8));
  }
  return (uint16_t)(romRead(gb, addr) | (romRead(gb, addr + 1) << 8));
}

static uint32_t romRead32(struct gb_s* gb, const uint_fast32_t addr) {
  const struct WalnutCgb* c = owner(gb);
  if (addr + 4u <= c->romSize) {
    const uint8_t* p = c->rom + addr;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  }
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

// Luma (0.299 R + 0.587 G + 0.114 B) of an RGB565 colour, on 0..255.
static unsigned luma565(const uint16_t rgb) {
  const unsigned r = (rgb >> 11) & 0x1F;
  const unsigned g = (rgb >> 5) & 0x3F;
  const unsigned b = rgb & 0x1F;
  return (r * 299u * 255u / 31u + g * 587u * 255u / 63u + b * 114u * 255u / 31u) / 1000u;
}

// The reader's grey levels (GameBoyActivity's shadeIsBlack()): darkness 0
// white to 16 black, an ordered dither. In the Contrast and Brightness modes a
// brightness falls into a quarter of 0..255; by day the quarters take the
// Game Boy's own four greys (levels 0, 4, 12, 16), and in a night scene the
// four darker ones (4, 8, 12, 16). Smooth uses all 17 levels.
static const uint8_t kDayLevel[4] = {0, 4, 12, 16};
static const uint8_t kNightLevel[4] = {4, 8, 12, 16};
static unsigned quarterOfLuma(const unsigned luma) {
  return luma >= 192u ? 0u : luma >= 128u ? 1u : luma >= 64u ? 2u : 3u;
}

// The median of eight values (the mean of the middle two).
static unsigned median8(unsigned* v) {
  for (unsigned i = 1; i < 8; ++i) {
    for (unsigned k = i; k > 0 && v[k] < v[k - 1]; --k) {
      const unsigned t = v[k];
      v[k] = v[k - 1];
      v[k - 1] = t;
    }
  }
  return (v[3] + v[4]) / 2;
}

// The picture is stretched only when the palettes span between these two
// (on 0..255): above, it already looks as the game means it (day); below, the
// game keeps the scene dark on purpose (a cave), with no contrast to find.
#define WALNUT_MIN_SPAN 32u
#define WALNUT_FULL_SPAN 200u

// The grey levels of all 64 palette colours (8 background and 8 sprite
// palettes of 4; a sprite palette's colour 0 is transparent). Brightness mode
// gives each colour the day grey of its brightness. Smooth gives it the level
// of its brightness among all 17, and in a night scene (below) stretches it
// onto levels 4 to 16. Contrast mode first
// stretches the brightness of every colour by one rule: from the black point
// (the median of the 8 background palettes' darkest colours) to the white
// point (the median of their lightest) over 0..255, when that span is narrow.
// By day the palettes span most of 0..255 and nothing changes; when the game
// dims them all for the night - by brightness its grass fell into black - the
// stretch brings their colours apart again, onto the night greys, which leave
// out white so the scene stays dark (stretched onto all four day greys it was
// glaring). A palette the game keeps bright (a text box) only clips to the
// lightest. One colour always gets one grey, whichever palette holds it.
static void buildShades(struct WalnutCgb* c, const uint16_t* palette) {
  unsigned luma[0x40];
  for (unsigned i = 0; i < 0x40; ++i) luma[i] = luma565(palette[i]);
  unsigned black = 0;
  unsigned white = 255;
  int night = 0;
  if (c->colourMode != WALNUT_COLOURS_BRIGHTNESS) {
    unsigned darkest[8];
    unsigned lightest[8];
    for (unsigned p = 0; p < 8; ++p) {
      darkest[p] = 255;
      lightest[p] = 0;
      for (unsigned k = 0; k < 4; ++k) {
        const unsigned l = luma[p * 4 + k];
        if (l < darkest[p]) darkest[p] = l;
        if (l > lightest[p]) lightest[p] = l;
      }
    }
    black = median8(darkest);
    white = median8(lightest);
    night = white >= black + WALNUT_MIN_SPAN && white < black + WALNUT_FULL_SPAN;
  }
  const int smooth = c->colourMode == WALNUT_COLOURS_SMOOTH;
  for (unsigned i = 0; i < 0x40; ++i) {
    unsigned l = luma[i];
    if (night) {
      l = l <= black ? 0 : l >= white ? 255 : (l - black) * 255u / (white - black);
      c->shadeOf[i] = smooth ? (uint8_t)(16u - (l * 12u + 127u) / 255u) : kNightLevel[quarterOfLuma(l)];
    } else {
      c->shadeOf[i] = smooth ? (uint8_t)(16u - (l * 16u + 127u) / 255u) : kDayLevel[quarterOfLuma(l)];
    }
  }
  for (unsigned i = 0x20; i < 0x40; i += 4) c->shadeOf[i] = 0;  // transparent
  memcpy(c->paletteSeen, palette, sizeof(c->paletteSeen));
  c->shadesValid = 1;
}

// Game Boy Color pixels index the colour palettes (RGB565 in fixPalette);
// each colour becomes one of the reader's grey levels (buildShades()). Game
// Boy (DMG) games on this core take the Game Boy's four greys, as Peanut-GB's
// do.
static void drawLine(struct gb_s* gb, const uint8_t* pixels, const uint_fast8_t line) {
  struct WalnutCgb* c = owner(gb);
  if (gb->cgb.cgbMode) {
    if (!c->shadesValid || memcmp(c->paletteSeen, gb->cgb.fixPalette, sizeof(c->paletteSeen)) != 0) {
      buildShades(c, gb->cgb.fixPalette);
    }
    for (unsigned x = 0; x < LCD_WIDTH; ++x) c->shades[x] = c->shadeOf[pixels[x] & 0x3F];
  } else {
    for (unsigned x = 0; x < LCD_WIDTH; ++x) c->shades[x] = kDayLevel[pixels[x] & 0x03];
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

// The core's own frame skip, steered from outside: its line drawing runs while
// frame_skip_count is set (it flips the count at each VBlank; this sets it
// again before every frame).
void walnut_cgb_set_colours(WalnutCgb* c, const int mode) {
  c->colourMode = (uint8_t)mode;
  c->shadesValid = 0;
}

void walnut_cgb_set_draw(WalnutCgb* c, const int draw) {
  c->gb.direct.frame_skip = 1;
  c->gb.display.frame_skip_count = draw ? 1 : 0;
}

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
