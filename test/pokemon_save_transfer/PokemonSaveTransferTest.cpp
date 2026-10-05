#include <HalStorage.h>

#include <cstdio>
#include <string>
#include <vector>

#include "PokemonCrc32.h"
#include "PokemonSaveBundleCodec.h"
#include "pokemon/PokemonSaveTransfer.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                                \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::fprintf(stderr, "%s:%d check failed: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                       \
    }                                                                                   \
  } while (false)

namespace st = pokemon::save_transfer;
using Bytes = std::vector<uint8_t>;

Bytes pattern(const size_t size, const uint8_t seed) {
  Bytes bytes(size);
  for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(seed + i * 7);
  return bytes;
}

void put(const std::string& path, const Bytes& bytes) {
  HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  file.write(bytes.data(), bytes.size());
  file.close();
}

Bytes get(const std::string& path) {
  HalFile file = Storage.open(path.c_str(), O_RDONLY);
  if (!file) return {};
  Bytes bytes(static_cast<size_t>(file.fileSize64()));
  file.read(bytes.data(), bytes.size());
  file.close();
  return bytes;
}

std::string live(const char* name) { return std::string("/.crosspoint/") + name; }
std::string backup(const char* name) { return std::string(st::BACKUP_DIRECTORY) + "/" + name; }

// The sending device's save: deliberately not every slot (no B snapshot, no
// IV/EV store), so the receiver's own extra files must not survive.
void putSenderSave() {
  put(live("pokemon-a.bin"), pattern(5000, 1));
  put(live("pokemon-battle-b.bin"), pattern(700, 2));
  put(live("pokemon-hof-a.bin"), pattern(260, 3));
}

Bytes buildBundle() {
  st::BundleSource source;
  CHECK(source.open());
  Bytes bundle;
  uint8_t chunk[333];  // odd size: chunks straddle the table/file boundaries
  for (;;) {
    const int count = source.read(chunk, sizeof(chunk));
    CHECK(count >= 0);
    if (count <= 0) break;
    bundle.insert(bundle.end(), chunk, chunk + count);
  }
  CHECK(bundle.size() == source.totalBytes());
  source.close();
  return bundle;
}

// Receiving device: its own, different save, including files the sender
// does not have.
void putReceiverSave() {
  put(live("pokemon-a.bin"), pattern(4000, 9));
  put(live("pokemon-b.bin"), pattern(4000, 10));
  put(live("pokemon-ivev-a.bin"), pattern(900, 11));
  put(live("pokemon-battle.bin"), pattern(120, 12));
}

void checkSenderSaveInstalled() {
  CHECK(get(live("pokemon-a.bin")) == pattern(5000, 1));
  CHECK(get(live("pokemon-battle-b.bin")) == pattern(700, 2));
  CHECK(get(live("pokemon-hof-a.bin")) == pattern(260, 3));
  CHECK(!Storage.exists(live("pokemon-b.bin").c_str()));
  CHECK(!Storage.exists(live("pokemon-ivev-a.bin").c_str()));
  CHECK(!Storage.exists(live("pokemon-battle.bin").c_str()));
  CHECK(!Storage.exists(st::STAGING_PATH));
  CHECK(!Storage.exists(st::READY_PATH));
  CHECK(!Storage.exists(st::INSTALLING_PATH));
}

void checkReceiverSaveBackedUp() {
  CHECK(get(backup("pokemon-a.bin")) == pattern(4000, 9));
  CHECK(get(backup("pokemon-b.bin")) == pattern(4000, 10));
  CHECK(get(backup("pokemon-ivev-a.bin")) == pattern(900, 11));
  CHECK(get(backup("pokemon-battle.bin")) == pattern(120, 12));
}

void noSaveMeansNoBundle() {
  Storage.clear();
  CHECK(!st::hasLocalSave());
  st::BundleSource source;
  CHECK(!source.open());
}

void bundleCarriesExactlyTheSaveFiles() {
  Storage.clear();
  putSenderSave();
  CHECK(st::hasLocalSave());
  const Bytes bundle = buildBundle();
  pokemon::SaveBundleHeader header;
  CHECK(pokemon::decodeSaveBundleTable(bundle.data(), bundle.size(), header) == pokemon::SaveBundleParse::Ok);
  CHECK(header.entryCount == 3);
  CHECK(header.totalBytes() == bundle.size());
  CHECK(header.versions == pokemon::currentSaveFormatVersions());
}

void verifiedBundleInstallsAndBacksUpTheOldSave() {
  Storage.clear();
  putSenderSave();
  const Bytes bundle = buildBundle();

  Storage.clear();
  putReceiverSave();
  put(backup("pokemon-moves-a.bin"), pattern(50, 99));  // stale, from an older transfer
  put(st::STAGING_PATH, bundle);
  pokemon::SaveBundleHeader header;
  CHECK(st::verifyStagedBundle(header) == st::VerifyResult::Ok);
  CHECK(st::commitStagedBundle());
  CHECK(Storage.exists(st::READY_PATH));
  // Nothing touched the live save before the install step.
  CHECK(get(live("pokemon-a.bin")) == pattern(4000, 9));

  CHECK(st::applyPendingSaveTransfer());
  checkSenderSaveInstalled();
  checkReceiverSaveBackedUp();
  CHECK(!Storage.exists(backup("pokemon-moves-a.bin").c_str()));
  // Nothing left pending: a second run is a no-op.
  CHECK(st::applyPendingSaveTransfer());
  checkSenderSaveInstalled();
}

void corruptOrNewerBundlesAreRejected() {
  Storage.clear();
  putSenderSave();
  Bytes bundle = buildBundle();
  pokemon::SaveBundleHeader header;

  Bytes corrupt = bundle;
  corrupt[corrupt.size() - 10] ^= 0x40U;  // inside the last file's data
  Storage.clear();
  put(st::STAGING_PATH, corrupt);
  CHECK(st::verifyStagedBundle(header) == st::VerifyResult::Corrupt);

  Bytes truncated(bundle.begin(), bundle.end() - 1);
  Storage.clear();
  put(st::STAGING_PATH, truncated);
  CHECK(st::verifyStagedBundle(header) == st::VerifyResult::Corrupt);

  // Rebuild the table with a newer snapshot version (valid CRC).
  pokemon::SaveBundleHeader newer;
  CHECK(pokemon::decodeSaveBundleTable(bundle.data(), bundle.size(), newer) == pokemon::SaveBundleParse::Ok);
  newer.versions.snapshot = static_cast<uint16_t>(newer.versions.snapshot + 1);
  size_t written = 0;
  CHECK(pokemon::encodeSaveBundleTable(newer, bundle.data(), bundle.size(), written));
  Storage.clear();
  put(st::STAGING_PATH, bundle);
  CHECK(st::verifyStagedBundle(header) == st::VerifyResult::UnsupportedVersion);
}

void interruptedInstallFinishesOnTheNextRun() {
  Storage.clear();
  putSenderSave();
  const Bytes bundle = buildBundle();

  Storage.clear();
  putReceiverSave();
  put(st::STAGING_PATH, bundle);
  pokemon::SaveBundleHeader header;
  CHECK(st::verifyStagedBundle(header) == st::VerifyResult::Ok);
  CHECK(st::commitStagedBundle());

  // Power cut halfway through writing the new files.
  Storage.setWriteLimit(5200);
  CHECK(!st::applyPendingSaveTransfer());
  Storage.clearWriteLimit();
  CHECK(Storage.exists(st::INSTALLING_PATH));
  // A new transfer cannot be committed over an unfinished install.
  put(st::STAGING_PATH, bundle);
  CHECK(!st::commitStagedBundle());
  st::discardStagedBundle();

  // "Next boot": finishes, and the backup still holds the original save,
  // not the half-installed one.
  CHECK(st::applyPendingSaveTransfer());
  checkSenderSaveInstalled();
  checkReceiverSaveBackedUp();
}

void unreadableJournalRestoresTheOldSave() {
  Storage.clear();
  putSenderSave();
  const Bytes bundle = buildBundle();

  Storage.clear();
  putReceiverSave();
  put(st::STAGING_PATH, bundle);
  pokemon::SaveBundleHeader header;
  CHECK(st::verifyStagedBundle(header) == st::VerifyResult::Ok);
  CHECK(st::commitStagedBundle());
  Storage.setWriteLimit(5200);
  CHECK(!st::applyPendingSaveTransfer());
  Storage.clearWriteLimit();
  Storage.setByte(st::INSTALLING_PATH, 20, 0xEE);  // table corrupted on SD after the commit

  CHECK(!st::applyPendingSaveTransfer());
  CHECK(!Storage.exists(st::INSTALLING_PATH));
  CHECK(get(live("pokemon-a.bin")) == pattern(4000, 9));
  CHECK(get(live("pokemon-b.bin")) == pattern(4000, 10));
  CHECK(get(live("pokemon-ivev-a.bin")) == pattern(900, 11));
  CHECK(!Storage.exists(live("pokemon-hof-a.bin").c_str()));
}

void movingTheSaveLeavesItOnlyInTheBackup() {
  Storage.clear();
  putSenderSave();
  put(backup("pokemon-ivev-b.bin"), pattern(40, 77));  // stale backup
  CHECK(st::moveLocalSaveToBackup());
  CHECK(!st::hasLocalSave());
  CHECK(!Storage.exists(live("pokemon-battle-b.bin").c_str()));
  CHECK(get(backup("pokemon-a.bin")) == pattern(5000, 1));
  CHECK(get(backup("pokemon-hof-a.bin")) == pattern(260, 3));
  CHECK(!Storage.exists(backup("pokemon-ivev-b.bin").c_str()));
}

}  // namespace

int main() {
  noSaveMeansNoBundle();
  bundleCarriesExactlyTheSaveFiles();
  verifiedBundleInstallsAndBacksUpTheOldSave();
  corruptOrNewerBundlesAreRejected();
  interruptedInstallFinishesOnTheNextRun();
  unreadableJournalRestoresTheOldSave();
  movingTheSaveLeavesItOnlyInTheBackup();
  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}
