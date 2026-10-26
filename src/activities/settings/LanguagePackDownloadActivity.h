#pragma once

#include <string>

#include "activities/Activity.h"
#include "activities/ScreenTransitionRefresh.h"

// Settings > Language > "Download more languages": joins Wi-Fi, downloads the
// latest release's language pack to the SD card (LanguagePackStore::PACK_PATH)
// and restarts, which installs it at boot. Opened after a network boot
// (NetworkBootTarget::DOWNLOAD_LANGUAGES) so Wi-Fi + TLS get a whole heap.
class LanguagePackDownloadActivity final : public Activity {
 public:
  LanguagePackDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LanguagePackDownload", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state_ == DOWNLOADING; }
  bool skipLoopDelay() override { return true; }

 private:
  enum State { WIFI_SELECTION, DOWNLOADING, COMPLETE, ERROR };

  void onWifiSelectionComplete(bool success);
  void download();
  bool pollCancelInput();

  State state_ = WIFI_SELECTION;
  ScreenTransitionRefresh screenTransitionRefresh_;
  size_t downloaded_ = 0;
  size_t total_ = 0;
  bool cancelRequested_ = false;
  bool restartRequested_ = false;
};
