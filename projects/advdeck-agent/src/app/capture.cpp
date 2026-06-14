// src/app/capture.cpp
//
// The Capture route drives a TextEditor, takes the first non-empty
// line as the project title, and calls ctx.projects.create_project.
// On success we set ctx.last_created_slug to the new project's slug
// and return Route::ProjectDetail so the dispatcher can open it. On
// cancel we return Route::Home. A create failure (typically "SD not
// mounted") is shown to the user and we re-enter the editor with the
// buffer intact.

#include "app/capture.h"
#include "app/routes.h"

#include <cstdint>
#include <string>

#include "advdeck/project_store.h"
#include "advdeck/storage.h"
#include "platform/display.h"
#include "platform/keyboard.h"
#include "ui/status_bar.h"
#include "ui/text_editor.h"

namespace advdeck {
namespace app {

namespace {

platform::Display& disp() {
  static platform::Display d;
  return d;
}

// Trim ASCII whitespace from a line. The user typically types
// "# My idea\n\nbody" — we want the H1 line and nothing else as the
// title (project_store already strips the leading "# " and leading
// whitespace when extracting the H1 for the summary).
std::string trim_ws(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' ||
                   s[a] == '\n')) {
    ++a;
  }
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' ||
                   s[b - 1] == '\n')) {
    --b;
  }
  return s.substr(a, b - a);
}

// Pick the first non-empty line out of the buffer. Empty buffer
// returns "".
std::string first_nonempty_line(const std::string& buf) {
  std::size_t i = 0;
  while (i < buf.size()) {
    std::size_t j = buf.find('\n', i);
    if (j == std::string::npos) j = buf.size();
    std::string line = buf.substr(i, j - i);
    if (line.back() == '\r') line.pop_back();
    if (!trim_ws(line).empty()) {
      return line;
    }
    i = j + 1;
  }
  return "";
}

void show_error(const std::string& msg) {
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(false);
  disp().text(4, 24, 0xF800 /* red */, "Save failed:");
  disp().text(4, 40, 0xFFFF, msg);
  disp().text(4, 100, 0x7BEF, "[any key] back to editor");
  disp().push();
  // Block until the user dismisses.
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (ev.any) return;
  }
}

}  // namespace

Route route_capture_impl(Ctx& ctx) {
  std::string initial;
  for (;;) {
    ui::TextEditor editor(initial, "New idea:");
    ui::EditorResult r = editor.run();
    if (r.cancelled) return Route::Home;

    std::string title = first_nonempty_line(r.text);
    if (title.empty()) {
      // Nothing to save; treat as cancel.
      return Route::Home;
    }

    std::string err;
    std::string slug = ctx.projects.create_project(title, r.text, &err);
    if (!err.empty() || slug.empty()) {
      show_error(err.empty() ? "unknown error" : err);
      // Re-open the editor with the same buffer so the user can fix
      // the issue and try again.
      initial = r.text;
      continue;
    }

    // Stash the new slug on the Ctx so the dispatcher can open the
    // project detail view. Ctx::last_created_slug is a tiny deviation
    // from the Phase 1 contract: it is only written here, and only
    // read by the dispatcher to translate a ProjectDetail intent
    // into the right slug.
    ctx.last_created_slug = slug;
    return Route::ProjectDetail;
  }
}

}  // namespace app
}  // namespace advdeck
