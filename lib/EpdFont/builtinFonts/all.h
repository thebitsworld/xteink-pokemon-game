#pragma once

#include <builtinFonts/ui_symbols_10.h>

// Built-in reading fonts retain the PHM fallback ranges but exclude emoticons.
// Bitter's own 16 files were dropped to save flash (~600 KB estimated - see
// TASKS.md's "Font tích hợp đọc sách" note); LexendDeca is the sole built-in
// reading font now (it was already this device's default reader font family,
// CrossPointSettings.h's `fontFamily = LEXENDDECA`), with SD-card fonts still
// available for anyone who wants a different one. CrossPointSettings.cpp's
// getBuiltInReaderFontId()/getFallbackReaderFontIdForFamily() redirect any
// save with fontFamily == BITTER (from before this change) to LexendDeca's
// glyph data instead - that FONT_FAMILY enum value itself is kept reserved,
// only its font files are gone.
#include <builtinFonts/lexenddeca_10_bold.h>
#include <builtinFonts/lexenddeca_10_bolditalic.h>
#include <builtinFonts/lexenddeca_10_italic.h>
#include <builtinFonts/lexenddeca_10_regular.h>
#include <builtinFonts/lexenddeca_12_bold.h>
#include <builtinFonts/lexenddeca_12_bolditalic.h>
#include <builtinFonts/lexenddeca_12_italic.h>
#include <builtinFonts/lexenddeca_12_regular.h>
#include <builtinFonts/lexenddeca_14_bold.h>
#include <builtinFonts/lexenddeca_14_bolditalic.h>
#include <builtinFonts/lexenddeca_14_italic.h>
#include <builtinFonts/lexenddeca_14_regular.h>
#include <builtinFonts/lexenddeca_16_bold.h>
#include <builtinFonts/lexenddeca_16_bolditalic.h>
#include <builtinFonts/lexenddeca_16_italic.h>
#include <builtinFonts/lexenddeca_16_regular.h>

// UI fonts - no emoji or PHM variants.
#include <builtinFonts/inter_10_bold.h>
#include <builtinFonts/inter_10_regular.h>
#include <builtinFonts/inter_12_bold.h>
#include <builtinFonts/inter_12_regular.h>
#include <builtinFonts/inter_8_regular.h>
