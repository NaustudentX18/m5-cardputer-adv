// src/app/tasks.cpp
//
// Real implementation of the per-project task list route. Phase 1
// keeps the UI minimal: a scrollable list of titles, a status
// indicator ([ ] / [>] / [x]), and a few keys bound to common
// actions. Full text editing and selection are intentionally out of
// scope — A03 owns the multiline idea editor; A12 will polish the
// task list visuals.

#include "app/tasks.h"

#include <cstdint>
#include <string>
#include <vector>

#include "advdeck/task_store.h"
#include "platform/display.h"
#include "platform/keyboard.h"
#include "ui/status_bar.h"

namespace advdeck {
namespace app {

namespace {

// Top-of-screen strip is 12 px (status bar). Below that we draw a
// header (12 px) with the project slug, then 11 rows of 10 px each,
// giving us room for ~10 visible tasks on a 240x135 panel.
constexpr int kHeaderY = 12;
constexpr int kRowH = 10;
constexpr int kListTop = kHeaderY + 12;
constexpr int kLeft = 4;
constexpr int kMaxTitleChars =
    (platform::Display::kWidth - kLeft - 6 /*status + id + ": "*/) / 6;
constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kSel = 0x07E0;     // green for the highlight bar
constexpr uint16_t kDim = 0x7BEF;     // dim grey for status / id

platform::Display& disp() {
  static platform::Display d;
  return d;
}

// Truncate a title to fit on one row, appending '~' if we cut it.
std::string clip(const std::string& s, int max_chars) {
  if (max_chars <= 1) return "~";
  if (static_cast<int>(s.size()) <= max_chars) return s;
  return s.substr(0, max_chars - 1) + "~";
}

// One row of the task list. status: "todo" / "doing" / "done".
// "doing" gets a > glyph so it stands out on the 1x font.
std::string status_glyph(const std::string& status) {
  if (status == "done") return "[x]";
  if (status == "doing") return "[>]";
  return "[ ]";
}

void draw(const std::string& slug, const std::vector<Task>& tasks,
          int sel) {
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(/*sd_ok=*/true);
  disp().text(kLeft, kHeaderY, kFg, "Tasks: " + slug);
  // Draw rows.
  for (std::size_t i = 0; i < tasks.size(); ++i) {
    const int y = kListTop + static_cast<int>(i) * kRowH;
    if (static_cast<int>(i) == sel) {
      disp().rect(0, y - 1, platform::Display::kWidth, kRowH, kSel);
    }
    const std::string glyph = status_glyph(tasks[i].status);
    const std::string title =
        clip(tasks[i].title, kMaxTitleChars);
    disp().text(kLeft, y, kFg, glyph + " " + title);
  }
  // Footer hint. Bottom of screen is 135; reserve 10 px above.
  const int footer_y = platform::Display::kHeight - kRowH;
  disp().text(kLeft, footer_y, kDim, "[space]toggle [del]del [n]new [esc]back");
  disp().push();
}

// One-line input at the bottom. Enter confirms, Esc cancels. The
// input is appended to `out` as the user types. Returns true if the
// user confirmed, false if they cancelled. The status bar / list
// are not redrawn while typing — the user has a visible prompt the
// whole time and we restore the list on return.
bool prompt_for_title(std::string* out) {
  out->clear();
  const int footer_y = platform::Display::kHeight - kRowH;
  for (;;) {
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(true);
    disp().text(kLeft, kHeaderY, kFg, "New task");
    disp().text(kLeft, footer_y, kFg, "> " + *out + "_");
    disp().text(kLeft, footer_y - kRowH, kDim, "[enter]ok [esc]cancel");
    disp().push();
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) return false;
    if (ev.enter) return !out->empty();
    if (ev.backspace) {
      if (!out->empty()) out->pop_back();
      continue;
    }
    if (ev.c >= 0x20 && ev.c < 0x7F) {
      out->push_back(ev.c);
    }
  }
}

}  // namespace

Route route_task_list_impl(Ctx& ctx, const std::string& slug) {
  // The dispatcher in main.cpp currently calls route_task_list with
  // a stub. We use ctx.projects + ctx.storage directly so this
  // route works whether or not main.cpp has wired up the lazy
  // Ctx::tasks_for pointer. The Ctx hook remains the future API.
  TaskStore store(ctx.storage, ctx.projects.project_dir(slug));
  std::vector<Task> tasks;
  std::string err;
  std::string rc = store.load(&tasks, &err);
  if (!rc.empty() && tasks.empty()) {
    // Recoverable parse error — store has moved the bad file aside
    // and reset to empty. Show the user a hint in the header.
    disp().begin();
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(true);
    disp().text(kLeft, kHeaderY, kFg, "Tasks: " + slug);
    disp().text(kLeft, kListTop, kFg, "(load: " + clip(err, 28) + ")");
    disp().text(kLeft, platform::Display::kHeight - kRowH, kDim,
                "[esc]back");
    disp().push();
    for (;;) {
      const platform::KeyEvent ev = platform::poll();
      if (!ev.any) continue;
      if (ev.escape) return Route::Home;
    }
  }

  int sel = 0;
  disp().begin();
  draw(slug, tasks, sel);
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) return Route::Home;
    if (ev.enter) {
      // Phase 1: enter on a row is a no-op (toggle is on space).
      // We keep it bound so the user gets no surprise on touch.
      continue;
    }
    if (ev.backspace) {
      if (tasks.empty()) continue;
      std::string e;
      store.delete_task(tasks[sel].id, &e);
      tasks.erase(tasks.begin() + sel);
      if (sel >= static_cast<int>(tasks.size())) {
        sel = static_cast<int>(tasks.size()) - 1;
        if (sel < 0) sel = 0;
      }
      draw(slug, tasks, sel);
      continue;
    }
    if (ev.c == ' ') {
      if (tasks.empty()) continue;
      const std::string cur = tasks[sel].status;
      std::string next = "doing";
      if (cur == "doing") next = "done";
      else if (cur == "done") next = "todo";
      else if (cur == "todo") next = "doing";
      std::string e;
      store.set_status(tasks[sel].id, next, &e);
      if (e.empty()) {
        tasks[sel].status = next;
        draw(slug, tasks, sel);
      }
      continue;
    }
    if (ev.c == 'n' || ev.c == 'N') {
      std::string title;
      if (!prompt_for_title(&title)) {
        draw(slug, tasks, sel);
        continue;
      }
      std::string e;
      Task added;
      store.add_task(title, &added, &e);
      if (e.empty()) {
        // Reload to get the canonical task list back. This also
        // picks up any concurrent updates if the user has multiple
        // views open (Phase 1 is single-user, but cheap to do).
        store.load(&tasks, &e);
        sel = static_cast<int>(tasks.size()) - 1;
        draw(slug, tasks, sel);
      }
      continue;
    }
    // Navigation. Match the menu's keys (; . j k) plus the obvious
    // arrow fallbacks.
    if (ev.c == ';' || ev.c == 'j' || ev.c == '>') {
      if (sel < static_cast<int>(tasks.size()) - 1) {
        ++sel;
        draw(slug, tasks, sel);
      }
      continue;
    }
    if (ev.c == '.' || ev.c == 'k' || ev.c == '<') {
      if (sel > 0) {
        --sel;
        draw(slug, tasks, sel);
      }
      continue;
    }
  }
}

}  // namespace app
}  // namespace advdeck
