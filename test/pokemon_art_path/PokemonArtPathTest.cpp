#include <gtest/gtest.h>

#include "components/pokemon/PokemonArtPath.h"

TEST(PokemonArtPath, BuildsApprovedKantoIconAndHeroPaths) {
  char path[64]{};
  ASSERT_NE(pokemon::pokemonSpeciesArtPath(1, false, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/sprites/001.bmp");
  ASSERT_NE(pokemon::pokemonSpeciesArtPath(151, true, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/heroes/151.bmp");
}

TEST(PokemonArtPath, RejectsSpeciesOutsideTheOriginal151) {
  char path[64] = "unchanged";
  EXPECT_EQ(pokemon::pokemonSpeciesArtPath(0, false, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "");
  EXPECT_EQ(pokemon::pokemonSpeciesArtPath(172, false, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "");
}

TEST(PokemonArtPath, BuildsDedicatedOrientationSpecificPokedexPaths) {
  char path[64]{};
  ASSERT_NE(pokemon::pokemonPokedexArtPath(1, false, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/pokedex/portrait/001.bmp");
  ASSERT_NE(pokemon::pokemonPokedexArtPath(29, true, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/pokedex/landscape/029.bmp");
  ASSERT_NE(pokemon::pokemonPokedexArtPath(151, false, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/pokedex/portrait/151.bmp");
}

TEST(PokemonArtPath, BuildsStonePathsButLeavesLinkCableBlank) {
  char path[80]{};
  ASSERT_NE(pokemon::pokemonItemArtPath(pokemon::EvolutionItem::ThunderStone, false, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/items/thunder-stone.bmp");
  ASSERT_NE(pokemon::pokemonItemArtPath(pokemon::EvolutionItem::MoonStone, true, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/heroes/items/moon-stone.bmp");
  EXPECT_EQ(pokemon::pokemonItemArtPath(pokemon::EvolutionItem::LinkCable, false, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "");
}

TEST(PokemonArtPath, BuildsBackSpritePathsForTheOriginal151Only) {
  char path[64]{};
  ASSERT_NE(pokemon::pokemonSpeciesBackArtPath(1, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/heroes/back/001.bmp");
  ASSERT_NE(pokemon::pokemonSpeciesBackArtPath(151, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/heroes/back/151.bmp");
  EXPECT_EQ(pokemon::pokemonSpeciesBackArtPath(0, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "");
  EXPECT_EQ(pokemon::pokemonSpeciesBackArtPath(152, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "");
}

TEST(PokemonArtPath, BuildsBagItemPathsForBagItemsOnly) {
  char path[64]{};
  // Item id 7 (Poke Ball) is the first bag item, right after the 6 evolution
  // items - see EVOLUTION_ITEM_COUNT.
  ASSERT_NE(pokemon::pokemonBagItemArtPath(7, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/items/007.bmp");
  ASSERT_NE(pokemon::pokemonBagItemArtPath(83, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/items/083.bmp");
  // Evolution items (ids 1-6) go through pokemonItemArtPath()'s slug-based
  // path instead, not this one.
  EXPECT_EQ(pokemon::pokemonBagItemArtPath(6, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "");
  EXPECT_EQ(pokemon::pokemonBagItemArtPath(84, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "");
}

TEST(PokemonArtPath, BuildsBadgePathsForTheEightKantoGyms) {
  char path[64]{};
  ASSERT_NE(pokemon::pokemonBadgeArtPath(1, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/badges/01.bmp");
  ASSERT_NE(pokemon::pokemonBadgeArtPath(8, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "/pokemon/badges/08.bmp");
  EXPECT_EQ(pokemon::pokemonBadgeArtPath(0, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "");
  EXPECT_EQ(pokemon::pokemonBadgeArtPath(9, path, sizeof(path)), nullptr);
  EXPECT_STREQ(path, "");
}
