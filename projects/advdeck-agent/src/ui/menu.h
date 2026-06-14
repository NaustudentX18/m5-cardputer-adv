// src/ui/menu.h
//
// Tiny key-driven menu. The menu owns no state between calls; routes
// pass the list of items each time they need to draw it. The menu
// drives `selected_index` up/down with `;` (the Cardputer's down key
// is the rightmost letter row, but we accept both arrow up/down for
// host testing) and returns on Enter or Esc. The function is
// synchronous: it blocks on key input but does not block on anything
// else (no SD I/O, no network).

#ifndef ADVDECK_SRC_UI_MENU_H_
#define ADVDECK_SRC_UI_MENU_H_

#include <cstddef>
#include <string>
#include <vector>

#include "platform/display.h"
#include "platform/keyboard.h"

namespace advdeck {
namespace ui {

// Result of a single run: which item the user confirmed, and whether
// they confirmed (enter) or cancelled (escape).
struct MenuResult {
  int index = 0;
  bool confirmed = false;  // true if Enter, false if Esc
  bool cancelled = false;  // true if Esc
};

class Menu {
 public:
  Menu(platform::Display& display) : display_(display) {}

  // Block until the user picks a row or presses Esc. `items` is a
  // list of labels; `selected_index` is the initial selection (clamped
  // into range). Returns the final selection index and whether the
  // user confirmed or cancelled. The menu is drawn before the first
  // read so the screen updates even if the user holds no key.
  MenuResult run(const std::vector<std::string>& items, int selected_index) const;

 private:
  platform::Display& display_;
};

}  // namespace ui
}  // namespace advdeck

#endif  // ADVDECK_SRC_UI_MENU_H_
