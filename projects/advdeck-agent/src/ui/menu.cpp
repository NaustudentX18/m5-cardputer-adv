// src/ui/menu.cpp
//
// 240x135 has room for ~12 rows of 8-px text. We reserve the top
// status bar (12 px) and start items at y=16, giving 14 visible rows
// plus a 1-px gap. Long labels are truncated at the right edge.

#include "ui/menu.h"

#include <algorithm>
#include <cstdint>

#include "platform/keyboard.h"

namespace advdeck {
namespace ui {

namespace {
constexpr int kTop = 16;            // below the status bar
constexpr int kRowH = 10;            // 8-px font + 2 px gap
constexpr int kLeft = 4;
constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kSel = 0x07E0;    // green for the highlight bar
}  // namespace

MenuResult Menu::run(const std::vector<std::string>& items,
                     int selected_index) const {
  MenuResult result;
  if (items.empty()) {
    result.cancelled = true;
    return result;
  }
  // std::clamp is C++17 but not always present in the Arduino libc++; use
  // a local clamp to stay portable across the toolchains we may target.
  int sel = selected_index;
  if (sel < 0) sel = 0;
  const int max_sel = static_cast<int>(items.size()) - 1;
  if (sel > max_sel) sel = max_sel;
  auto draw = [&]() mutable {
    display_.clear();
    for (std::size_t i = 0; i < items.size(); ++i) {
      const int y = kTop + static_cast<int>(i) * kRowH;
      if (static_cast<int>(i) == sel) {
        display_.rect(0, y - 1, platform::Display::kWidth, kRowH, kSel);
      }
      const std::string& label = items[i];
      // Phase 1: truncate on the right at the screen edge; A12 will
      // add horizontal scrolling for long labels.
      const int max_chars =
          (platform::Display::kWidth - kLeft) / 6;
      const std::string shown =
          label.size() > static_cast<std::size_t>(max_chars)
              ? label.substr(0, std::max(0, max_chars - 1)) + "~"
              : label;
      display_.text(kLeft, y, kFg, shown);
    }
    display_.push();
  };

  draw();
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) {
      result.index = sel;
      result.cancelled = true;
      return result;
    }
    if (ev.enter) {
      result.index = sel;
      result.confirmed = true;
      return result;
    }
    // ';' is the Cardputer's down-arrow key on the rightmost letter.
    // For a portable menu we also accept 'j' / 'k' / arrow symbols
    // when they are present in the printable stream.
    if (ev.c == ';' || ev.c == 'j' || ev.c == '>') {
      if (sel < static_cast<int>(items.size()) - 1) {
        ++sel;
        draw();
      }
    } else if (ev.c == '.' || ev.c == 'k' || ev.c == '<') {
      if (sel > 0) {
        --sel;
        draw();
      }
    } else if (ev.backspace) {
      if (sel > 0) {
        --sel;
        draw();
      }
    }
  }
}

}  // namespace ui
}  // namespace advdeck
