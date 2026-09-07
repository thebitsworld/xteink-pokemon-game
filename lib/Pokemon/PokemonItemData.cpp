#if defined(CROSSINK_ENABLE_POKEMON)

#include "PokemonBattleTypes.h"
#include "PokemonItems.generated.h"

namespace pokemon {

// PokemonTypes.h pins its own copy of this bound (POKEMON_BAG_SLOT_COUNT,
// the non-stone item count) since it cannot include this battle layer
// without inverting the dependency direction; keep the two in sync.
static_assert(POKEMON_BAG_SLOT_COUNT + 6U == ITEM_COUNT);

const ItemData* itemData(const uint8_t itemId) {
  if (itemId == 0 || itemId > ITEM_COUNT) return nullptr;
  return &generated::ITEMS[itemId - 1U];
}

}  // namespace pokemon

#endif
