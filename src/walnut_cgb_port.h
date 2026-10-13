#pragma once

// The Walnut-CGB core (lib/WalnutCGB), which runs Game Boy Color games, behind
// an opaque handle: its gb_s and symbol names clash with Peanut-GB's, so it is
// compiled alone in src/walnut_cgb_impl.c and C++ never includes its header.
// X4 Pro builds only (CROSSINK_GAMEBOY).

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WalnutCgb WalnutCgb;

// One finished line of 160 pixels as the reader's grey levels: 0 white, 1 a
// quarter black, 2 half, 3 three quarters, 4 black (Game Boy shades use 0, 1,
// 3 and 4; Game Boy Color colours are turned into levels in walnut_cgb_impl.c).
typedef void (*WalnutCgbLineFn)(void* user, const uint8_t* shades, uint_fast8_t line);
// The core hit an error it cannot run past (gb_error_e code, address).
typedef void (*WalnutCgbErrorFn)(void* user, int error, uint16_t addr);

// Starts the core on `rom` (kept by the caller for the core's lifetime).
// Returns NULL with *error set: a gb_init_error_e value, or -1 out of memory.
WalnutCgb* walnut_cgb_create(const uint8_t* rom, size_t romSize, WalnutCgbLineFn line, WalnutCgbErrorFn error,
                             void* user, int* errorOut);
void walnut_cgb_destroy(WalnutCgb* cgb);

// The cartridge RAM the game needs (0 for none); negative if unknown.
int walnut_cgb_save_size(WalnutCgb* cgb, size_t* size);
// The cartridge RAM buffer (kept by the caller, `size` bytes).
void walnut_cgb_set_cart_ram(WalnutCgb* cgb, uint8_t* ram, size_t size);
// Called with each cartridge RAM write (to mark the save dirty).
void walnut_cgb_set_ram_written(WalnutCgb* cgb, void (*written)(void* user));

void walnut_cgb_run_frame(WalnutCgb* cgb);
// How Game Boy Color colours become greys: contrast (default) stretches a
// scene the game dims (night) onto the darker greys so its colours stay
// apart; otherwise each colour takes the grey of its brightness.
void walnut_cgb_set_contrast(WalnutCgb* cgb, int contrast);
// Whether the next frame's lines are drawn (sent to the line callback); a
// frame that is not drawn still runs in full.
void walnut_cgb_set_draw(WalnutCgb* cgb, int draw);
// Joypad state, active low as on the Game Boy.
void walnut_cgb_set_joypad(WalnutCgb* cgb, uint8_t activeLow);
void walnut_cgb_set_rtc(WalnutCgb* cgb, const struct tm* time);
// The ROM's title, up to 16 characters and a terminator.
void walnut_cgb_rom_name(WalnutCgb* cgb, char out[17]);
// True when the game runs in Game Boy Color mode.
int walnut_cgb_is_color(const WalnutCgb* cgb);

// A saved state is the core's whole context; its pointers are put back on load.
size_t walnut_cgb_state_size(void);
void walnut_cgb_save_state(const WalnutCgb* cgb, void* out);
void walnut_cgb_load_state(WalnutCgb* cgb, const void* in);

#ifdef __cplusplus
}
#endif
