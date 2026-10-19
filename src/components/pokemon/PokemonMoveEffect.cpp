#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonMoveEffect.h"

#include <cstdio>

namespace pokemon {
namespace {

MoveEffect effectForType(const PokemonType type) {
  switch (type) {
    case PokemonType::Fire:
      return MoveEffect::Flame;
    case PokemonType::Water:
      return MoveEffect::Water;
    case PokemonType::Ice:
      return MoveEffect::Ice;
    case PokemonType::Electric:
      return MoveEffect::Bolt;
    case PokemonType::Grass:
    case PokemonType::Bug:
      return MoveEffect::Leaf;
    case PokemonType::Poison:
      return MoveEffect::Poison;
    case PokemonType::Ground:
    case PokemonType::Rock:
      return MoveEffect::Rock;
    case PokemonType::Flying:
      return MoveEffect::Wind;
    case PokemonType::Psychic:
      return MoveEffect::Psychic;
    case PokemonType::Ghost:
      return MoveEffect::Ghost;
    case PokemonType::Dragon:
      return MoveEffect::Dragon;
    default:
      return MoveEffect::Hit;
  }
}

}  // namespace

MoveEffectStep moveEffectFor(const BattleCombatant& actor, const BattleActionResult& action) {
  if (!action.acted) return {};
  switch (action.event) {
    case BattleLogEvent::StatRaised:
    case BattleLogEvent::BuffApplied:
    case BattleLogEvent::SubstituteUp:
      return {MoveEffect::Sparkle, true};
    case BattleLogEvent::MoveHit:
    case BattleLogEvent::MoveSuperEffective:
    case BattleLogEvent::MoveNotVeryEffective:
    case BattleLogEvent::OneHitKo:
    case BattleLogEvent::InflictedStatus:
    case BattleLogEvent::StatLowered:
    case BattleLogEvent::Seeded:
    case BattleLogEvent::MoveDisabled:
      break;
    default:
      return {};
  }
  const uint8_t moveId =
      action.moveSlot >= BATTLE_MOVE_SLOTS ? STRUGGLE_MOVE_ID : actor.moves[action.moveSlot].moveId;
  const MoveData* move = moveData(moveId);
  return {move == nullptr ? MoveEffect::Hit : effectForType(move->type), false};
}

const char* moveEffectSlug(const MoveEffect effect) {
  switch (effect) {
    case MoveEffect::Hit:
      return "hit";
    case MoveEffect::Flame:
      return "flame";
    case MoveEffect::Water:
      return "water";
    case MoveEffect::Ice:
      return "ice";
    case MoveEffect::Bolt:
      return "bolt";
    case MoveEffect::Leaf:
      return "leaf";
    case MoveEffect::Poison:
      return "poison";
    case MoveEffect::Rock:
      return "rock";
    case MoveEffect::Wind:
      return "wind";
    case MoveEffect::Psychic:
      return "psychic";
    case MoveEffect::Ghost:
      return "ghost";
    case MoveEffect::Dragon:
      return "dragon";
    case MoveEffect::Sparkle:
      return "sparkle";
    case MoveEffect::None:
      break;
  }
  return nullptr;
}

uint8_t moveEffectFrameCount(const MoveEffect effect) {
  switch (effect) {
    case MoveEffect::Hit:
    case MoveEffect::Leaf:
    case MoveEffect::Psychic:
      return 3;
    case MoveEffect::Ice:
    case MoveEffect::Poison:
    case MoveEffect::Sparkle:
      return 4;
    case MoveEffect::Flame:
    case MoveEffect::Water:
    case MoveEffect::Bolt:
    case MoveEffect::Rock:
    case MoveEffect::Wind:
    case MoveEffect::Ghost:
    case MoveEffect::Dragon:
      return 5;
    case MoveEffect::None:
      break;
  }
  return 0;
}

const char* pokemonMoveEffectArtPath(const MoveEffect effect, const uint8_t frame, char* output,
                                     const size_t outputSize) {
  if (output == nullptr || outputSize == 0) return nullptr;
  output[0] = '\0';
  const char* slug = moveEffectSlug(effect);
  if (slug == nullptr || frame >= moveEffectFrameCount(effect)) return nullptr;
  const int written =
      std::snprintf(output, outputSize, "/pokemon/effects/%s/%02u.bmp", slug, static_cast<unsigned>(frame + 1));
  if (written < 0 || static_cast<size_t>(written) >= outputSize) {
    output[0] = '\0';
    return nullptr;
  }
  return output;
}

}  // namespace pokemon

#endif
