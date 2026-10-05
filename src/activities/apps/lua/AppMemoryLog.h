#pragma once

#include <cstdint>
#include <string>

namespace ink {
class LuaRunner;
}

// Appends one line per Lua app session to app_paths::MEMORY_LOG on the SD
// card, so the memory an app really has on a device can be read without a USB
// serial connection. Columns are described by the file's header line.
namespace app_memory_log {

struct OpenSnapshot {
  uint32_t freeHeap = 0;
  uint32_t maxAlloc = 0;
};

// System heap just before the app's Lua state is created.
OpenSnapshot takeOpenSnapshot();

// Call after the runner has shut down, so the write has the app's memory back.
void recordSession(const std::string& appId, const OpenSnapshot& open, const ink::LuaRunner& runner, bool hadError);

}  // namespace app_memory_log
