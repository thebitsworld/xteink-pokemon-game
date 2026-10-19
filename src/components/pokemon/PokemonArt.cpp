#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonArt.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <vector>

#include "PokemonArtPath.h"
#include "fontIds.h"

namespace pokemon {
namespace {

void drawFallback(const GfxRenderer& renderer, const Rect bounds) {
  const char* mark = "?";
  const int width = renderer.getTextWidth(UI_12_FONT_ID, mark);
  const int height = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawText(UI_12_FONT_ID, bounds.x + (bounds.width - width) / 2, bounds.y + (bounds.height - height) / 2,
                    mark);
}

bool drawPath(const GfxRenderer& renderer, const char* path, const Rect bounds, const bool fallback) {
  if (path == nullptr || bounds.width <= 0 || bounds.height <= 0) return false;
  FsFile file;
  if (!Storage.openFileForRead("PKART", path, file)) {
    LOG_ERR("PKART", "Missing Pokemon art: %s", path);
    if (fallback) drawFallback(renderer, bounds);
    return false;
  }
  Bitmap bitmap(file);
  const BmpReaderError parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_ERR("PKART", "Invalid Pokemon art: %s", path);
    if (fallback) drawFallback(renderer, bounds);
    file.close();
    return false;
  }

  renderer.drawBitmap(bitmap, bounds.x, bounds.y, bounds.width, bounds.height, 0.0f, 0.0f);
  file.close();
  return true;
}

}  // namespace

bool drawPokemonSpeciesArt(const GfxRenderer& renderer, const uint16_t speciesId, const bool hero, const Rect bounds,
                           const bool fallback) {
  char path[64]{};
  return drawPath(renderer, pokemonSpeciesArtPath(speciesId, hero, path, sizeof(path)), bounds, fallback);
}

bool drawPokemonSpeciesBackArt(const GfxRenderer& renderer, const uint16_t speciesId, const Rect bounds,
                               const bool fallback) {
  char path[64]{};
  return drawPath(renderer, pokemonSpeciesBackArtPath(speciesId, path, sizeof(path)), bounds, fallback);
}

bool drawPokemonPokedexArt(const GfxRenderer& renderer, const uint16_t speciesId, const bool landscape,
                           const Rect bounds, const bool fallback) {
  char path[64]{};
  return drawPath(renderer, pokemonPokedexArtPath(speciesId, landscape, path, sizeof(path)), bounds, fallback);
}

bool drawPokemonItemArt(const GfxRenderer& renderer, const EvolutionItem item, const bool hero, const Rect bounds,
                        const bool fallback) {
  char path[80]{};
  const char* pathValue = pokemonItemArtPath(item, hero, path, sizeof(path));
  if (pathValue == nullptr && item == EvolutionItem::LinkCable) return false;
  return drawPath(renderer, pathValue, bounds, fallback);
}

bool drawPokemonBagItemArt(const GfxRenderer& renderer, const uint8_t itemId, const Rect bounds,
                          const bool fallback) {
  char path[64]{};
  return drawPath(renderer, pokemonBagItemArtPath(itemId, path, sizeof(path)), bounds, fallback);
}

bool drawPokemonBadgeArt(const GfxRenderer& renderer, const uint8_t gymIndex, const Rect bounds,
                         const bool fallback) {
  char path[64]{};
  return drawPath(renderer, pokemonBadgeArtPath(gymIndex, path, sizeof(path)), bounds, fallback);
}

bool drawPokemonTrainerArt(const GfxRenderer& renderer, const uint8_t gymIndex, const Rect bounds,
                          const bool fallback) {
  char path[64]{};
  return drawPath(renderer, pokemonTrainerArtPath(gymIndex, path, sizeof(path)), bounds, fallback);
}

bool drawPokemonMoveEffectArt(const GfxRenderer& renderer, const MoveEffect effect, const uint8_t frame,
                              const Rect bounds) {
  constexpr int frameWidth = 144;
  constexpr int frameHeight = 108;
  const int width = std::min(frameWidth, bounds.width * 6 / 5);
  const int height = width * frameHeight / frameWidth;
  const int left = bounds.x + (bounds.width - width) / 2;
  const int top = bounds.y + (bounds.height - height) / 2;
  char path[64]{};
  if (pokemonMoveEffectArtPath(effect, frame, path, sizeof(path)) == nullptr) return false;
  FsFile file;
  if (!Storage.openFileForRead("PKART", path, file)) {
    LOG_ERR("PKART", "Missing Pokemon art: %s", path);
    return false;
  }
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
    LOG_ERR("PKART", "Invalid Pokemon art: %s", path);
    file.close();
    return false;
  }
  // First a white outline around every inked pixel, so the effect still
  // reads where it lands on the (equally dithered) Pokemon sprite, then the
  // effect itself.
  const float scale = std::min(1.0f, std::min(static_cast<float>(width) / static_cast<float>(bitmap.getWidth()),
                                              static_cast<float>(height) / static_cast<float>(bitmap.getHeight())));
  const int halo = scale >= 0.75f ? 2 : 1;
  std::vector<uint8_t> outputRow(static_cast<size_t>((bitmap.getWidth() + 3) / 4));
  std::vector<uint8_t> rowBytes(static_cast<size_t>(bitmap.getRowBytes()));
  for (int bmpY = 0; bmpY < bitmap.getHeight(); ++bmpY) {
    if (bitmap.readNextRow(outputRow.data(), rowBytes.data()) != BmpReaderError::Ok) break;
    const int sourceY = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
    const int screenY = top + static_cast<int>(sourceY * scale);
    for (int bmpX = 0; bmpX < bitmap.getWidth(); ++bmpX) {
      const uint8_t value = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;
      if (value >= 3) continue;
      const int screenX = left + static_cast<int>(bmpX * scale);
      renderer.fillRect(screenX - halo, screenY - halo, 2 * halo + 1, 2 * halo + 1, false);
    }
  }
  const bool drawn = bitmap.rewindToData() == BmpReaderError::Ok && renderer.drawBitmap(bitmap, left, top, width, height);
  file.close();
  return drawn;
}

bool pokemonMoveEffectArtInstalled(const MoveEffect effect) {
  char path[64]{};
  return pokemonMoveEffectArtPath(effect, 0, path, sizeof(path)) != nullptr && Storage.exists(path);
}

bool drawPokemonTrainerClassArt(const GfxRenderer& renderer, const uint8_t classId, const Rect bounds,
                                const bool fallback) {
  char path[64]{};
  return drawPath(renderer, pokemonTrainerClassArtPath(classId, path, sizeof(path)), bounds, fallback);
}

}  // namespace pokemon

#endif
