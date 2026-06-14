// src/app/help.cpp
//
// A12 UX polish: the help route. A single screen that lists
// the most common keybindings. Reachable from the home menu
// via '?'.
//
// The screen is host-testable: render_help_route() returns the
// same content the firmware pushes to the display, as a
// multi-line string with "key=<binding> desc=<description>"
// lines. The host test asserts on substrings; the firmware
// splits('\n') and writes each line via Display::text().

#include "app/help.h"
#include "app/routes.h"

#include <cstdint>
#include <string>
#include <vector>

#include "platform/display.h"
#include "platform/keyboard.h"
#include "ui/status_bar.h"

namespace advdeck {
namespace app {

namespace {

platform::Display& disp() {
  static platform::Display d;
  return d;
}

constexpr int kHeaderY = 16;
constexpr int kRowH = 10;
constexpr int kListTop = 28;
constexpr int kFooterY = 122;
constexpr int kLeft = 4;
constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kDim = 0x7BEF;
constexpr uint16_t kSel = 0x07E0;

struct HelpLine {
  std::string key;
  std::string desc;
};

// A short, screen-sized list. We deliberately keep it
// below 10 lines so a single screen shows everything with
// no scroll.
const std::vector<HelpLine>& help_lines() {
  static const std::vector<HelpLine> lines = {
      {"[?]",        "show this help"},
      {"[r]",        "open recorder"},
      {"[p]",        "open projects"},
      {"[c]",        "open calendar"},
      {"[n]",        "new (project / task / event)"},
      {"[e]",        "edit (idea / event)"},
      {"[space]",    "toggle task status"},
      {"[del]",      "delete (task / event)"},
      {"[esc]",      "back / exit"},
      {"[enter]",    "open / confirm"},
  };
  return lines;
}

void draw_screen() {
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(true);
  disp().text(kLeft, kHeaderY, kFg, "Help");
  const auto& lines = help_lines();
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const int y = kListTop + static_cast<int>(i) * kRowH;
    const std::string row = lines[i].key + "  " + lines[i].desc;
    disp().text(kLeft, y, kFg, row);
  }
  disp().text(kLeft, kFooterY, kDim,
              "[any key] back to home");
  disp().push();
}

}  // namespace

std::string render_help_route(Ctx& ctx) {
  (void)ctx;
  std::string out;
  out += "header=Help\n";
  const auto& lines = help_lines();
  for (const auto& l : lines) {
    out += "key=";
    out += l.key;
    out += " desc=";
    out += l.desc;
    out += '\n';
  }
  out += "footer=[any key] back to home\n";
  return out;
}

Route route_help_impl(Ctx& ctx) {
  (void)ctx;
  draw_screen();
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (ev.any) return Route::Home;
  }
}

}  // namespace app
}  // namespace advdeck
