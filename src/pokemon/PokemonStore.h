#pragma once

#include <PokemonGame.h>
#include <PokemonStoreCodec.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace pokemon {

enum class StoreBeginResult : uint8_t {
  Empty,
  Ready,
  Corrupt,
  Unsupported,
};

enum class PcOrder : uint8_t {
  CatchDate,
  PokedexNumber,
  Alphabetical,
};

class PokemonStore {
 public:
  StoreBeginResult begin();
  bool isReady() const { return ready_; }
  uint32_t recordCount() const { return ready_ ? activeHeader_.recordCount : 0; }
  // The id a newly-created record should use. NOT recordCount() + 1 - once a
  // record can be removed (RecordMutationKind::Remove), recordCount() drops
  // but existing ids do not shift down, so ids are no longer dense and
  // recordCount() + 1 can collide with a still-live record. highestRecordId_
  // is tracked precisely because of this - see inspectSnapshot()'s own doc
  // comment in the .cpp.
  uint32_t nextRecordId() const { return highestRecordId_ + 1U; }
  bool loadState(PokemonState& output) const;
  bool readRecord(uint32_t recordId, PokemonRecord& output) const;
  // Looks up multiple record ids in a single forward scan of the file,
  // instead of one independent open+scan per id via readRecord() -
  // loadSnapshot()/healPartyOnRead() both used to do exactly that, once per
  // party member (see docs/development/pokemon-gen1-audit-round3.md item
  // 3.1 and round4's item 3.2). recordIds and output must be the same
  // size - returns false (no writes at all) otherwise. A requested id of 0,
  // or one that doesn't exist in the file, leaves that slot's output entry
  // default-constructed (recordId == 0) - the same per-id "not found"
  // contract readRecord() already has, just resolved for every id in one
  // pass instead of N. The overall bool return is about the SCAN itself
  // (store not ready, or a decode error partway through) - a merely-missing
  // id is not a failure and does not stop other ids in the same call from
  // being found; callers check each output[i].recordId individually for
  // that.
  bool readRecords(std::span<const uint32_t> recordIds, std::span<PokemonRecord> output) const;
  bool loadOwnedEvolutionNeeds(OwnedEvolutionNeeds& output) const;
  bool readPcPage(PcOrder order, size_t offset, std::span<PokemonRecord> output, size_t& count) const;
  bool commit(const PokemonState& state, const RecordMutation& mutation = {});
  bool reset();

 private:
  bool writeSnapshot(const PokemonState& state, const RecordMutation& mutation, bool discardRecords);

  SnapshotHeader activeHeader_{};
  uint32_t highestRecordId_ = 0;
  bool activeIsA_ = false;
  bool ready_ = false;
  bool writable_ = false;
};

}  // namespace pokemon
