// src/app/home.cpp
//
// A12 UX polish: home route. The top-level menu now carries a
// persistent footer (PHASE-5-INTERFACES.md §6.2) and a '?'
// shortcut to the help route.
//
// The ui::Menu helper's run() blocks on its own poll loop and
// doesn't know about '?', so we re-implement the loop here.
// The drawing code mirrors menu.cpp: a list of labels with a
// green highlight bar over the selected row, plus a 12-px
// footer strip at y = 122.

#include "app/home.h"
#include "app/routes.h"

#include <cstdint>
#include <string>
#include <vector>

#include "platform/display.h"
#include "platform/keyboard.h"
#include "ui/layout.h"
#include "ui/status_bar.h"

namespace advdeck {
namespace app {

namespace {

platform::Display& disp() {
  static platform::Display d;
  return d;
}

constexpr int kFooterY = 122;
constexpr int kRowH = 10;
constexpr int kListTop = 16;
constexpr int kLeft = 4;
constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kDim = 0x7BEF;
constexpr uint16_t kSel = 0x07E0;

// A12 footer. Drawn on every frame so it stays in sync with
// the route the user is on.
constexpr const char* kHomeFooter =
    "[r] record  [p] projects  [c] calendar  [?] help  [esc] exit";

const std::vector<std::string>& menu_items() {
  static const std::vector<std::string> items = {
      "Capture", "Projects", "Tasks", "Calendar",
      "Record",  "Sync",     "Export", "Review",
  };
  return items;
}

Route route_for_index(int index) {
  switch (index) {
    case 0: return Route::Capture;
    case 1: return Route::ProjectList;
    case 2: return Route::TaskList;
    case 3: return Route::Calendar;
    case 4: return Route::Record;
    case 5: return Route::Sync;
    case 6: return Route::Export;
    case 7: return Route::Review;
    default: return Route::Home;
  }
}

int kMaxChars() {
  return (platform::Display::kWidth - kLeft) / 6;
}

void draw_menu(int sel) {
  const auto& items = menu_items();
  for (std::size_t i = 0; i < items.size(); ++i) {
    const int y = kListTop + static_cast<int>(i) * kRowH;
    if (static_cast<int>(i) == sel) {
      disp().rect(0, y - 1, platform::Display::kWidth, kRowH, kSel);
    }
    const std::string& label = items[i];
    const int max_chars = kMaxChars();
    const std::string shown =
        label.size() > static_cast<std::size_t>(max_chars)
            ? label.substr(0, max_chars - 1) + "~"
            : label;
    disp().text(kLeft, y, kFg, shown);
  }
  disp().text(kLeft, kFooterY, kDim, kHomeFooter);
  disp().push();
}

}  // namespace

Route route_home_impl(Ctx& ctx) {
  (void)ctx;
  int sel = 0;
  disp().begin();
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(ctx.storage.is_mounted());
  draw_menu(sel);
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) return Route::Home;
    if (ev.enter) return route_for_index(sel);
    if (ev.c == '?' || ev.c == '/') {
      return Route::Help;
    }
    // Single-letter shortcuts for the most common picks.
    if (ev.c == 'r' || ev.c == 'R') return Route::Record;
    if (ev.c == 'p' || ev.c == 'P') return Route::ProjectList;
    if (ev.c == 'c' || ev.c == 'C') return Route::Calendar;
    if (ev.c == ';' || ev.c == 'j' || ev.c == '>') {
      if (sel < static_cast<int>(menu_items().size()) - 1) {
        ++sel;
        draw_menu(sel);
      }
      continue;
    }
    if (ev.c == '.' || ev.c == 'k' || ev.c == '<') {
      if (sel > 0) {
        --sel;
        draw_menu(sel);
      }
      continue;
    }
    if (ev.backspace) {
      if (sel > 0) {
        --sel;
        draw_menu(sel);
      }
    }
  }
}

}  // namespace app
}  // namespace advdeck
