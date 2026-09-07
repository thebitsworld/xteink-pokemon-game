#pragma once

#if defined(CROSSINK_ENABLE_POKEMON)

#include <PokemonBattleStoreCodec.h>

namespace pokemon {

// Reads/writes /.crosspoint/pokemon-battle.bin: the live HP/PP/status for
// Party members currently in a fight. See docs/file-formats.md. Unlike
// PokemonStore (the main double-buffered save), this file is fully
// reconstructible, so a single load() that tolerates and silently discards
// any corruption is sufficient - callers never see an error, only an
// "entry not found, rebuild it" outcome from findEntry().
class PokemonBattleStore {
 public:
  // Reads the file if present. Called automatically by every other method
  // on first use (idempotent, so an explicit call is only needed to force a
  // re-read). Always succeeds from the caller's point of view: a missing,
  // truncated, or CRC-mismatched file just leaves the store empty rather
  // than reporting failure - there is nothing for a caller to recover from.
  void load() const;
  const BattleRecordEntry* findEntry(uint32_t recordId) const;
  // Inserts/replaces/removes an entry and rewrites the whole file. Returns
  // false only for a validation failure (see upsertBattleEntry) or a write
  // failure; the in-memory state and on-disk file are left as they were
  // before the call in either case.
  bool upsertEntry(const BattleRecordEntry& entry);
  bool removeEntry(uint32_t recordId);

 private:
  bool writeFile() const;

  mutable BattleStoreState state_{};
  mutable bool loaded_ = false;
};

PokemonBattleStore& devicePokemonBattleStore();

}  // namespace pokemon

#endif
