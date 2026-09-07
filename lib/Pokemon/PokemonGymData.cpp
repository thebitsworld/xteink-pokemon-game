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

}  // namespace pokemon

#endif
