// src/ui/text_editor.h
//
// Multiline text editor. Used by the Capture route (new idea) and the
// Project detail route (e-edits the idea). The editor is a thin
// stateful widget: it owns the buffer, the cursor, and the current
// scroll row, and exposes a single `tick(KeyEvent)` so a host test can
// drive it without any display. On firmware the run() loop also
// redraws between ticks; on host the test just calls tick() and reads
// the buffer / done() state.
//
// M5 / display calls are gated by ADVDECK_FIRMWARE so the editor
// compiles into the host test binary without pulling in M5Cardputer.

#ifndef ADVDECK_SRC_UI_TEXT_EDITOR_H_
#define ADVDECK_SRC_UI_TEXT_EDITOR_H_

#include <string>

#include "platform/keyboard.h"

namespace advdeck {
namespace ui {

struct EditorResult {
  // Exactly one of these is true when run() returns.
  bool confirmed = false;  // user pressed Enter on a finished buffer
  bool cancelled = false;  // user pressed Esc
  std::string text;        // the buffer at exit
};

class TextEditor {
 public:
  // Construct with an initial buffer (typically ""). `editing` is
  // the on-screen label for the prompt ("New idea", "Edit idea", etc).
  TextEditor(std::string initial, std::string editing_label);

  // Read-only accessors used by run() and by host tests.
  const std::string& buffer() const { return buffer_; }
  int cursor() const { return cursor_; }
  int scroll_y() const { return scroll_y_; }
  const std::string& label() const { return label_; }

  // True once tick() has set confirmed_ or cancelled_. run() also
  // checks this on each iteration.
  bool done() const { return confirmed_ || cancelled_; }
  bool confirmed() const { return confirmed_; }
  bool cancelled() const { return cancelled_; }

  // The 240x135 screen fits 10 rows of 10 px each below the 12-px
  // status bar. The editor draws the prompt on row 0 of the content
  // area and reserves a 1-row footer for hints.
  static constexpr int kVisibleRows = 10;
  static constexpr int kRowH = 10;
  static constexpr int kContentTop = 14;  // below the 12-px status bar + gap

  // Process one key event. Returns true if the event changed state
  // (used by run() to know whether to redraw). The host test relies on
  // this being pure: no display, no time, no globals.
  bool tick(const platform::KeyEvent& ev);

  // Firmware-only: drive a blocking event loop until done(). Redraws
  // the screen between ticks. On host this is unused; tests call
  // tick() directly.
  EditorResult run();

  // Test/host helpers: rewind to a known state.
  void reset(std::string initial);

 private:
  // Insert a printable char at the cursor. Returns true if the buffer
  // changed. The editor accepts ASCII 0x20..0x7E plus a small handful
  // of extended chars (treated as raw bytes).
  bool insert_char(char c);
  // Backspace one position to the left of the cursor. If the cursor
  // is at the start of a line, the line is joined with the previous
  // one and the cursor lands at the join point. Returns true on
  // change.
  bool backspace();

  // Convert cursor offset to (row, col) and back.
  void offset_to_rowcol(int offset, int* row, int* col) const;
  int rowcol_to_offset(int row, int col) const;

  // Repaint the editor frame. Only compiled on firmware; on host it
  // is a no-op so the editor stays testable.
  void redraw();

  std::string buffer_;
  std::string label_;
  int cursor_ = 0;     // byte offset in buffer_
  int scroll_y_ = 0;   // first visible content row
  bool confirmed_ = false;
  bool cancelled_ = false;
};

}  // namespace ui
}  // namespace advdeck

#endif  // ADVDECK_SRC_UI_TEXT_EDITOR_H_
