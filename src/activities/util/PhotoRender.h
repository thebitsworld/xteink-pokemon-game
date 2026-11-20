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

// overlay, when given, runs after the image in every pass: gray == false on the B/W base (draw
// the overlay), true on a gray plane (clear the overlay's area so no gray shows through).
Result render(GfxRenderer& renderer, const std::string& path, bool cropMode,
              const std::function<void(bool gray)>& overlay = nullptr);

}  // namespace photo
