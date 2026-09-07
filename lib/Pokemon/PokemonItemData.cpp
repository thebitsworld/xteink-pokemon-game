#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonBattleTypes.h"
#include "PokemonItems.generated.h"

namespace pokemon {

const ItemData* itemData(const uint8_t itemId) {
  if (itemId == 0 || itemId > ITEM_COUNT) return nullptr;
  return &generated::ITEMS[itemId - 1U];
}

}  // namespace pokemon

#endif
