#include "PhotoRender.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <ImageToneProfile.h>
#include <Logging.h>

#include <algorithm>
#include <cmath>

#include "Epub/converters/PngToFramebufferConverter.h"

namespace photo {

namespace {

void runOverlay(GfxRenderer& renderer, const std::function<void(bool)>& overlay) {
  if (overlay) overlay(renderer.getRenderMode() != GfxRenderer::BW);
}

Result renderPng(GfxRenderer& renderer, const std::string& filePath, const bool cropMode,
                 const std::function<void(bool)>& overlay, const int reserveBottom) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight() - reserveBottom;
  ImageDimensions dims;
  if (!PngToFramebufferConverter::getDimensionsStatic(filePath, dims)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return Result::ImageError;
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
  // Each pass re-decodes the PNG (the dither is purely positional, so every pass quantizes
  // identically).
  const auto drawFrame = [&]() {
    if (!converter.decodeToFramebuffer(filePath, renderer, config)) return false;
    renderer.preserveImagePolarity(x, y, drawWidth, drawHeight);
    runOverlay(renderer, overlay);
    return true;
  };

  renderer.clearScreen();
  bool success = drawFrame();
  if (success) {
    // Prefer the Direct waveform where the panel implements it: it folds the B/W base into
    // the grayscale pass instead of pushing a separate base refresh first.
    const bool absolute = renderer.supportsAbsoluteGrayscale();
    const bool direct = absolute && renderer.supportsDirectGrayscale();
    if (absolute) {
      success = direct ? renderer.displayDirectGrayscaleBase() : renderer.displayAbsoluteGrayscaleBase();
    } else {
      renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
    }
    for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      if (!success) break;
      renderer.clearScreen(absolute ? 0xFF : 0x00);
      renderer.setRenderMode(mode);
      success = drawFrame();
      if (!success) break;
      if (mode == GfxRenderer::GRAYSCALE_LSB)
        renderer.copyGrayscaleLsbBuffers();
      else
        renderer.copyGrayscaleMsbBuffers();
    }
    if (success) renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
  if (!success) {
    renderer.setRenderMode(GfxRenderer::BW);
    return Result::ImageError;
  }
  return Result::Ok;
}

Result renderBmp(GfxRenderer& renderer, const std::string& filePath, const bool cropMode,
                 const std::function<void(bool)>& overlay, const int reserveBottom) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight() - reserveBottom;
  HalFile file;
  if (!Storage.openFileForRead("SLDSHW", filePath, file)) return Result::ImageError;

  Bitmap bitmap(file, true, imageToneProfile(renderer.supportsAbsoluteGrayscale()));
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    return Result::ImageError;
  }

  // Keep error diffusion on the screen-sized grid (see the same note in
  // SleepActivity::renderBitmapSleepScreen()): resampling an already dithered source makes
  // the source pattern alias into regular seams/haze.
  if (!cropMode && (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight)) {
    const float fitScale = std::min(static_cast<float>(pageWidth) / bitmap.getWidth(),
                                    static_cast<float>(pageHeight) / bitmap.getHeight());
    bitmap.setDitheredOutputSize(static_cast<int>(std::floor((bitmap.getWidth() - 1) * fitScale)) + 1,
                                 static_cast<int>(std::floor((bitmap.getHeight() - 1) * fitScale)) + 1);
  }

  int x, y;
  float cropX = 0, cropY = 0;
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

  // Same sequence as SleepActivity::renderBitmapSleepScreen() (custom sleep cover, no
  // filter): HALF_REFRESH base pass, gray planes cleared to 0x00 on non-absolute (X3) panels,
  // no BW redraw.
  const auto drawFrame = [&]() {
    const bool ok = renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    if (ok) runOverlay(renderer, overlay);
    return ok;
  };

  renderer.clearScreen();
  bool success = drawFrame();
  if (success && bitmap.hasGreyscale()) {
    const bool absolute = renderer.supportsAbsoluteGrayscale();
    const bool direct = absolute && renderer.supportsDirectGrayscale();
    if (absolute) {
      success = direct ? renderer.displayDirectGrayscaleBase() : renderer.displayAbsoluteGrayscaleBase();
    } else {
      renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
    }
    for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      if (!success) break;
      success = bitmap.rewindToData() == BmpReaderError::Ok;
      if (!success) break;
      renderer.clearScreen(absolute ? 0xFF : 0x00);
      renderer.setRenderMode(mode);
      success = drawFrame();
      if (!success) break;
      if (mode == GfxRenderer::GRAYSCALE_LSB)
        renderer.copyGrayscaleLsbBuffers();
      else
        renderer.copyGrayscaleMsbBuffers();
    }
    if (success) renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  } else if (success) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
  file.close();
  if (!success) {
    renderer.setRenderMode(GfxRenderer::BW);
    LOG_ERR("SLDSHW", "Failed to render complete BMP image");
    return Result::RenderFailed;
  }
  return Result::Ok;
}

}  // namespace

Result render(GfxRenderer& renderer, const std::string& path, const bool cropMode,
              const std::function<void(bool gray)>& overlay, int reserveBottom) {
  reserveBottom = std::max(0, std::min(reserveBottom, renderer.getScreenHeight() / 2));
  // The decode + multi-pass grayscale SPI writes must not run at the reduced idle CPU
  // frequency (on the X3 the APB clock tracks the CPU below 80 MHz, which throws off the
  // panel's SPI timing; see the slideshow's history).
  HalPowerManager::Lock powerLock;
  if (FsHelpers::hasPngExtension(path)) return renderPng(renderer, path, cropMode, overlay, reserveBottom);
  return renderBmp(renderer, path, cropMode, overlay, reserveBottom);
}

}  // namespace photo
