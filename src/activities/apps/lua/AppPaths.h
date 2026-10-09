#pragma once

#include <HalStorage.h>

#include <string>
#include <vector>

// Where the Lua app platform keeps its files on the SD card. Everything lives
// under the hidden /.crosspoint folder, so the Library and the file browser
// (which skip dot-entries) never list an app's .txt data files as books.
namespace app_paths {

// Installed apps, one folder per app id (App Store and web manager install here).
constexpr const char* INSTALL_DIR = "/.crosspoint/apps";
// Per-app saved data (smudge.save/load), one folder per app id.
constexpr const char* DATA_DIR = "/.crosspoint/apps-data";
// Applications list settings (sort order, usage scores).
constexpr const char* SETTINGS_FILE = "/.crosspoint/apps-settings.bin";
// App Store download cache: catalog.tmp and icons/<id>.raw.
constexpr const char* CACHE_DIR = "/.crosspoint/cache/apps";
// One line per app session with the memory it had (AppMemoryLog).
constexpr const char* MEMORY_LOG = "/.crosspoint/apps-memory.txt";

// Folders scanned for installed apps, INSTALL_DIR first. The others are only
// read, so apps copied by hand (or an SD card from CrossSmudge) still show up.
constexpr const char* SCAN_DIRS[] = {
    INSTALL_DIR, "/.crosspoint/applications", "/.crosssmudge/applications", "/apps", "/applications",
    "/.smudge/applications",
};

// This reader as an app's "devices" list (manifest.json, catalog.json) and
// smudge.get_device() name it: "x3" for the X3/X4 firmware (ESP32-C3, no
// PSRAM, the 75 KB app budget), "x4pro", "x4" (X4 classic) or "sticky". A
// simulator answers as the reader it simulates.
inline const char* deviceId() {
#if defined(FREEINK_DEVICE_X4PRO) || defined(SIMULATOR_DEVICE_X4_PRO)
  return "x4pro";
#elif defined(FREEINK_DEVICE_STICKY)
  return "sticky";
#elif defined(FREEINK_DEVICE_X4CLASSIC)
  return "x4";
#else
  return "x3";
#endif
}

// Whether an app whose "devices" list is `devices` runs here; an app without
// the list runs everywhere.
inline bool supportsThisDevice(const std::vector<std::string>& devices) {
  if (devices.empty()) return true;
  for (const auto& d : devices) {
    if (d == deviceId()) return true;
  }
  return false;
}

inline std::string installDirFor(const std::string& appId) { return std::string(INSTALL_DIR) + "/" + appId; }

// The folder holding app `appId`, or "" when it is not installed anywhere.
inline std::string findAppDir(const std::string& appId) {
  for (const char* base : SCAN_DIRS) {
    const std::string path = std::string(base) + "/" + appId;
    if (Storage.exists(path.c_str())) return path;
  }
  return "";
}

}  // namespace app_paths
