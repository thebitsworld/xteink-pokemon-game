#pragma once

#include <cstdint>
#include <string>

// ESP.restart() with an RTC_NOINIT flag that survives the reboot, so setup()
// skips the boot splash and routes straight to a destination. Used to clear
// heap fragmentation accumulated during a wifi session.

enum class NetworkBootTarget : uint32_t {
  OTA = 2,
  OPDS = 3,
  KOREADER_SYNC = 4,
  KOREADER_AUTH = 5,
  FILE_TRANSFER = 6,
  MANAGE_FONTS = 7,
};

constexpr bool isNetworkBootTargetValue(const uint32_t value) {
  switch (static_cast<NetworkBootTarget>(value)) {
    case NetworkBootTarget::OTA:
    case NetworkBootTarget::OPDS:
    case NetworkBootTarget::KOREADER_SYNC:
    case NetworkBootTarget::KOREADER_AUTH:
    case NetworkBootTarget::FILE_TRANSFER:
    case NetworkBootTarget::MANAGE_FONTS:
      return true;
  }
  return false;
}

static_assert(isNetworkBootTargetValue(static_cast<uint32_t>(NetworkBootTarget::OTA)) &&
                  isNetworkBootTargetValue(static_cast<uint32_t>(NetworkBootTarget::OPDS)) &&
                  isNetworkBootTargetValue(static_cast<uint32_t>(NetworkBootTarget::KOREADER_SYNC)) &&
                  isNetworkBootTargetValue(static_cast<uint32_t>(NetworkBootTarget::KOREADER_AUTH)) &&
                  isNetworkBootTargetValue(static_cast<uint32_t>(NetworkBootTarget::FILE_TRANSFER)) &&
                  isNetworkBootTargetValue(static_cast<uint32_t>(NetworkBootTarget::MANAGE_FONTS)),
              "Every network boot target must pass RTC target validation");

// Whether this target's parent screen keeps rendering while its deferred
// Wi-Fi child activity (always a WifiSelectionActivity connect/scan) runs, so
// needs the larger reader-sized render-task stack on S3 devices instead of
// the tighter generic network one - see main.cpp's useReaderRenderStack.
// A switch with no default (rather than an OR-chain of ==) means adding a
// 7th NetworkBootTarget without deciding this here warns (-Wswitch, enabled
// project-wide in platformio.ini) instead of silently landing on the smaller
// stack with no signal at all, the exact way OTA and KOREADER_AUTH were once
// missed and overflowed that stack on X4 Pro.
constexpr bool keepsReaderRenderStackOnS3(const NetworkBootTarget target) {
  switch (target) {
    case NetworkBootTarget::OTA:
    case NetworkBootTarget::OPDS:
    case NetworkBootTarget::KOREADER_SYNC:
    case NetworkBootTarget::KOREADER_AUTH:
    case NetworkBootTarget::FILE_TRANSFER:
    case NetworkBootTarget::MANAGE_FONTS:
      return true;
  }
  return false;
}

void silentRestart();                                            // home screen
void silentRestartToReader(bool cleanImageBaseOnEntry = false);  // currently-open EPUB (APP_STATE.openEpubPath)
// Reboots immediately after an activity releases exclusive raw storage.
void restartToHomeAfterStorageHandoff();
void silentRestartToNetwork(NetworkBootTarget target, uint32_t payload = 0);
void silentRestartToManageFonts();

void armSilentRestartReaderPageBuild(const std::string& bookPath, uint16_t spineIndex, uint16_t targetPage,
                                     bool autoPageTurnActive);
bool consumeSilentRestartReaderPageBuild(const std::string& bookPath, uint16_t& spineIndex, uint16_t& targetPage,
                                         bool& autoPageTurnActive);
