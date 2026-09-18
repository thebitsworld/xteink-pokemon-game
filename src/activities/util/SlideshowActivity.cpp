#include "SlideshowActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "Epub/converters/PngToFramebufferConverter.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "fontIds.h"

namespace {

bool isViewableImageFile(const std::string& filename) {
  return FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename);
}

bool isMacOSSidecarFile(const std::string& filename) { return filename.rfind("._", 0) == 0; }

void drawSlideshowMessage(GfxRenderer& renderer, const MappedInputManager& mappedInput, const char* message) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, message);
  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

// Minutes between auto-advances, converted to milliseconds for millis() comparisons.
unsigned long intervalMs() { return static_cast<unsigned long>(SETTINGS.slideshowIntervalMinutes) * 60UL * 1000UL; }

}  // namespace

SlideshowActivity::SlideshowActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Slideshow", renderer, mappedInput) {}

void SlideshowActivity::onEnter() {
  Activity::onEnter();
  if (screen == Screen::Menu) {
    renderer.clearScreen();
    refreshMenuPopup();
  } else {
    startPlayback();
  }
  requestUpdate();
}

void SlideshowActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SlideshowActivity::refreshMenuPopup() {
  const std::string folderLabel =
      std::string(tr(STR_SLIDESHOW_CHOOSE_FOLDER)) + ": " +
      (APP_STATE.slideshowFolderPath.empty() ? tr(STR_SLIDESHOW_FOLDER_NOT_SET) : APP_STATE.slideshowFolderPath);

  char intervalText[32];
  snprintf(intervalText, sizeof(intervalText), tr(STR_SLEEP_TIMER_VALUE_FORMAT), SETTINGS.slideshowIntervalMinutes);
  const std::string intervalLabel = std::string(tr(STR_SLIDESHOW_INTERVAL)) + ": " + intervalText;

  const bool isCrop = SETTINGS.slideshowScaleMode == CrossPointSettings::SLIDESHOW_CROP;
  const std::string displayModeLabel =
      std::string(tr(STR_SLIDESHOW_DISPLAY_MODE)) + ": " + (isCrop ? tr(STR_CROP) : tr(STR_FIT));

  const std::string startLabel = tr(STR_SLIDESHOW_START);

  std::vector<std::string> options{folderLabel, intervalLabel, displayModeLabel, startLabel};
  optionPopup.show(tr(STR_SLIDESHOW), options, 0, [this](const int index) { onMenuOptionSelected(index); });
  optionPopup.setDisabledOptions({false, false, false, APP_STATE.slideshowFolderPath.empty()});
  optionPopup.setCancelCallback([this] { activityManager.goHome(); });
}

void SlideshowActivity::onMenuOptionSelected(const int index) {
  switch (index) {
    case 0:
      openChooseFolder();
      return;
    case 1:
      openIntervalPicker();
      return;
    case 2:
      toggleScaleMode();
      return;
    case 3:
      if (!APP_STATE.slideshowFolderPath.empty()) {
        screen = Screen::Playing;
        startPlayback();
      }
      return;
    default:
      return;
  }
}

void SlideshowActivity::openChooseFolder() {
  const std::string initialPath = APP_STATE.slideshowFolderPath.empty() ? "/" : APP_STATE.slideshowFolderPath;
  auto picker = std::make_unique<FileBrowserActivity>(renderer, mappedInput, initialPath,
                                                      FileBrowserActivity::Mode::PickDirectory);
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    const auto* path = std::get_if<FilePathResult>(&result.data);
    if (!result.isCancelled && path) {
      APP_STATE.slideshowFolderPath = path->path == "/" ? std::string() : path->path;
      APP_STATE.saveToFile();
    }
    refreshMenuPopup();
    requestUpdate();
  });
}

void SlideshowActivity::openIntervalPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SlideshowMenuIntervalInterval", StrId::STR_SLIDESHOW_INTERVAL,
          SETTINGS.slideshowIntervalMinutes, CrossPointSettings::MIN_SLIDESHOW_INTERVAL_MINUTES,
          CrossPointSettings::MAX_SLIDESHOW_INTERVAL_MINUTES, 1, 5, StrId::STR_SLEEP_TIMER_VALUE_FORMAT,
          /*readerActivity=*/false, /*allowPowerAsConfirm=*/false, /*ignoreInitialConfirmRelease=*/true,
          /*showPercentValue=*/false, StrId::STR_NONE_OPT, /*overrideDisabledReaderTouchscreen=*/false,
          /*showTouchHeaderBackButton=*/true, /*valueFormatter=*/nullptr, /*tapStep=*/0,
          /*useReaderSlider=*/true),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.slideshowIntervalMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        refreshMenuPopup();
        requestUpdate();
      });
}

void SlideshowActivity::toggleScaleMode() {
  SETTINGS.slideshowScaleMode = SETTINGS.slideshowScaleMode == CrossPointSettings::SLIDESHOW_CROP
                                    ? CrossPointSettings::SLIDESHOW_FIT
                                    : CrossPointSettings::SLIDESHOW_CROP;
  SETTINGS.saveToFile();
  refreshMenuPopup();
  requestUpdate();
}

void SlideshowActivity::render(RenderLock&&) {
  if (screen != Screen::Menu) return;
  renderer.clearScreen();
  optionPopup.processRender(renderer, mappedInput);
}

void SlideshowActivity::loadImageList() {
  images.clear();
  currentIndex = -1;

  const std::string& dirPath = APP_STATE.slideshowFolderPath;
  if (dirPath.empty()) return;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.' && !isMacOSSidecarFile(name)) {
        std::string fname(name);
        if (isViewableImageFile(fname)) {
          images.push_back(fname);
        }
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(images);
  if (!images.empty()) currentIndex = 0;
}

void SlideshowActivity::startPlayback() {
  loadImageList();
  if (images.empty()) {
    screen = Screen::Empty;
    drawEmptyMessage();
    return;
  }
  screen = Screen::Playing;
  lastAdvanceMs = millis();
  renderCurrentImage();
}

void SlideshowActivity::drawEmptyMessage() { drawSlideshowMessage(renderer, mappedInput, tr(STR_SLIDESHOW_EMPTY_FOLDER)); }

void SlideshowActivity::renderCurrentImage() {
  if (currentIndex < 0 || currentIndex >= static_cast<int>(images.size())) return;

  // Ghost-cleanup pass before every single image (not just periodically -
  // slideshow already waits whole minutes between images, unlike the
  // reader's page-turn latency budget). A real full-panel refresh (ignores
  // any differential baseline) physically resettles the panel so this
  // image's own grayscale composite starts clean.
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  // The refresh above alone was NOT enough (confirmed by testing on real
  // X3 hardware) - the reader hit this exact same problem and its own fix
  // is the OEM grayscale pre-conditioning pass, run once right before the
  // gray planes are written (see EpubReaderActivity.cpp/XtcReaderActivity.cpp,
  // same call): "X3 grayscale overlays settle better if the OEM precondition
  // step runs before the gray planes are written." Without it the B/W base
  // pass alone leaves the panel under-conditioned for the LSB/MSB grayscale
  // planes that follow, which is what actually showed up as X3 images
  // looking washed out/less detailed than the identical image on X4 Pro.
  renderer.preconditionGrayscale();

  std::string dirPath = APP_STATE.slideshowFolderPath;
  if (dirPath.back() != '/') dirPath += "/";
  const std::string filePath = dirPath + images[currentIndex];

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const bool cropMode = SETTINGS.slideshowScaleMode == CrossPointSettings::SLIDESHOW_CROP;

  if (FsHelpers::hasPngExtension(filePath)) {
    ImageDimensions dims;
    if (!PngToFramebufferConverter::getDimensionsStatic(filePath, dims)) {
      drawSlideshowMessage(renderer, mappedInput, "Invalid PNG File");
      return;
    }

    float scale = 1.0f;
    const float scaleX = static_cast<float>(pageWidth) / static_cast<float>(dims.width);
    const float scaleY = static_cast<float>(pageHeight) / static_cast<float>(dims.height);
    if (cropMode) {
      scale = std::max(scaleX, scaleY);
    } else if (dims.width > pageWidth || dims.height > pageHeight) {
      scale = std::min(scaleX, scaleY);
    }

    const int drawWidth = std::max(1, static_cast<int>(static_cast<float>(dims.width) * scale));
    const int drawHeight = std::max(1, static_cast<int>(static_cast<float>(dims.height) * scale));
    const int x = (pageWidth - drawWidth) / 2;
    const int y = (pageHeight - drawHeight) / 2;

    RenderConfig config;
    config.x = x;
    config.y = y;
    config.maxWidth = drawWidth;
    config.maxHeight = drawHeight;
    config.useGrayscale = true;
    config.useDithering = true;
    config.performanceMode = false;
    config.useExactDimensions = true;

    PngToFramebufferConverter converter;
    // Each pass re-decodes the PNG under a different render mode (BW, then the
    // LSB/MSB grayscale planes) rather than resuming a stream - the dither in
    // DirectPixelWriter is purely positional, so re-decoding reproduces the
    // same quantized values per plane, matching the pattern already used for
    // BMP below and for the sleep cover (SleepActivity::renderBitmapSleepScreen).
    // No button-hint row while an image is actually showing - on non-touch
    // devices (X3) it ate into the image area for no real benefit (Back still
    // works via the physical button either way, loop() handles it directly).
    const auto drawFrame = [&]() {
      renderer.clearScreen();
      if (!converter.decodeToFramebuffer(filePath, renderer, config)) return false;
      renderer.preserveImagePolarity(x, y, drawWidth, drawHeight);
      return true;
    };

    bool success = drawFrame();
    if (success) {
      const bool absolute = renderer.supportsAbsoluteGrayscale();
      if (absolute) {
        success = renderer.displayAbsoluteGrayscaleBase();
      } else {
        renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      }
      for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
        if (!success) break;
        renderer.setRenderMode(mode);
        success = drawFrame();
        if (!success) break;
        if (mode == GfxRenderer::GRAYSCALE_LSB)
          renderer.copyGrayscaleLsbBuffers();
        else
          renderer.copyGrayscaleMsbBuffers();
      }
      // Stop right here, matching SleepActivity::renderBitmapSleepScreen()
      // (confirmed to render correctly on X3) - do NOT redraw a plain B/W
      // frame and call cleanupGrayscaleWithFrameBuffer() afterward like this
      // used to (copied from BmpViewerActivity's own, never-quite-right
      // pattern). That extra step visibly washed out the real grayscale
      // image displayGrayBuffer() had just shown correctly.
      if (success) renderer.displayGrayBuffer();
      renderer.setRenderMode(GfxRenderer::BW);
    }
    if (!success) {
      drawSlideshowMessage(renderer, mappedInput, "Invalid PNG File");
    }
    return;
  }

  HalFile file;
  if (!Storage.openFileForRead("SLDSHW", filePath, file)) {
    drawSlideshowMessage(renderer, mappedInput, "Could not open file");
    return;
  }

  Bitmap bitmap(file, true, renderer.supportsAbsoluteGrayscale());
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    drawSlideshowMessage(renderer, mappedInput, "Invalid BMP File");
    file.close();
    return;
  }

  int x, y;
  float cropX = 0, cropY = 0;
  if (!cropMode && (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight)) {
    // Re-dither AT the final on-screen size instead of dithering at the
    // source resolution and letting drawBitmap() point-sample it down
    // afterward - matches SleepActivity::renderBitmapSleepScreen()'s own
    // FIT-mode handling (confirmed correct on X3). Scaling an
    // already-dithered bitmap breaks the dither pattern's regularity and
    // is what was visibly losing contrast/detail here. Only applies to
    // Fit (no crop) - setDitheredOutputSize() resizes the whole image
    // uniformly, it has no concept of cropping a region first.
    const float scale = std::min(static_cast<float>(pageWidth) / static_cast<float>(bitmap.getWidth()),
                                 static_cast<float>(pageHeight) / static_cast<float>(bitmap.getHeight()));
    const int targetWidth = static_cast<int>(std::floor((bitmap.getWidth() - 1) * scale)) + 1;
    const int targetHeight = static_cast<int>(std::floor((bitmap.getHeight() - 1) * scale)) + 1;
    bitmap.setDitheredOutputSize(targetWidth, targetHeight);
  }
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (cropMode) {
        cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (cropMode) {
        cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
    }
  } else {
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  // No button-hint row while an image is actually showing - see the PNG
  // branch's identical comment above.
  const auto drawFrame = [&]() {
    return renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
  };

  renderer.clearScreen();
  bool success = drawFrame();
  if (success && bitmap.hasGreyscale()) {
    const bool absolute = renderer.supportsAbsoluteGrayscale();
    if (absolute) {
      success = renderer.displayAbsoluteGrayscaleBase();
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
    }
    for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      if (!success) break;
      success = bitmap.rewindToData() == BmpReaderError::Ok;
      if (!success) break;
      renderer.clearScreen();
      renderer.setRenderMode(mode);
      success = drawFrame();
      if (!success) break;
      if (mode == GfxRenderer::GRAYSCALE_LSB)
        renderer.copyGrayscaleLsbBuffers();
      else
        renderer.copyGrayscaleMsbBuffers();
    }
    // Stop right here, matching SleepActivity::renderBitmapSleepScreen() -
    // see the PNG branch's identical comment above for why the extra
    // redraw+cleanupGrayscaleWithFrameBuffer() step this used to have is
    // gone.
    if (success) renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  } else if (success) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
  if (!success) {
    LOG_ERR("SLDSHW", "Failed to render complete BMP image");
    drawSlideshowMessage(renderer, mappedInput, tr(STR_FAILED_LOWER));
  }

  file.close();
}

void SlideshowActivity::advanceImage(const int delta) {
  if (images.empty()) return;
  const int count = static_cast<int>(images.size());
  currentIndex = ((currentIndex + delta) % count + count) % count;
  lastAdvanceMs = millis();
  renderCurrentImage();
}

void SlideshowActivity::loop() {
  Activity::loop();

  if (screen == Screen::Menu) {
    optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
    return;
  }

  // Screen::Playing and Screen::Empty: Back exits straight to Home.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }

  if (screen != Screen::Playing) return;

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left ||
      mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    advanceImage(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right || mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    advanceImage(-1);
    return;
  }

  if (millis() - lastAdvanceMs >= intervalMs()) {
    advanceImage(1);
  }
}
