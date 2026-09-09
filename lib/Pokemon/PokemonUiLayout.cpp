#include "PokemonUiLayout.h"

#include <algorithm>

namespace pokemon {

int pokemonRowsPerPage(const int screenHeight, const int contentTop, const int bottomReserve, const int rowHeight,
                       const int maximumRows) {
  if (rowHeight <= 0 || maximumRows <= 0) return 0;
  const int available = std::max(0, screenHeight - contentTop - bottomReserve);
  return std::clamp(available / rowHeight, 1, maximumRows);
}

int pokemonPageStart(const int selectedIndex, const int rowsPerPage) {
  if (selectedIndex <= 0 || rowsPerPage <= 0) return 0;
  return (selectedIndex / rowsPerPage) * rowsPerPage;
}

int pokemonCenteredOffset(const int containerExtent, const int contentExtent) {
  return std::max(0, (containerExtent - contentExtent) / 2);
}

int pokemonRightAlignedX(const int rightEdge, const int contentWidth) { return rightEdge - std::max(0, contentWidth); }

PokemonListPresentation pokemonListPresentation(const bool hasArtwork) {
  freeink::ui::StyleSet styles = freeink::ui::selectedPlainListRowStyles();
  styles.selected = styles.normal;
  styles.focused = styles.normal;
  styles.active = styles.normal;
  // Artwork rows read left to right as marker, then icon, then text: the
  // selection triangle sits in its own small reserved strip at the row's
  // left edge (markerInset=4) before the icon (PokemonActivity.cpp's
  // renderRowArt() shifts the icon right to clear it) rather than squeezed
  // into the ~1px gap the icon and marker used to leave between them, which
  // let the marker land on top of whatever the icon's row drew immediately
  // after it (a wide HP bar made this obvious, but the tight gap existed
  // regardless of what followed the icon).
  return PokemonListPresentation{styles, hasArtwork ? 112 : 16, hasArtwork ? 4 : 0};
}

PokemonUiRect pokemonPokedexCardBounds(const int screenWidth, const int screenHeight, const int marginTop,
                                       const int marginBottom, const int footerHeight, const bool landscape) {
  constexpr int PORTRAIT_WIDTH = 472;
  constexpr int PORTRAIT_HEIGHT = 708;
  constexpr int LANDSCAPE_WIDTH = 288;
  constexpr int LANDSCAPE_HEIGHT = 432;
  int width = landscape ? LANDSCAPE_WIDTH : PORTRAIT_WIDTH;
  int height = landscape ? LANDSCAPE_HEIGHT : PORTRAIT_HEIGHT;
  const int availableHeight = std::max(0, screenHeight - marginTop - marginBottom - footerHeight);
  if (width > screenWidth || height > availableHeight) {
    const int widthScale = width == 0 ? 0 : screenWidth * 1000 / width;
    const int heightScale = height == 0 ? 0 : availableHeight * 1000 / height;
    const int scale = std::min(widthScale, heightScale);
    width = width * scale / 1000;
    height = height * scale / 1000;
  }
  return PokemonUiRect{(screenWidth - width) / 2, marginTop + (availableHeight - height) / 2, width, height};
}

bool pokemonNeedsCleanRefresh(const bool previousWasDetail, const bool nextIsDetail, const bool firstRender) {
  return firstRender || previousWasDetail != nextIsDetail;
}

}  // namespace pokemon
