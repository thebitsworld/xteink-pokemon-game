// The one translation unit that compiles the Peanut-GB core (lib/PeanutGB),
// in builds with the Game Boy screen (CROSSINK_GAMEBOY: the X4 Pro).
#if defined(CROSSINK_GAMEBOY) && CROSSINK_GAMEBOY
#define PEANUT_GB_IS_LITTLE_ENDIAN 1
#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#include <peanut_gb.h>
#endif
