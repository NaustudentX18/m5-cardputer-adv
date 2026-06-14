// src/platform/keyboard.h
//
// Thin wrapper over M5Cardputer's keyboard. Phase 1 only emits the
// handful of events the route dispatcher cares about: printable chars
// and the small set of control keys (enter, backspace, escape).
// Modifier and special keys are ignored for now.

#ifndef ADVDECK_SRC_PLATFORM_KEYBOARD_H_
#define ADVDECK_SRC_PLATFORM_KEYBOARD_H_

#include <cstdint>

namespace advdeck {
namespace platform {

struct KeyEvent {
  char c = '\0';        // printable ASCII, or '\0' for non-char events
  bool enter = false;
  bool backspace = false;
  bool escape = false;
  bool any = false;     // true if poll() returned something
};

// Drain the keyboard queue and return the next event. Returns an event
// with `any == false` if no key is pressed. Call once per loop tick.
KeyEvent poll();

}  // namespace platform
}  // namespace advdeck

#endif  // ADVDECK_SRC_PLATFORM_KEYBOARD_H_
