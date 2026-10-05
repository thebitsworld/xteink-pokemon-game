#include "activities/apps/lua/AppMemoryLog.h"

#include <AppVersion.h>
#include <Arduino.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>

#include "activities/apps/lua/AppPaths.h"
#include "activities/apps/lua/LuaRunner.h"

namespace app_memory_log {
namespace {

constexpr char LOG_HEADER[] =
    "# version,device,app,free_before,max_alloc_before,lua_limit,lua_peak,free_min,refused,result\n";
constexpr size_t MAX_LOG_BYTES = 64u * 1024u;

const char* deviceName() {
#if defined(BOARD_HAS_PSRAM)
  return "x4-pro";
#else
  return gpio.deviceIsX3() ? "x3" : "x4";
#endif
}

}  // namespace

OpenSnapshot takeOpenSnapshot() {
  OpenSnapshot s;
  s.freeHeap = ESP.getFreeHeap();
  s.maxAlloc = ESP.getMaxAllocHeap();
  return s;
}

void recordSession(const std::string& appId, const OpenSnapshot& open, const ink::LuaRunner& runner,
                   const bool hadError) {
  char line[192];
  const int len =
      snprintf(line, sizeof(line), "%s,%s,%s,%u,%u,%u,%u,%u,%u,%s\n", AppVersion::version(), deviceName(),
               appId.c_str(), static_cast<unsigned>(open.freeHeap), static_cast<unsigned>(open.maxAlloc),
               static_cast<unsigned>(runner.luaHeapLimitBytes()), static_cast<unsigned>(runner.luaPeakBytes()),
               static_cast<unsigned>(runner.minFreeHeapBytes()), static_cast<unsigned>(runner.refusedAllocations()),
               hadError ? "error" : "ok");
  if (len <= 0 || static_cast<size_t>(len) >= sizeof(line)) return;
  LOG_INF("LUA", "memory: %s", line);

  Storage.ensureDirectoryExists("/.crosspoint");
  HalFile file = Storage.open(app_paths::MEMORY_LOG, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("LUA", "Failed to open %s for append", app_paths::MEMORY_LOG);
    return;
  }
  const size_t existing = file.size();
  if (existing >= MAX_LOG_BYTES) {
    file.close();
    return;
  }
  if (existing == 0) file.write(LOG_HEADER, sizeof(LOG_HEADER) - 1);
  file.write(line, static_cast<size_t>(len));
  file.close();
}

}  // namespace app_memory_log
