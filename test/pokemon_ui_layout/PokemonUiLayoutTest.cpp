#include <EpdFontFamily.h>
#include <PokemonUiLayout.h>
#include <builtinFonts/inter_12_bold.h>
#include <builtinFonts/inter_12_regular.h>
#include <gtest/gtest.h>

namespace {
// Reproduces the real layout math freeink-sdk's list.h applies to an
// artwork row on the Xteink X3 (screen width 528, 8px margin each side ->
// listBounds_ width 512 - see PokemonActivity::buildList()): content width
// is the row width minus sidePadding on BOTH sides (list.h applies it
// symmetrically), and a label sharing its line with a value further loses
// that value's measured width plus valueInset (8, set in buildList()) and
// textGap (10, list.h's own default).
constexpr int X3_ROW_WIDTH = 512;
constexpr int VALUE_INSET = 8;
constexpr int TEXT_GAP = 10;

int labelAvailWidth(const int sidePadding, const int valueWidth) {
  const int contentWidth = X3_ROW_WIDTH - sidePadding * 2;
  return valueWidth > 0 ? contentWidth - valueWidth - VALUE_INSET - TEXT_GAP : contentWidth;
}

class RealFontWidths : public ::testing::Test {
 protected:
  EpdFont regular_{&inter_12_regular};
  EpdFont bold_{&inter_12_bold};
  EpdFontFamily family_{&regular_, &bold_, nullptr, nullptr, nullptr};

  int widthOf(const char* text) const {
    int w = 0, h = 0;
    family_.getTextDimensions(text, &w, &h, EpdFontFamily::REGULAR);
    return w;
  }
};
}  // namespace

TEST(PokemonUiLayoutTest, UsesAvailableHeightInsteadOfADeviceSpecificPageSize) {
  EXPECT_EQ(pokemon::pokemonRowsPerPage(792, 72, 48, 64, 10), 10);
  EXPECT_EQ(pokemon::pokemonRowsPerPage(528, 72, 48, 64, 10), 6);
  EXPECT_EQ(pokemon::pokemonRowsPerPage(480, 80, 48, 64, 10), 5);
}

TEST(PokemonUiLayoutTest, KeepsAtLeastOneRowAndNeverExceedsFixedStorage) {
  EXPECT_EQ(pokemon::pokemonRowsPerPage(100, 80, 48, 64, 10), 1);
  EXPECT_EQ(pokemon::pokemonRowsPerPage(2000, 0, 0, 64, 10), 10);
}

TEST(PokemonUiLayoutTest, StartsPagesUsingTheCalculatedCapacity) {
  EXPECT_EQ(pokemon::pokemonPageStart(9, 10), 0);
  EXPECT_EQ(pokemon::pokemonPageStart(10, 10), 10);
  EXPECT_EQ(pokemon::pokemonPageStart(150, 10), 150);
  EXPECT_EQ(pokemon::pokemonPageStart(6, 6), 6);
}

TEST(PokemonUiLayoutTest, CentersNativeItemArtWithinItsRow) {
  EXPECT_EQ(pokemon::pokemonCenteredOffset(64, 32), 16);
  EXPECT_EQ(pokemon::pokemonCenteredOffset(80, 32), 24);
  EXPECT_EQ(pokemon::pokemonCenteredOffset(30, 40), 0);
}

TEST(PokemonUiLayoutTest, UsesTheSameWhitePaperCursorForTextAndArtworkLists) {
  for (const bool hasArtwork : {false, true}) {
    const auto presentation = pokemon::pokemonListPresentation(hasArtwork);
    for (const auto& style : {presentation.rowStyles.normal, presentation.rowStyles.selected,
                              presentation.rowStyles.focused, presentation.rowStyles.active}) {
      EXPECT_EQ(style.background.kind, freeink::ui::PaintKind::Solid);
      EXPECT_EQ(style.background.color, freeink::ui::Color::White);
      EXPECT_EQ(style.foreground.kind, freeink::ui::PaintKind::Solid);
      EXPECT_EQ(style.foreground.color, freeink::ui::Color::Black);
    }
  }

  const auto textOnly = pokemon::pokemonListPresentation(false);
  EXPECT_EQ(textOnly.sidePadding, 16);
  EXPECT_EQ(textOnly.markerInset, 0);

  const auto artwork = pokemon::pokemonListPresentation(true);
  EXPECT_EQ(artwork.sidePadding, 112);
  EXPECT_EQ(artwork.markerInset, 4);
}

TEST(PokemonUiLayoutTest, RightAlignsSummaryValuesToTheContentEdge) {
  EXPECT_EQ(pokemon::pokemonRightAlignedX(500, 42), 458);
  EXPECT_EQ(pokemon::pokemonRightAlignedX(500, 0), 500);
}

TEST(PokemonUiLayoutTest, CentersNativePokedexCardsWithoutUpscaling) {
  const auto portrait = pokemon::pokemonPokedexCardBounds(528, 792, 0, 0, 40, false);
  EXPECT_EQ(portrait.x, 28);
  EXPECT_EQ(portrait.y, 22);
  EXPECT_EQ(portrait.width, 472);
  EXPECT_EQ(portrait.height, 708);

  const auto landscape = pokemon::pokemonPokedexCardBounds(792, 528, 0, 0, 40, true);
  EXPECT_EQ(landscape.x, 252);
  EXPECT_EQ(landscape.y, 28);
  EXPECT_EQ(landscape.width, 288);
  EXPECT_EQ(landscape.height, 432);
}

// Regression coverage for fix/pokedex-tmhm-badges-text-truncation: Pokedex,
// Bag > Machine (TM/HM), and Badges were folding a second piece of info
// (the dex number, the taught move name, the Defeated/Locked status) onto
// the SAME line as the label, competing with the label for the row's
// content width - artwork rows only get sidePadding=112 of contentW=288 to
// share between the label and any value (see pokemonListPresentation()),
// and real worst-case strings didn't fit, so they were silently
// ellipsis-truncated. The fix moves the second piece of info to a subtitle
// line instead (PokemonActivity.cpp's Pokedex/BagMachine/Badges row-building
// cases), which gets the row's FULL content width rather than sharing it
// with a value. These tests measure the real worst-case strings against the
// real inter_12 font (the UI_12_FONT_ID this list actually renders with) to
// confirm the new single-purpose lines actually fit, and that the old
// combined single-line strings genuinely did not (documenting the bug this
// fixes, not just the fix itself).
TEST_F(RealFontWidths, PokedexNameAndSubtitleBothFitWhereTheOldCombinedLineDidNot) {
  const auto artwork = pokemon::pokemonListPresentation(true);
  const int caughtValueW = widthOf("Caught");
  const int availForLabel = labelAvailWidth(artwork.sidePadding, caughtValueW);
  // Longest species name that keeps a "Caught" value on the same line still
  // fits comfortably once the label is name-only.
  EXPECT_LE(widthOf("Charmander"), availForLabel);
  EXPECT_LE(widthOf("Tentacruel"), availForLabel);
  // The subtitle line ("No. 004") has the full content width to itself.
  const int fullContentW = X3_ROW_WIDTH - artwork.sidePadding * 2;
  EXPECT_LE(widthOf("No. 004"), fullContentW);
  // The old combined "No. 004  Charmander" line, sharing space with the
  // same "Caught" value, did not fit - confirms this was a real overflow,
  // not a theoretical one.
  EXPECT_GT(widthOf("No. 004  Charmander"), availForLabel);
}

TEST_F(RealFontWidths, BagMachineIdAndMoveNameSubtitleBothFitWhereTheOldCombinedLineDidNot) {
  const auto artwork = pokemon::pokemonListPresentation(true);
  const int countValueW = widthOf("\xC3\x97 99");  // "× 99"
  const int availForLabel = labelAvailWidth(artwork.sidePadding, countValueW);
  // The TM/HM id alone is short - it was never the problem.
  EXPECT_LE(widthOf("TM45"), availForLabel);
  // The move-name subtitle gets the full content width, so even the
  // longest real Gen 1 TM/HM move name fits.
  const int fullContentW = X3_ROW_WIDTH - artwork.sidePadding * 2;
  EXPECT_LE(widthOf("Thunder-Wave"), fullContentW);
  EXPECT_LE(widthOf("Self-Destruct"), fullContentW);
  // The old combined "TM45 - Thunder-Wave" line, sharing space with the ×
  // count value, did not fit.
  EXPECT_GT(widthOf("TM45 - Thunder-Wave"), availForLabel);
}

TEST_F(RealFontWidths, BadgeNameAndStatusSubtitleBothFitWhereTheOldCombinedLineDidNot) {
  const auto artwork = pokemon::pokemonListPresentation(true);
  // The badge name is now the label with no value sharing its line, so it
  // gets the full content width.
  const int fullContentW = X3_ROW_WIDTH - artwork.sidePadding * 2;
  EXPECT_LE(widthOf("Cascade Badge"), fullContentW);
  EXPECT_LE(widthOf("Rainbow Badge"), fullContentW);
  // Defeated/Locked, now a subtitle, also has the full content width.
  EXPECT_LE(widthOf("Defeated"), fullContentW);
  // The old combined line, with Defeated/Locked as the same-line value,
  // did not fit for several real badge names.
  const int defeatedValueW = widthOf("Defeated");
  const int oldAvailForLabel = labelAvailWidth(artwork.sidePadding, defeatedValueW);
  EXPECT_GT(widthOf("Cascade Badge"), oldAvailForLabel);
  EXPECT_GT(widthOf("Rainbow Badge"), oldAvailForLabel);
}

TEST(PokemonUiLayoutTest, UsesCleanRefreshOnlyAtActivityAndDetailBoundaries) {
  EXPECT_TRUE(pokemon::pokemonNeedsCleanRefresh(false, false, true));
  EXPECT_TRUE(pokemon::pokemonNeedsCleanRefresh(false, true, false));
  EXPECT_TRUE(pokemon::pokemonNeedsCleanRefresh(true, false, false));
  EXPECT_FALSE(pokemon::pokemonNeedsCleanRefresh(false, false, false));
  EXPECT_FALSE(pokemon::pokemonNeedsCleanRefresh(true, true, false));
}

TEST(PokemonUiLayoutTest, TrainerCardGridFitsThirteenTilesOnBothPortraitPanels) {
  struct Panel {
    int width;
    int height;
  };
  for (const Panel panel : {Panel{480, 800}, Panel{528, 792}}) {
    const int top = 200;
    const int bottom = panel.height - 20;
    pokemon::PokemonUiRect cells[13]{};
    ASSERT_EQ(pokemon::pokemonTrainerCardGrid(cells, 13, panel.width, top, bottom, 13, 2), 13);
    for (int i = 0; i < 13; ++i) {
      EXPECT_GE(cells[i].x, 0);
      EXPECT_LE(cells[i].x + cells[i].width, panel.width);
      EXPECT_GE(cells[i].y, top);
      EXPECT_LE(cells[i].y + cells[i].height, bottom);
      EXPECT_EQ(cells[i].width, cells[0].width);
      EXPECT_EQ(cells[i].height, cells[0].height);
      for (int j = i + 1; j < 13; ++j) {
        const bool apart = cells[i].x + cells[i].width <= cells[j].x || cells[j].x + cells[j].width <= cells[i].x ||
                           cells[i].y + cells[i].height <= cells[j].y || cells[j].y + cells[j].height <= cells[i].y;
        EXPECT_TRUE(apart) << "tiles " << i << " and " << j << " overlap on " << panel.width;
      }
    }
    EXPECT_EQ(cells[0].y, cells[1].y);   // two columns per row
    EXPECT_LT(cells[0].x, cells[1].x);
    EXPECT_EQ(cells[0].x, cells[2].x);   // columns line up down the grid
    EXPECT_GT(cells[2].y, cells[0].y);
  }
}

TEST(PokemonUiLayoutTest, TrainerCardGridCentersTheLoneLastTile) {
  pokemon::PokemonUiRect cells[13]{};
  ASSERT_EQ(pokemon::pokemonTrainerCardGrid(cells, 13, 480, 200, 780, 13, 2), 13);
  const int leftEdge = cells[0].x;
  const int rightEdge = cells[1].x + cells[1].width;
  const int lastCenter = cells[12].x + cells[12].width / 2;
  EXPECT_NEAR(lastCenter, (leftEdge + rightEdge) / 2, 1);
  EXPECT_GT(cells[12].y, cells[10].y);
}

TEST(PokemonUiLayoutTest, TrainerCardGridRefusesAnAreaTooShortForTheGrid) {
  pokemon::PokemonUiRect cells[13]{};
  EXPECT_EQ(pokemon::pokemonTrainerCardGrid(cells, 13, 480, 200, 400, 13, 2), 0);
  EXPECT_EQ(pokemon::pokemonTrainerCardGrid(cells, 12, 480, 200, 780, 13, 2), 0);  // capacity too small
  EXPECT_EQ(pokemon::pokemonTrainerCardGrid(nullptr, 13, 480, 200, 780, 13, 2), 0);
}

// The X4 Pro portrait panel is 480 wide (listBounds_ 464), so artwork rows
// there only get 464 - 2 * 112 = 240px of content width - narrower than the
// X3 the tests above were written against. These pin the rows that used to be
// ellipsis-truncated there against the real inter_12 metrics.
TEST_F(RealFontWidths, ArtworkRowsFitTheNarrowerX4ProPanel) {
  constexpr int X4PRO_CONTENT_WIDTH = 464 - 2 * 112;
  const char* longestNames[] = {"Kangaskhan", "Charmander", "Bellsprout", "Tentacruel", "Farfetch\xE2\x80\x99" "d"};
  const char* genderStar = " \xE2\x99\x82 \xE2\x98\x85";  // " ♂ ★"
  char label[64];
  for (const char* name : longestNames) {
    // Party > Move / BattleSwitch label: full name + gender + shiny star on
    // one line, with Lv N on the subtitle line instead of a same-line value.
    snprintf(label, sizeof(label), "%s%s", name, genderStar);
    EXPECT_LE(widthOf(label), X4PRO_CONTENT_WIDTH) << name;
    EXPECT_LE(widthOf("Lv 100"), X4PRO_CONTENT_WIDTH);
    // Pokedex: number + status share the subtitle, name is alone on its line.
    EXPECT_LE(widthOf("No. 004  -  Caught"), X4PRO_CONTENT_WIDTH);
    EXPECT_LE(widthOf(name), X4PRO_CONTENT_WIDTH) << name;
  }
  // The old same-line "Caught"/"Lv 100" values did not leave room for the
  // longer names on this panel - the bug these rows were reworked for.
  EXPECT_GT(widthOf("Charmander"), X4PRO_CONTENT_WIDTH - widthOf("Caught") - VALUE_INSET - TEXT_GAP);
  EXPECT_GT(widthOf("Charmander"),
            X4PRO_CONTENT_WIDTH - widthOf("Lv 100  \xE2\x99\x82 \xE2\x98\x85") - VALUE_INSET - TEXT_GAP);
  // Item rows only keep the count on the name's line when both fit; the
  // worst cases (a two-digit or three-digit count on a long name) do not.
  const int paralyzeWithTripleDigits =
      widthOf("Paralyze Heal") + widthOf("\xC3\x97 255") + VALUE_INSET + TEXT_GAP;
  EXPECT_GT(paralyzeWithTripleDigits, X4PRO_CONTENT_WIDTH);
  // ...and the count alone always fits on its own subtitle line.
  EXPECT_LE(widthOf("\xC3\x97 65535"), X4PRO_CONTENT_WIDTH);
}
