#pragma once

#include <HalStorage.h>
#include <PokemonSaveBundleCodec.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace pokemon {

// SD-card side of moving a whole Pokemon save between two devices (the radio
// side is PokemonSaveTransferActivity). See PokemonSaveBundleCodec.h for the
// bundle layout.
//
// Receiving is a small journal so a power cut at any point leaves either the
// old save or the new one, never a mix of the two:
//
//   1. the bundle streams into  pokemon-transfer.part
//   2. verifyStagedBundle(): table CRC, format versions, every file's CRC
//   3. commitStagedBundle(): clears the previous backup, renames
//      .part -> pokemon-transfer.ready  (the commit point - from here on the
//      new save WILL be installed, at the latest on the next boot)
//   4. applyPendingSaveTransfer():
//        a. moves every current save file into pokemon-backup/, then renames
//           .ready -> pokemon-transfer.installing
//        b. writes each bundled file to <name>.tmp, renames it into place,
//           then deletes .installing
//      Both steps are idempotent, and devicePokemonService() runs this before
//      any store is touched, so a half-finished install completes on boot.
namespace save_transfer {

constexpr const char* STAGING_PATH = "/.crosspoint/pokemon-transfer.part";
constexpr const char* READY_PATH = "/.crosspoint/pokemon-transfer.ready";
constexpr const char* INSTALLING_PATH = "/.crosspoint/pokemon-transfer.installing";
constexpr const char* BACKUP_DIRECTORY = "/.crosspoint/pokemon-backup";

// True when this device has a main save snapshot on SD.
bool hasLocalSave();

// Streams this device's save as one bundle, file after file, without ever
// holding more than one chunk in memory.
class BundleSource {
 public:
  // Scans the save files and checksums each one. False if there is no save.
  bool open();
  void close();
  uint32_t totalBytes() const { return static_cast<uint32_t>(header_.totalBytes()); }
  const SaveBundleHeader& header() const { return header_; }
  // Next bytes of the bundle: >0 bytes copied, 0 at the end, -1 on a read error.
  int read(uint8_t* output, size_t capacity);

 private:
  SaveBundleHeader header_{};
  std::array<uint8_t, SAVE_BUNDLE_MAX_TABLE_BYTES> table_{};
  size_t tableBytes_ = 0;
  size_t tablePosition_ = 0;
  uint8_t entryIndex_ = 0;
  uint32_t entryRemaining_ = 0;
  HalFile file_;
  bool open_ = false;
};

enum class VerifyResult : uint8_t { Ok, Corrupt, UnsupportedVersion, ReadError };

// Checks the fully received STAGING_PATH; `header` gets its table on success.
VerifyResult verifyStagedBundle(SaveBundleHeader& header);
// Step 3 above. Only call after verifyStagedBundle() returned Ok.
bool commitStagedBundle();
void discardStagedBundle();
// Step 4 above. True when nothing is pending or the install completed.
bool applyPendingSaveTransfer();
// The sender's "move" option: puts its own save into BACKUP_DIRECTORY (so a
// mistake is still recoverable by hand) and leaves the device with no save.
bool moveLocalSaveToBackup();

}  // namespace save_transfer
}  // namespace pokemon
