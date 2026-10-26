#include "LanguagePackDownloadActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "LanguagePackStore.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {

constexpr int DOWNLOAD_MAX_ATTEMPTS = 3;
constexpr uint32_t DOWNLOAD_RETRY_DELAY_MS = 500;

bool validatePack(const std::string& path) { return language_pack_store::fileIsValidPack(path.c_str()); }

}  // namespace

void LanguagePackDownloadActivity::onEnter() {
  Activity::onEnter();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void LanguagePackDownloadActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
  }
  // The boot installs the downloaded pack; it also gives back the heap Wi-Fi split.
  if (restartRequested_) silentRestart();
}

void LanguagePackDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }
  download();
}

bool LanguagePackDownloadActivity::pollCancelInput() {
  if (cancelRequested_) return true;
  mappedInput.update();
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      (mappedInput.hasTouchHardware() && TouchHeaderBackButton::wasTapped(mappedInput, renderer))) {
    cancelRequested_ = true;
  }
  return cancelRequested_;
}

void LanguagePackDownloadActivity::download() {
  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloaded_ = 0;
    total_ = 0;
    cancelRequested_ = false;
  }
  requestUpdateAndWait();

  HttpDownloader::DownloadOptions options;
  options.stageAsPart = true;  // a failed download never replaces a good pack on the card
  options.validate = &validatePack;
  options.checkFreeSpace = true;
  options.shouldCancel = [this]() { return pollCancelInput(); };
  HttpDownloader::DownloadError result = HttpDownloader::HTTP_ERROR;
  for (int attempt = 1; attempt <= DOWNLOAD_MAX_ATTEMPTS && result != HttpDownloader::OK; ++attempt) {
    if (attempt > 1) delay(DOWNLOAD_RETRY_DELAY_MS);
    result = HttpDownloader::downloadToFile(
        language_pack_store::PACK_URL, language_pack_store::PACK_PATH,
        [this](const size_t downloaded, const size_t total) {
          downloaded_ = downloaded;
          total_ = total;
          requestUpdate(true);
        },
        &cancelRequested_, "", "", options);
    if (result == HttpDownloader::ABORTED) break;
    if (result != HttpDownloader::OK) LOG_ERR("LANGPACK", "Download attempt %d failed (%d)", attempt, result);
  }

  if (result == HttpDownloader::ABORTED) {
    mappedInput.suppressNextBackRelease();
    finish();
    return;
  }
  {
    RenderLock lock(*this);
    state_ = result == HttpDownloader::OK ? COMPLETE : ERROR;
  }
  requestUpdate();
}

void LanguagePackDownloadActivity::loop() {
  if (state_ == COMPLETE) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      restartRequested_ = true;
      finish();
    }
  } else if (state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        (mappedInput.hasTouchHardware() && TouchHeaderBackButton::wasTapped(mappedInput, renderer))) {
      finishAfterBackPress();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      download();
    }
  }
}

void LanguagePackDownloadActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  GUI.drawHeader(renderer, header, tr(STR_LANGUAGE_PACK));

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int centerY = (pageHeight - lineHeight) / 2;
  const Rect textArea{metrics.contentSidePadding, 0, pageWidth - metrics.contentSidePadding * 2, pageHeight};

  if (state_ == DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_LANGUAGE_PACK_DOWNLOADING));
    const int percent = total_ > 0 ? static_cast<int>(downloaded_ * 100 / total_) : 0;
    GUI.drawProgressBar(renderer,
                        Rect{metrics.contentSidePadding, centerY + metrics.verticalSpacing,
                             pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
                        percent, 100);
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == COMPLETE) {
    UITheme::drawCenteredWrappedText(renderer, textArea, UI_10_FONT_ID, centerY - lineHeight,
                                     tr(STR_LANGUAGE_PACK_READY), 3, true, EpdFontFamily::BOLD, 2);
    const auto labels = mappedInput.mapLabels("", tr(STR_OK), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    UITheme::drawCenteredWrappedText(renderer, textArea, UI_10_FONT_ID, centerY - lineHeight,
                                     tr(STR_LANGUAGE_PACK_FAILED), 3, true, EpdFontFamily::BOLD, 2);
    const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer(screenTransitionRefresh_.modeFor(static_cast<uint8_t>(state_)));
}
