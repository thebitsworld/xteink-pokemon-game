#pragma once

#if defined(CROSSINK_ENABLE_POKEMON)

#include <PokemonBattleStoreCodec.h>

namespace pokemon {

// Reads/writes /.crosspoint/pokemon-battle-{a,b}.bin: the live HP/PP/status
// and moveset for Party members currently in a fight. See
// docs/file-formats.md. A Pokemon's actual 4 moves can now diverge from what
// would be reconstructed from its level (TM/HM teaching, the Moveset
// screen's learn/forget), so this data is no longer purely a cache of the
// main save - a lost file can silently revert a player's move choices, not
// just reset HP/PP. It is therefore double-buffered the same way as
// pokemon-{a,b}.bin: a write always lands on the currently-inactive file,
// verified by reading it back, before the active pointer flips. A missing
// or corrupt file (in a fresh install, or if both slots are ever lost) still
// just leaves the store empty rather than reporting failure - there is
// nothing for a caller to recover from - but that is now a fallback of last
// resort rather than the everyday case.
class PokemonBattleStore {
 public:
  // Reads whichever of the two files is newest (or migrates a pre-existing
  // single-file pokemon-battle.bin the first time). Called automatically by
  // every other method on first use (idempotent, so an explicit call is only
  // needed to force a re-read).
  void load() const;
  const BattleRecordEntry* findEntry(uint32_t recordId) const;
  // Inserts/replaces/removes an entry and rewrites the whole (inactive) file,
  // then flips the active pointer only after a read-back verifies it.
  // Returns false only for a validation failure (see upsertBattleEntry) or a
  // write failure; the in-memory state and both on-disk files are left as
  // they were before the call in either case.
  bool upsertEntry(const BattleRecordEntry& entry);
  bool removeEntry(uint32_t recordId);
  // Writes an empty state (same double-buffer commit as upsertEntry/
  // removeEntry - lands on the currently-inactive slot, verified, then flips
  // active). Used by PokemonService::reset() so a fresh game doesn't read
  // back a previous playthrough's leftover HP/PP/moveset once record ids
  // start over from 1 again.
  bool reset();

 private:
  bool writeState(const BattleStoreState& state) const;

  mutable BattleStoreState state_{};
  mutable bool loaded_ = false;
  mutable bool ready_ = false;      // at least one of the two files currently holds a valid, decoded state
  mutable bool activeIsA_ = false;  // which of the two files ready_ refers to
  mutable uint32_t sequence_ = 0;   // sequence number of the active file; 0 when !ready_
};

PokemonBattleStore& devicePokemonBattleStore();

}  // namespace pokemon

#endif
