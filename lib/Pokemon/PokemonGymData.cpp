#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonBattleTypes.h"
#include "PokemonGyms.generated.h"

namespace pokemon {

// PokemonTypes.h pins its own copy of this bound (POKEMON_GYM_PROGRESS_BITS)
// since it cannot include this battle layer without inverting the
// dependency direction; keep the two in sync.
static_assert(POKEMON_GYM_PROGRESS_BITS == GYM_COUNT);

const GymData* gymData(const uint8_t gymIndex) {
  if (gymIndex == 0 || gymIndex > GYM_COUNT) return nullptr;
  return &generated::GYMS[gymIndex - 1U];
}

std::span<const GymTeamMember> gymTeamFor(const uint8_t gymIndex) {
  if (gymIndex == 0 || gymIndex > GYM_COUNT) return {};
  const GymData& gym = generated::GYMS[gymIndex - 1U];
  return {generated::GYM_TEAM_MEMBERS + gym.teamOffset, gym.teamCount};
}

GymProgress gymProgressFor(const uint16_t battleProgress, const uint8_t gymIndex) {
  if (gymIndex == 0 || gymIndex > GYM_COUNT) return GymProgress::Locked;
  const uint16_t bit = static_cast<uint16_t>(1U << (gymIndex - 1U));
  if ((battleProgress & bit) != 0) return GymProgress::Defeated;
  if (gymIndex <= 8U) {
    const uint16_t requiredMask = static_cast<uint16_t>(bit - 1U);  // every earlier gym bit
    return (battleProgress & requiredMask) == requiredMask ? GymProgress::Available : GymProgress::Locked;
  }
  constexpr uint16_t ALL_GYMS_MASK = 0x00FFU;  // bits 0-7: all 8 gyms
  return (battleProgress & ALL_GYMS_MASK) == ALL_GYMS_MASK ? GymProgress::Available : GymProgress::Locked;
}

}  // namespace pokemon

#endif
