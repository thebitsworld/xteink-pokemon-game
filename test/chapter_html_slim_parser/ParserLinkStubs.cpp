#include <BidiUtils.h>
#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <Epub/parsers/PreviewBlockLocator.h>
#include <Epub/tables/CompactTableLayout.h>
#include <GfxRenderer.h>

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string&, bool) { return {}; }

namespace BidiUtils {
bool startsWithRtl(const char*, int) { return false; }
int detectParagraphLevel(const char*, int fallbackLevel, int) { return fallbackLevel; }
bool computeVisualWordOrder(const std::vector<std::string>& words, bool, std::vector<uint16_t>& order) {
  order.resize(words.size());
  for (size_t index = 0; index < words.size(); ++index) order[index] = static_cast<uint16_t>(index);
  return true;
}
}  // namespace BidiUtils

TextBlock::TextBlock(const std::vector<std::string>&, const std::vector<int16_t>&,
                     const std::vector<EpdFontFamily::Style>&, const std::vector<uint8_t>&,
                     const std::vector<uint16_t>&, const std::vector<uint16_t>&, const std::vector<uint8_t>&,
                     const std::vector<bool>&, const BlockStyle& blockStyle, std::vector<std::string> rubyTexts)
    : blockStyle(blockStyle), rubyTexts(std::move(rubyTexts)) {}
bool TextBlock::hasRuby() const { return false; }

bool ImageDecoderFactory::isFormatSupported(const std::string& path) { return path.ends_with(".jpg"); }
// v1.6.0 added an <img> fallback path in ChapterHtmlSlimParser.cpp that calls getDecoder() when
// Epub::extractItemToFile() succeeds - stubs/Epub.h's extractItemToFile() always returns false, so
// this is never actually reached at runtime, but the symbol still needs a definition to link. A real
// decoder here would pull in the PNGdec/JPEGDEC libraries this suite otherwise has no need for; the
// caller already tolerates a null decoder (see the `decoder &&` guard before `decoder->getDimensions()`).
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }

ImageBlock::ImageBlock(std::string imagePath, std::string sourcePath, const int16_t width, const int16_t height)
    : imagePath(std::move(imagePath)), sourcePath(std::move(sourcePath)), width(width), height(height) {}

void PageImage::render(GfxRenderer&, int, int, int, bool) {}
void PageImage::renderPlaceholder(GfxRenderer&, int, int, bool) const {}
bool PageImage::serialize(FsFile&) { return false; }

PreviewBlockLocator::PreviewBlockLocator(const char*, IsBlockTagFn) {}
PreviewBlockLocator::~PreviewBlockLocator() = default;

CompactTableLayout::CompactTableLayout(GfxRenderer& renderer, int, uint16_t, uint16_t, uint16_t, uint8_t,
                                       BlockStyle tableStyle)
    : renderer_(renderer), tableStyle_(tableStyle) {}
bool CompactTableLayout::beginRow() { return true; }
bool CompactTableLayout::beginCell(bool, uint8_t, uint32_t, const BlockStyle&) { return true; }
bool CompactTableLayout::appendWord(std::string_view, EpdFontFamily::Style, bool, bool, uint8_t) { return true; }
bool CompactTableLayout::endCell(const std::vector<std::pair<int, FootnoteEntry>>&) { return true; }
CompactTableLayout::RowResult CompactTableLayout::finishRow(TableFragmentRow&, std::vector<std::shared_ptr<TextBlock>>&,
                                                            std::vector<FootnoteEntry>&, uint32_t&) {
  return RowResult::Ok;
}

void PageLine::render(GfxRenderer&, int, int, int, bool) {}
bool PageLine::serialize(FsFile&) { return false; }
void PageHorizontalRule::render(GfxRenderer&, int, int, int, bool) {}
bool PageHorizontalRule::serialize(FsFile&) { return false; }
void PageTableFragment::render(GfxRenderer&, int, int, int, bool) {}
bool PageTableFragment::serialize(FsFile&) { return false; }
