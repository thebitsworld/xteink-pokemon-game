#pragma once

#include <PokemonTypes.h>

#include "GfxRenderer.h"
#include "PokemonMoveEffect.h"
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
bool drawPokemonTrainerArt(const GfxRenderer& renderer, uint8_t gymIndex, Rect bounds, bool drawFallback = true);
// Frame `frame` of a move-effect animation (144x108), centred on `bounds` (a
// battle sprite's box) and a fifth larger than it, so effects around the
// Pokemon fit.
bool drawPokemonMoveEffectArt(const GfxRenderer& renderer, MoveEffect effect, uint8_t frame, Rect bounds);
// False when the SD-card art pack predates the move animations.
bool pokemonMoveEffectArtInstalled(MoveEffect effect);
bool drawPokemonTrainerClassArt(const GfxRenderer& renderer, uint8_t classId, Rect bounds,
                                bool drawFallback = true);

}  // namespace pokemon
