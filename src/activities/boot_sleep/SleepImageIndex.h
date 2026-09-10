#pragma once

#include <cstdint>
#include <string>

class CrossPointState;

namespace SleepImageIndex {

struct Selection {
  std::string path;
  bool isPng = false;
  uint16_t index = 0;
};

// Select one image from the cached directory index. The cache is rebuilt when
// it is missing, stale, or when header validation is requested after a failed
// render. A cache failure returns false so callers can retain their directory
// scan fallback and never make sleep depend on a writable cache.
bool select(const std::string& directory, bool includePng, bool validateBmpHeaders, const CrossPointState& state,
            uint8_t recentWindow, Selection& selection);

// Drop all indexes. This only removes files owned by this module.
void invalidate();

// Invalidate only when a successful file mutation can affect a configured or
// built-in sleep-image folder. Paths are expected to be normalized absolute SD
// paths, as provided by the storage/file-transfer callers.
void invalidateForPath(const char* path);

}  // namespace SleepImageIndex
