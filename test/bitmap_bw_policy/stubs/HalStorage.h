#pragma once

// Minimal stand-in so BitmapHelpers.cpp's include of Bitmap.h resolves in the
// native test build. Only the type needs to exist here - BitmapBwPolicyTest
// exercises quantize1bit() directly and never constructs a Bitmap/HalFile.
class HalFile {};
using FsFile = HalFile;
