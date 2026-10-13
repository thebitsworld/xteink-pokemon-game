#pragma once

// An app's "How to play" guide: a short Markdown file in the app's folder
// (guide.md, or guide.vi.md when the reader is in Vietnamese), shown page by
// page. Not the book reader: a guide is no book - it must not join the recent
// books, count as reading time or credit the Pokemon game - and this screen is
// small enough to open over a running Lua app, which carries on when it
// closes. A first "# Title" line replaces the app's name in the header. Understands the Markdown a guide needs: "#"/"##"/"###" headings,
// paragraphs, "- " bullets and "1. " numbered items; inline marks (**, *, `)
// are dropped.

#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UITheme.h"
#include "fontIds.h"

class GuideViewerActivity : public Activity {
 public:
  GuideViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path, std::string title)
      : Activity("Guide", renderer, mappedInput), path_(std::move(path)), title_(std::move(title)) {}

  // The guide of the app in `appDir` for the reader's language, or "" if it
  // has none.
  static std::string guidePathFor(const std::string& appDir) {
    if (I18N.getLanguage() == Language::VI) {
      const std::string vi = appDir + "/guide.vi.md";
      if (Storage.exists(vi.c_str())) return vi;
    }
    const std::string en = appDir + "/guide.md";
    return Storage.exists(en.c_str()) ? en : std::string();
  }

  void onEnter() override {
    Activity::onEnter();
    loadBlocks();
    // Opened by holding Select: that press's release is not a page turn.
    swallowConfirm_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
    page_ = 0;
    laidOutWidth_ = 0;
    requestUpdate();
  }

  void onExit() override {
    blocks_.clear();
    blocks_.shrink_to_fit();
    lines_.clear();
    lines_.shrink_to_fit();
    Activity::onExit();
  }

  void loop() override {
    using Button = MappedInputManager::Button;
    if (swallowConfirm_) {
      if (mappedInput.isPressed(Button::Confirm)) return;
      swallowConfirm_ = false;
      if (mappedInput.wasReleased(Button::Confirm)) return;
    }
    if (mappedInput.wasReleased(Button::Back)) {
      finish();
      return;
    }
    int dir = 0;
    if (mappedInput.wasReleased(Button::Right) || mappedInput.wasReleased(Button::Down) ||
        mappedInput.wasReleased(Button::PageForward) || mappedInput.wasReleased(Button::Confirm)) {
      dir = 1;
    } else if (mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Up) ||
               mappedInput.wasReleased(Button::PageBack)) {
      dir = -1;
    }
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up) dir = 1;
    if (swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down) dir = -1;
    int tx = 0;
    int ty = 0;
    if (dir == 0 && mappedInput.wasScreenTapped(tx, ty)) {
      // The header's back arrow area leaves; otherwise the right half turns
      // forward and the left half back.
      if (ty < headerBottom() && tx < renderer.getScreenWidth() / 4) {
        finish();
        return;
      }
      dir = tx >= renderer.getScreenWidth() / 2 ? 1 : -1;
    }
    if (dir == 0) return;
    const int next = std::max(0, std::min(pageCount_ - 1, page_ + dir));
    if (next != page_) {
      page_ = next;
      requestUpdate();
    }
  }

  void render(RenderLock&&) override {
    const int width = renderer.getScreenWidth();
    if (width != laidOutWidth_) layOut(width);
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    char pageText[24];
    snprintf(pageText, sizeof(pageText), "%d / %d", page_ + 1, std::max(1, pageCount_));
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, title_.c_str(), pageText);
    if (lines_.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, "No guide", true);
    }
    for (const Line& line : lines_) {
      if (line.page != page_) continue;
      renderer.drawText(line.font, line.x, line.y, line.text.c_str(), true, line.bold ? EpdFontFamily::BOLD
                                                                                      : EpdFontFamily::REGULAR);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "<", ">");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
  }

 private:
  enum class Kind : uint8_t { Title, Heading, Paragraph, Bullet, Numbered };
  struct Block {
    Kind kind;
    std::string text;
    std::string marker;  // "1." for a numbered item
  };
  struct Line {
    std::string text;
    int font;
    bool bold;
    int x;
    int y;
    int page;
  };

  static constexpr size_t MAX_GUIDE_BYTES = 32 * 1024;

  std::string path_;
  std::string title_;
  std::vector<Block> blocks_;
  std::vector<Line> lines_;
  int page_ = 0;
  int pageCount_ = 1;
  int laidOutWidth_ = 0;
  bool swallowConfirm_ = false;

  int headerBottom() const {
    const auto& metrics = UITheme::getInstance().getMetrics();
    return metrics.topPadding + metrics.headerHeight;
  }

  // Drops inline Markdown marks: bold/italic asterisks and underscores pairs,
  // backticks.
  static std::string plain(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
      if (c == '*' || c == '`') continue;
      out += c;
    }
    return out;
  }

  static std::string trimmed(const std::string& s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
  }

  void loadBlocks() {
    blocks_.clear();
    HalFile file;
    if (path_.empty() || !Storage.openFileForRead("GUIDE", path_, file)) return;
    std::string text;
    text.resize(std::min<size_t>(file.fileSize(), MAX_GUIDE_BYTES));
    const int got = file.read(reinterpret_cast<uint8_t*>(&text[0]), text.size());
    file.close();
    text.resize(got > 0 ? static_cast<size_t>(got) : 0);

    std::string paragraph;
    const auto flush = [&]() {
      if (!paragraph.empty()) blocks_.push_back({Kind::Paragraph, plain(paragraph), ""});
      paragraph.clear();
    };
    size_t start = 0;
    while (start <= text.size()) {
      size_t end = text.find('\n', start);
      if (end == std::string::npos) end = text.size();
      const std::string raw = text.substr(start, end - start);
      const std::string line = trimmed(raw);
      const bool indented = !raw.empty() && (raw[0] == ' ' || raw[0] == '\t');
      start = end + 1;
      if (line.empty()) {
        flush();
        continue;
      }
      if (line[0] == '#') {
        flush();
        size_t level = 0;
        while (level < line.size() && line[level] == '#') ++level;
        const std::string heading = plain(trimmed(line.substr(level)));
        if (level == 1 && blocks_.empty() && paragraph.empty()) {
          // The guide's title goes in the header.
          title_ = heading;
          continue;
        }
        blocks_.push_back({level == 1 ? Kind::Title : Kind::Heading, heading, ""});
      } else if ((line[0] == '-' || line[0] == '*') && line.size() > 1 && line[1] == ' ') {
        flush();
        blocks_.push_back({Kind::Bullet, plain(trimmed(line.substr(2))), ""});
      } else if (isdigit(static_cast<unsigned char>(line[0])) && line.find(". ") != std::string::npos &&
                 line.find(". ") <= 3) {
        flush();
        const size_t dot = line.find(". ");
        blocks_.push_back({Kind::Numbered, plain(trimmed(line.substr(dot + 2))), line.substr(0, dot + 1)});
      } else {
        // An indented line right after a list item continues it; anything
        // else belongs to a paragraph.
        if (indented && paragraph.empty() && !blocks_.empty() &&
            (blocks_.back().kind == Kind::Bullet || blocks_.back().kind == Kind::Numbered)) {
          blocks_.back().text += " " + plain(line);
        } else {
          if (!paragraph.empty()) paragraph += ' ';
          paragraph += line;
        }
      }
    }
    flush();
  }

  // Wraps every block to `width` and splits the lines into pages.
  void layOut(const int width) {
    lines_.clear();
    laidOutWidth_ = width;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int left = 20;
    const int right = width - 20;
    const int top = headerBottom() + 14;
    const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - 10;
    int y = top;
    int page = 0;
    for (const Block& block : blocks_) {
      const bool heading = block.kind == Kind::Title || block.kind == Kind::Heading;
      const int font = heading ? UI_12_FONT_ID : UI_10_FONT_ID;
      const int lineH = renderer.getLineHeight(font);
      const int gapBefore = heading ? 12 : 6;
      const bool item = block.kind == Kind::Bullet || block.kind == Kind::Numbered;
      const std::string marker = block.kind == Kind::Bullet ? "\xE2\x80\xA2" : block.marker;
      const int markerW = item ? renderer.getTextWidth(font, marker.c_str()) : 0;
      const int textX = left + (item ? 8 + markerW + 8 : 0);
      // Words onto lines no wider than the column.
      std::vector<std::string> wrapped;
      std::string current;
      size_t i = 0;
      const std::string& text = block.text;
      while (i < text.size()) {
        size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        const std::string word = text.substr(i, j - i);
        i = j + 1;
        if (word.empty()) continue;
        const std::string tryLine = current.empty() ? word : current + " " + word;
        if (!current.empty() &&
            renderer.getTextWidth(font, tryLine.c_str(), heading ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR) >
                right - textX) {
          wrapped.push_back(current);
          current = word;
        } else {
          current = tryLine;
        }
      }
      if (!current.empty()) wrapped.push_back(current);
      if (wrapped.empty()) continue;

      if (y != top) y += gapBefore;
      // A heading never ends a page alone: it moves on with its next line.
      const int need = heading ? lineH * 2 : lineH;
      for (size_t k = 0; k < wrapped.size(); ++k) {
        if (y + (k == 0 ? need : lineH) > bottom) {
          ++page;
          y = top;
        }
        if (k == 0 && item) lines_.push_back({marker, font, false, left + 8, y, page});
        lines_.push_back({wrapped[k], font, heading, textX, y, page});
        y += lineH;
      }
    }
    pageCount_ = page + 1;
    if (page_ >= pageCount_) page_ = pageCount_ - 1;
  }
};
