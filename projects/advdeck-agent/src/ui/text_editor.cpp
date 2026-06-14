// src/ui/text_editor.cpp
//
// See text_editor.h for the contract. The implementation is split
// into two layers:
//   1. Pure buffer logic (insert_char, backspace, row/col math). These
//      are compiled on host and firmware alike, and they are what the
//      tests exercise.
//   2. Display rendering. Wrapped in #ifdef ADVDECK_FIRMWARE so the
//      host test binary links cleanly without M5GFX.

#include "ui/text_editor.h"

#include <algorithm>
#include <cstdint>
#include <string>

#ifdef ADVDECK_FIRMWARE
#include "platform/display.h"
#include "ui/status_bar.h"
#endif

namespace advdeck {
namespace ui {

namespace {

#ifdef ADVDECK_FIRMWARE
// Treat the buffer as a sequence of '\n'-separated lines. Used by
// the firmware redraw path. Host tests don't need these helpers.
int line_start(const std::string& s, int row) {
  if (row <= 0) return 0;
  int seen = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\n') {
      ++seen;
      if (seen == row) return static_cast<int>(i) + 1;
    }
  }
  return static_cast<int>(s.size());
}
int line_end(const std::string& s, int row) {
  // Position of the '\n' that terminates `row`, or s.size() if no
  // trailing newline.
  int start = line_start(s, row);
  for (std::size_t i = start; i < s.size(); ++i) {
    if (s[i] == '\n') return static_cast<int>(i);
  }
  return static_cast<int>(s.size());
}

int line_count(const std::string& s) {
  if (s.empty()) return 1;
  int n = 1;
  for (char c : s) {
    if (c == '\n') ++n;
  }
  return n;
}
#endif

}  // namespace


TextEditor::TextEditor(std::string initial, std::string editing_label)
    : buffer_(std::move(initial)), label_(std::move(editing_label)) {
  cursor_ = static_cast<int>(buffer_.size());
}

void TextEditor::reset(std::string initial) {
  buffer_ = std::move(initial);
  cursor_ = static_cast<int>(buffer_.size());
  scroll_y_ = 0;
  confirmed_ = false;
  cancelled_ = false;
}

void TextEditor::offset_to_rowcol(int offset, int* row, int* col) const {
  if (offset < 0) offset = 0;
  if (offset > static_cast<int>(buffer_.size())) {
    offset = static_cast<int>(buffer_.size());
  }
  int r = 0;
  int c = 0;
  for (int i = 0; i < offset; ++i) {
    if (buffer_[i] == '\n') {
      ++r;
      c = 0;
    } else {
      ++c;
    }
  }
  *row = r;
  *col = c;
}

int TextEditor::rowcol_to_offset(int row, int col) const {
  if (row < 0) row = 0;
  if (col < 0) col = 0;
  int r = 0;
  int i = 0;
  for (; i < static_cast<int>(buffer_.size()) && r < row; ++i) {
    if (buffer_[i] == '\n') ++r;
  }
  if (r < row) {
    // Past the end of the buffer: clamp to the end.
    return static_cast<int>(buffer_.size());
  }
  // Now i is the start of `row`. Walk `col` characters (or until a
  // newline / end of buffer).
  for (int k = 0; k < col && i < static_cast<int>(buffer_.size()); ++k, ++i) {
    if (buffer_[i] == '\n') return i;  // can't go past the line end
  }
  return i;
}

bool TextEditor::insert_char(char c) {
  if (cursor_ < 0) cursor_ = 0;
  if (cursor_ > static_cast<int>(buffer_.size())) {
    cursor_ = static_cast<int>(buffer_.size());
  }
  buffer_.insert(buffer_.begin() + cursor_, c);
  ++cursor_;
  return true;
}

bool TextEditor::backspace() {
  if (cursor_ <= 0) return false;
  // If the cursor sits right after a '\n', the deletion joins this
  // line with the previous one: the '\n' is removed and the cursor
  // sits at the join point.
  if (cursor_ > 0 && buffer_[cursor_ - 1] == '\n') {
    buffer_.erase(buffer_.begin() + cursor_ - 1);
    --cursor_;
    return true;
  }
  buffer_.erase(buffer_.begin() + cursor_ - 1);
  --cursor_;
  return true;
}

bool TextEditor::tick(const platform::KeyEvent& ev) {
  if (!ev.any) return false;
  if (cancelled_ || confirmed_) return false;
  if (ev.escape) {
    cancelled_ = true;
    return true;
  }
  if (ev.enter) {
    // Two interpretations of Enter, decided by where the caret sits.
    //   - If the buffer is empty, the user has not started typing;
    //     treat Enter as a no-op (matches the existing contract
    //     that an empty buffer cannot be saved).
    //   - If the cursor sits at the start of an empty trailing line
    //     (i.e. the buffer ends with a '\n' the user has just
    //     typed), Enter confirms. The trailing '\n' is preserved on
    //     the way out so the router can read idea.md as-is.
    //   - Otherwise Enter inserts a '\n' at the cursor.
    if (buffer_.empty()) {
      return false;
    }
    if (cursor_ > 0 && cursor_ == static_cast<int>(buffer_.size()) &&
        buffer_.back() == '\n') {
      confirmed_ = true;
      return true;
    }
    return insert_char('\n');
  }
  if (ev.backspace) {
    return backspace();
  }
  // Printable ASCII. Keyboard's KeyEvent normalises to the 0x20..0x7E
  // range; a few special Cardputer chars (e.g. ';' / '.' / '/') come
  // through as printable bytes.
  if (ev.c >= 0x20 && ev.c < 0x7F) {
    return insert_char(ev.c);
  }
  return false;
}

#ifdef ADVDECK_FIRMWARE

namespace {
platform::Display& disp() {
  static platform::Display d;
  return d;
}

constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kCaret = 0x07E0;
}  // namespace

void TextEditor::redraw() {
  disp().clear();
  StatusBar bar(disp());
  bar.draw(true);
  disp().text(4, kContentTop, kFg, label_);

  int row = 0, col = 0;
  offset_to_rowcol(cursor_, &row, &col);
  // Auto-scroll so the caret row is visible.
  if (row < scroll_y_) scroll_y_ = row;
  if (row >= scroll_y_ + kVisibleRows) {
    scroll_y_ = row - kVisibleRows + 1;
  }
  if (scroll_y_ < 0) scroll_y_ = 0;

  const int total_rows = line_count(buffer_);
  for (int i = 0; i < kVisibleRows; ++i) {
    const int r = scroll_y_ + i;
    if (r >= total_rows) break;
    const int s = line_start(buffer_, r);
    const int e = line_end(buffer_, r);
    const std::string line = buffer_.substr(s, e - s);
    const int y = kContentTop + 12 + i * kRowH;
    // Phase 1: truncate at the right edge of the panel.
    const int max_chars = (platform::Display::kWidth - 4) / 6;
    const std::string shown =
        line.size() > static_cast<std::size_t>(max_chars)
            ? line.substr(0, static_cast<std::size_t>(max_chars - 1)) + "~"
            : line;
    disp().text(4, y, kFg, shown);
    if (r == row) {
      // Render a small caret marker at the column.
      const int caret_x = 4 + col * 6;
      if (caret_x < platform::Display::kWidth - 2) {
        disp().text(caret_x, y, kCaret, "_");
      }
    }
  }
  disp().text(4, platform::Display::kHeight - kRowH, 0x7BEF,
              "[Enter] save  [Esc] cancel");
  disp().push();
}

#endif  // ADVDECK_FIRMWARE

EditorResult TextEditor::run() {
#ifdef ADVDECK_FIRMWARE
  redraw();
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (tick(ev)) {
      redraw();
    }
    if (done()) break;
  }
#endif
  EditorResult r;
  r.confirmed = confirmed_;
  r.cancelled = cancelled_;
  r.text = buffer_;
  return r;
}

}  // namespace ui
}  // namespace advdeck
