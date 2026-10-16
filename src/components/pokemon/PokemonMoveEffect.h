#pragma once

#if defined(CROSSINK_ENABLE_POKEMON)

#include <PokemonBattle.h>

#include <cstdint>

namespace pokemon {

// Battle move-effect icons (pokemon/effects/<slug>.bmp, drawn by
// scripts/generate_pokemon_effect_art.py), shown over a move's target.
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
const char* pokemonMoveEffectArtPath(MoveEffect effect, char* output, size_t outputSize);

}  // namespace pokemon

#endif
