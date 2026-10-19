#pragma once

#if defined(CROSSINK_ENABLE_POKEMON)

#include <PokemonBattle.h>

#include <cstdint>

namespace pokemon {

// Battle move-effect animations: the frames of an original Red/Blue move
// animation (pokemon/effects/<slug>/NN.bmp, 144x108, built by
// scripts/generate_pokemon_effect_art.py), played over a move's target.
enum class MoveEffect : uint8_t {
  None = 0,
  Hit,
  Flame,
  Water,
  Ice,
  Bolt,
  Leaf,
  Poison,
  Rock,
  Wind,
  Psychic,
  Ghost,
  Dragon,
  Sparkle,
};

struct MoveEffectStep {
  MoveEffect effect = MoveEffect::None;
  // Where the icon goes: the other side for an attack or a debuff, the
  // actor itself for a self-buff (Swords Dance, Reflect, ...).
  bool onSelf = false;
};

// The effect one side's action shows, or MoveEffect::None when nothing
// visible happened (a miss, a failed move, a status that kept it from moving).
MoveEffectStep moveEffectFor(const BattleCombatant& actor, const BattleActionResult& action);

const char* moveEffectSlug(MoveEffect effect);
// How many frames an effect has - EFFECTS' `frames` in the generator script.
uint8_t moveEffectFrameCount(MoveEffect effect);
// Path of frame `frame` (0-based) of an effect.
const char* pokemonMoveEffectArtPath(MoveEffect effect, uint8_t frame, char* output, size_t outputSize);

}  // namespace pokemon

#endif
