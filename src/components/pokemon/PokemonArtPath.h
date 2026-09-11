#pragma once

#include <PokemonTypes.h>

#include <cstddef>
#include <cstdint>

namespace pokemon {

const char* pokemonSpeciesArtPath(uint16_t speciesId, bool hero, char* output, size_t outputSize);
const char* pokemonSpeciesBackArtPath(uint16_t speciesId, char* output, size_t outputSize);
const char* pokemonPokedexArtPath(uint16_t speciesId, bool landscape, char* output, size_t outputSize);
const char* pokemonItemArtPath(EvolutionItem item, bool hero, char* output, size_t outputSize);
const char* pokemonBagItemArtPath(uint8_t itemId, char* output, size_t outputSize);
const char* pokemonBadgeArtPath(uint8_t gymIndex, char* output, size_t outputSize);
const char* pokemonTrainerArtPath(uint8_t gymIndex, char* output, size_t outputSize);

}  // namespace pokemon
