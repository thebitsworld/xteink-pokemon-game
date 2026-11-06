#pragma once

#include <string>

#include "activities/Activity.h"
#include "activities/ScreenTransitionRefresh.h"

// Download more languages: joins Wi-Fi, downloads the latest release's language
// bundle to the SD card (language_bundle::DOWNLOAD_PATH), checks its SHA-256
// and restarts, which unpacks it at boot. Opened after a network boot
// (NetworkBootTarget::DOWNLOAD_ASSETS) so Wi-Fi + TLS get a whole heap.
class LanguageBundleDownloadActivity final : public Activity {
 public:
  LanguageBundleDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LanguageBundleDownload", renderer, mappedInput) {}

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
