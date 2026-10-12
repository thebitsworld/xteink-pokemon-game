#pragma once

// FreeInk — encrypted-entry read path.
//
// Access-only, by design:
//  - content stays encrypted at rest (items are accessed on read, into memory)
//  - the content key comes from the caller (setContentKey); how it is
//    obtained (a rights scheme, a key file) lives outside this lib
// There is deliberately no API that writes the content out in the clear.
//
// Freestanding C++17. Crypto and storage are injected.

#include <stdint.h>

#include <string>
#include <vector>

#include "ByteSource.h"
#include "ContentProtection.h"
#include "Crypto.h"
#include "Zip.h"

namespace freeink {
namespace content {

class ProtectedBook {
 public:
  // Opens a (possibly protected) EPUB: scans the container and, when
  // META-INF/encryption.xml lists encrypted entries, marks it protected.
  bool open(ByteSource& source);

  // Completes open using an already-scanned ZIP index. This lets an embedding
  // application classify a plain EPUB before loading any key material, then
  // transfer ownership of that same index instead of scanning the central
  // directory a second time.
  bool openFromScan(ByteSource& source, ZipScan&& scan);

  // True when the container has encrypted entries (font obfuscation alone
  // does not count).
  bool isProtected() const { return protected_; }
  const std::string& lastError() const { return lastError_; }

  // The 16-byte AES content key. Required before decryptEntryToSink().
  void setContentKey(const uint8_t key[16]);

  bool isEncrypted(const std::string& name) const;
  size_t decryptedSize(const std::string& name) const;

  // Access + inflate one protected entry, streamed through the sink. The only
  // read path: callers own their buffering, so no whole-entry allocation can
  // hide in here.
  bool decryptEntryToSink(ByteSource& source, Crypto& crypto, const std::string& name,
                          ContentChunkSink sink, void* context);

  // Read a non-encrypted entry fully, inflating when deflated. Capped and
  // OOM-safe: fails (rather than aborting) when the entry is oversized or
  // memory is unavailable.
  bool readEntryInflated(ByteSource& source, const std::string& name, std::string* out);

  // Inflates raw-deflate data (windowBits -15) into a caller-owned buffer of
  // exactly the expected size.
  bool inflateTo(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen);

 private:
  // Reserves lastError_'s buffer up front. Every error string assigned by this
  // class is longer than the SSO buffer, so without this each assignment
  // allocates -- including the ones on the out-of-memory paths, where a failing
  // operator new aborts the firmware under -fno-exceptions. Both open entry
  // points must call it before anything that can fail.
  void reserveErrorBuffer();
  bool finishOpen(ByteSource& source);
  // Stream-parses encryption.xml out of the zip in chunks, keeping only path
  // hashes. The manifest scales with the container's file count, so it is
  // never materialized whole.
  bool scanEncryptionXml(ByteSource& source, const ZipEntryInfo& entry);

  ZipScan zip_;
  // Sorted FNV-1a hashes of the aes128-cbc encrypted entry paths.
  std::vector<uint64_t> encryptedUriHashes_;
  uint8_t bookKey_[16] = {0};
  bool protected_ = false;
  bool hasKey_ = false;
  std::string lastError_;
};

}  // namespace content
}  // namespace freeink
