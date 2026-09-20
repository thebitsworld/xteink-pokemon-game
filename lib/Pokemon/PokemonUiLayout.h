#pragma once

#include <FreeInkUICore.h>

namespace pokemon {

struct PokemonUiRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct PokemonListPresentation {
  freeink::ui::StyleSet rowStyles{};
  int sidePadding = 16;
  int markerInset = 0;
};

int pokemonRowsPerPage(int screenHeight, int contentTop, int bottomReserve, int rowHeight, int maximumRows);
int pokemonPageStart(int selectedIndex, int rowsPerPage);
int pokemonCenteredOffset(int containerExtent, int contentExtent);
int pokemonRightAlignedX(int rightEdge, int contentWidth);
PokemonListPresentation pokemonListPresentation(bool hasArtwork);
PokemonUiRect pokemonPokedexCardBounds(int screenWidth, int screenHeight, int marginTop, int marginBottom,
                                       int footerHeight, bool landscape);
bool pokemonNeedsCleanRefresh(bool previousWasDetail, bool nextIsDetail, bool firstRender);

// Lays `tileCount` equal tiles into `columns` columns inside [top, bottom), top-anchored,
// writing the cells in reading order to `out` (at most `capacity`). Cell height is capped at
// `maxTileHeight` (defaults to the Trainer Card's own icon-sized 72px) so small counts do not
// stretch; a short last row is centered. Returns the number of cells written, or 0 if the area
// cannot hold the grid at the minimum tile size. The Hall of Fame screen passes a taller
// `maxTileHeight` of its own - its cells hold a hero sprite plus two lines of text, not just an
// icon, so the Trainer Card's own cap would crush them.
int pokemonTrainerCardGrid(PokemonUiRect* out, int capacity, int screenWidth, int top, int bottom, int tileCount,
                           int columns, int maxTileHeight = 72);

}  // namespace pokemon
