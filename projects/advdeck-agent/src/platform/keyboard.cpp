// src/platform/keyboard.cpp
//
// Phase 1 keyboard poll. Reads M5Cardputer.Keyboard once and produces
// a flat KeyEvent. We deliberately read at most one event per tick to
// keep route handlers small; the queue is drained on subsequent ticks.
//
// M5Cardputer's keyboard layer (v1.x) does not surface an explicit
// "esc" key on the Cardputer-Adv — the function row is opt+fn. We
// report opt as escape for now, which lets the dispatcher send a
// "back to home" intent. A12 will tighten this.

#include "platform/keyboard.h"

#ifdef ADVDECK_FIRMWARE
#include <M5Cardputer.h>
#endif

namespace advdeck {
namespace platform {

KeyEvent poll() {
  KeyEvent ev;
#ifdef ADVDECK_FIRMWARE
  M5Cardputer.update();
  auto& kb = M5Cardputer.Keyboard;
  if (!kb.isChange() || !kb.isPressed()) {
    return ev;
  }
  const auto& ks = kb.keysState();
  // Order matters: control keys first so a printable char that
  // happens to overlap (e.g. '\n' on enter) is reported as a
  // control event.
  if (ks.enter) {
    ev.enter = true;
    ev.any = true;
    return ev;
  }
  if (ks.del) {
    ev.backspace = true;
    ev.any = true;
    return ev;
  }
  // The Cardputer-Adv has no physical Esc key; opt (the row above
  // the letters) is the closest analogue. We expose it as `escape`
  // so the dispatcher can route it as "back".
  if (ks.opt) {
    ev.escape = true;
    ev.any = true;
    return ev;
  }
  // Printable ASCII: the keyboard layer exposes a `word` vector of
  // resolved chars. We surface the first printable byte.
  for (char c : ks.word) {
    if (c >= 0x20 && c < 0x7F) {
      ev.c = c;
      ev.any = true;
      return ev;
    }
  }
#endif
  return ev;
}

}  // namespace platform
}  // namespace advdeck
