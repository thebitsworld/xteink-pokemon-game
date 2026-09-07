#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonBattleTypes.h"
#include "PokemonMoves.generated.h"

namespace pokemon {

const MoveData* moveData(const uint8_t moveId) {
  if (moveId == 0 || moveId > MOVE_COUNT) return nullptr;
  return &generated::MOVES[moveId - 1U];
}

}  // namespace pokemon

#endif
