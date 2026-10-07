#pragma once

#include "CrossPointSettings.h"

// Features the reader can switch off under Settings > System. A switched-off
// feature disappears from Home; a switched-off Pokemon game also stops all of
// its work - no reading credit, no Home snapshot, no save offered to nearby
// readers. All are on by default.
namespace features {

inline bool pokemonGame() {
#if defined(CROSSINK_ENABLE_POKEMON)
  return SETTINGS.pokemonGameEnabled != 0;
#else
  return false;
#endif
}

inline bool applications() {
#if defined(CROSSINK_ENABLE_POKEMON) && defined(CROSSINK_ENABLE_LUA_APPS)
  return SETTINGS.applicationsEnabled != 0;
#else
  return false;
#endif
}

inline bool slideshow() { return SETTINGS.slideshowEnabled != 0; }

}  // namespace features
