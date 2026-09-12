#include <EpdFontFamily.h>
#include <Utf8.h>
#include <builtinFonts/inter_10_bold.h>
#include <builtinFonts/inter_10_regular.h>
#include <builtinFonts/inter_12_bold.h>
#include <builtinFonts/inter_12_regular.h>
#include <builtinFonts/ui_symbols_10.h>
#include <gtest/gtest.h>

namespace {
constexpr uint32_t POWER = 0x23FB;
constexpr uint32_t FEMALE = 0x2640;
constexpr uint32_t MALE = 0x2642;
const EpdFont symbols(&ui_symbols_10);
const EpdFont smallRegular(&inter_10_regular), smallBold(&inter_10_bold);
const EpdFont largeRegular(&inter_12_regular), largeBold(&inter_12_bold);
const EpdFontFamily small(&smallRegular, &smallBold, nullptr, nullptr, &symbols);
const EpdFontFamily large(&largeRegular, &largeBold, nullptr, nullptr, &symbols);
}  // namespace

TEST(UiSymbolFallback, ContainsExactlyThreeGlyphs) {
  // Power (pre-existing) + the male/female gender glyphs (added for the
  // Pokemon Party row's gender indicator - see genderAbbrev() in
  // PokemonActivity.cpp) - all three come from the same NotoSansSymbols
  // font stack, so they share this one small fallback glyph set instead of
  // needing a font per codepoint.
  EXPECT_EQ(sizeof(ui_symbols_10Glyphs) / sizeof(ui_symbols_10Glyphs[0]), 3u);
  EXPECT_EQ(sizeof(ui_symbols_10Intervals) / sizeof(ui_symbols_10Intervals[0]), 3u);
  EXPECT_TRUE(symbols.hasCodepoint(POWER));
  EXPECT_TRUE(symbols.hasCodepoint(FEMALE));
  EXPECT_TRUE(symbols.hasCodepoint(MALE));
  EXPECT_FALSE(symbols.hasCodepoint('A'));
}

TEST(UiSymbolFallback, SharesTheSameRasterAtBothScalesAndStyles) {
  for (const auto* family : {&small, &large}) {
    for (const auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
      const auto glyph = family->getGlyphData(POWER, style);
      EXPECT_EQ(glyph.fontData, &ui_symbols_10);
      EXPECT_EQ(glyph.glyph, symbols.findGlyph(POWER));
      EXPECT_TRUE(family->hasCodepoint(POWER, style));
      EXPECT_EQ(family->getFallbackCodepoint(POWER, style), POWER);
      int width = 0, height = 0;
      family->getTextDimensions("⏻", &width, &height, style);
      EXPECT_EQ(width, 18);
      EXPECT_EQ(height, 18);
    }
  }
}

TEST(UiSymbolFallback, GenderGlyphsFallBackAtBothScalesAndStyles) {
  for (const auto* family : {&small, &large}) {
    for (const auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
      for (const uint32_t codepoint : {FEMALE, MALE}) {
        const auto glyph = family->getGlyphData(codepoint, style);
        EXPECT_EQ(glyph.fontData, &ui_symbols_10);
        EXPECT_EQ(glyph.glyph, symbols.findGlyph(codepoint));
        EXPECT_TRUE(family->hasCodepoint(codepoint, style));
        EXPECT_EQ(family->getFallbackCodepoint(codepoint, style), codepoint);
      }
    }
  }
  // Measured width includes the glyph's 1px left bearing (EpdGlyph::left),
  // not just its raw EpdGlyph::width - matching how getTextDimensions()
  // computes maxX as glyphBaseX + glyph->left + glyph->width. U+2640 is
  // {width 9, left 1} -> 10; U+2642 is {width 11, left 1} -> 12.
  int width = 0, height = 0;
  small.getTextDimensions("♀", &width, &height);
  EXPECT_EQ(width, 10);
  EXPECT_EQ(height, 15);
  small.getTextDimensions("♂", &width, &height);
  EXPECT_EQ(width, 12);
  EXPECT_EQ(height, 12);
}

TEST(UiSymbolFallback, PreservesNormalGlyphsAndMissingGlyphBehavior) {
  EXPECT_EQ(small.getGlyphData('A').fontData, &inter_10_regular);
  EXPECT_EQ(large.getGlyphData('*', EpdFontFamily::BOLD).fontData, &inter_12_bold);
  const EpdFontFamily noFallback(&smallRegular);
  EXPECT_FALSE(noFallback.hasCodepoint(POWER));
  EXPECT_EQ(noFallback.getGlyphData(POWER).glyph, smallRegular.getGlyph(REPLACEMENT_GLYPH));
}
