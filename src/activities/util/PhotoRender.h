#pragma once

#include <functional>
#include <string>

class GfxRenderer;

// Full-screen photo drawing shared by the Photo Slideshow and Desk mode's photo frame: a BMP
// or PNG fitted (or cropped) to the screen, with the same grayscale sequence as
// SleepActivity::renderBitmapSleepScreen() (B/W base pass, then the LSB/MSB gray planes), and
// pushed to the panel. Moved out of SlideshowActivity::renderCurrentImage() unchanged.
namespace photo {

enum class Result {
  Ok,
  ImageError,    // unreadable file or header: nothing drawn
  RenderFailed,  // decoding failed part-way (BMP)
};

// overlay, when given, runs after the image in every pass: gray == false on the B/W base, true
// on a gray plane. reserveBottom keeps that many rows at the bottom free of the image (it is
// fitted or cropped to the area above), e.g. for an overlay that must not cover the photo.
Result render(GfxRenderer& renderer, const std::string& path, bool cropMode,
              const std::function<void(bool gray)>& overlay = nullptr, int reserveBottom = 0);

}  // namespace photo
