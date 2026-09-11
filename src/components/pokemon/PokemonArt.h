#pragma once

#include <PokemonTypes.h>

#include "GfxRenderer.h"
#include "components/themes/BaseTheme.h"

namespace pokemon {

bool drawPokemonSpeciesArt(const GfxRenderer& renderer, uint16_t speciesId, bool hero, Rect bounds,
                           bool drawFallback = true);
bool drawPokemonSpeciesBackArt(const GfxRenderer& renderer, uint16_t speciesId, Rect bounds,
                               bool drawFallback = true);
bool drawPokemonPokedexArt(const GfxRenderer& renderer, uint16_t speciesId, bool landscape, Rect bounds,
                           bool drawFallback = true);
bool drawPokemonItemArt(const GfxRenderer& renderer, EvolutionItem item, bool hero, Rect bounds,
                        bool drawFallback = true);
bool drawPokemonBagItemArt(const GfxRenderer& renderer, uint8_t itemId, Rect bounds, bool drawFallback = true);
bool drawPokemonBadgeArt(const GfxRenderer& renderer, uint8_t gymIndex, Rect bounds, bool drawFallback = true);

}  // namespace pokemon
