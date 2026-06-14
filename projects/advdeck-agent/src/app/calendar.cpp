// src/app/calendar.cpp
//
// Phase 1 calendar route. The screen lists upcoming events (sorted
// by starts_at) and accepts the simple input form
//   TITLE | YYYY-MM-DDTHH:MM:SSZ
// to add a new one. The user toggles a row's status with Enter, types
// 'a' to enter add-mode, or hits Esc to return to home. A12 will
// replace the body with a polished UX; for now this exists so the
// menu is wired to a real screen.

#include "app/calendar.h"
#include "app/routes.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "advdeck/calendar_store.h"
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

// Trim leading and trailing ASCII whitespace in place.
std::string trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

// Returns true if `s` looks like a YYYY-MM-DDTHH:MM:SSZ ISO8601
// timestamp. Just enough validation to catch obvious typos — the
// CalendarStore does not re-parse starts_at, so anything we pass
// through will be used as the id-prefix's date anchor.
bool looks_like_iso(const std::string& s) {
  if (s.size() != 20) return false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    bool ok = std::isdigit(static_cast<unsigned char>(c)) || c == '-' ||
              c == 'T' || c == ':' || c == 'Z';
    if (!ok) return false;
  }
  return s[4] == '-' && s[7] == '-' && s[10] == 'T' && s[13] == ':' &&
         s[16] == ':' && s[19] == 'Z';
}

// Capture a single line of text, terminated by Enter. Backspace
// edits. Esc cancels (returns false). Returns true on Enter.
bool read_line(std::string* out) {
  out->clear();
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) return false;
    if (ev.enter) return true;
    if (ev.backspace) {
      if (!out->empty()) out->pop_back();
    } else if (ev.c != '\0') {
      out->push_back(ev.c);
    }
    // Redraw input row.
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(false);
    disp().text(4, 30, 0xFFFF, "New event:");
    disp().text(4, 42, 0x07FF, "  TITLE | YYYY-MM-DDTHH:MM:SSZ");
    disp().text(4, 60, 0xFFFF, "> " + *out);
    disp().push();
  }
}

void draw_event_row(int y, const Event& e, bool selected,
                    uint16_t fg, uint16_t sel) {
  std::string line;
  line.reserve(40);
  line.push_back(selected ? '>' : ' ');
  line.push_back(' ');
  // One-letter status marker: [ ]/[~]/[x].
  char mark = '?';
  if (e.status == "accepted") mark = ' ';
  else if (e.status == "suggested") mark = '~';
  else if (e.status == "rejected") mark = 'x';
  line.push_back('[');
  line.push_back(mark);
  line.push_back(']');
  line.push_back(' ');
  // Use starts_at if present, else remind_at, else "--".
  std::string stamp = e.starts_at.empty() ? e.remind_at : e.starts_at;
  if (stamp.size() > 16) stamp = stamp.substr(0, 16);
  line += stamp;
  line += "  ";
  line += e.title.empty() ? "(untitled)" : e.title;
  disp().text(4, y, selected ? sel : fg, line);
}

}  // namespace

Route route_calendar_impl(Ctx& ctx) {
  if (ctx.calendar == nullptr) {
    // Calendar not wired in (very old main.cpp). Fall back to the
    // label stub so we still get a visible screen.
    disp().begin();
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(false);
    disp().text(4, 24, 0xFFFF, "Calendar (no store)");
    disp().push();
    for (;;) {
      const platform::KeyEvent ev = platform::poll();
      if (ev.any) return Route::Home;
    }
  }

  // Pull the current view: all events sorted by starts_at, with
  // remind_at-only events placed after events that have starts_at.
  std::vector<Event> events;
  std::string err;
  std::string r = ctx.calendar->upcoming("1970-01-01T00:00:00Z", &events, &err);
  (void)r;  // Surfaces to bar() only on firmware that draws SD status.

  int sel = 0;
  for (;;) {
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(ctx.storage.is_mounted());
    disp().text(4, 16, 0xFFFF, "Calendar");
    if (events.empty()) {
      disp().text(4, 36, 0x07FF, "(no events)");
    } else {
      // Up to 6 rows fit on the 135-px screen.
      const int visible = std::min<int>(6, static_cast<int>(events.size()));
      const int first = std::max(0, sel - visible + 1);
      for (int i = 0; i < visible; ++i) {
        const Event& e = events[first + i];
        draw_event_row(28 + i * 14, e, first + i == sel, 0xFFFF, 0x07FF);
      }
    }
    disp().text(4, 122, 0x07FF, "[a] add  [Enter] toggle  [Esc] back");
    disp().push();

    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) return Route::Home;
    if (!events.empty() && ev.c == 'j' && sel < static_cast<int>(events.size()) - 1) {
      ++sel;
      continue;
    }
    if (!events.empty() && ev.c == 'k' && sel > 0) {
      --sel;
      continue;
    }
    if (ev.enter && !events.empty()) {
      // Toggle: accepted <-> suggested (Phase 1: just bounce the
      // status so the user can see the change).
      Event& e = events[sel];
      e.status = (e.status == "suggested") ? "accepted" : "suggested";
      e.id = e.id;  // keep id stable
      std::string serr;
      // Re-save the whole list: simplest correct path for Phase 1.
      // Build a fresh vector that includes the mutated row.
      std::vector<Event> all;
      ctx.calendar->upcoming("1970-01-01T00:00:00Z", &all, &serr);
      // Save: CalendarStore has no public save() exposed in the
      // header, so write back via add_event (which assigns a fresh
      // id) only if the row is new. For a status toggle we instead
      // delete-and-readd. Phase 2 will add a proper update API.
      std::string delerr;
      ctx.calendar->delete_event(e.id, &delerr);
      Event fresh = e;
      fresh.id.clear();
      std::string aerr;
      ctx.calendar->add_event(fresh, nullptr, &aerr);
      // Reload the view.
      events.clear();
      ctx.calendar->upcoming("1970-01-01T00:00:00Z", &events, &err);
      if (sel >= static_cast<int>(events.size()))
        sel = std::max(0, static_cast<int>(events.size()) - 1);
      continue;
    }
    if (ev.c == 'a' || ev.c == 'A') {
      std::string line;
      if (!read_line(&line)) continue;  // user backed out
      // Parse "TITLE | ISO".
      std::string title;
      std::string iso;
      std::size_t sep = line.find('|');
      if (sep == std::string::npos) {
        title = trim(line);
        iso.clear();
      } else {
        title = trim(line.substr(0, sep));
        iso = trim(line.substr(sep + 1));
      }
      if (title.empty()) continue;  // ignore empty submissions
      if (!iso.empty() && !looks_like_iso(iso)) {
        // Bad ISO: show a one-line error and redraw next tick.
        disp().text(4, 100, 0xF800, "Bad date; expected YYYY-MM-DDTHH:MM:SSZ");
        disp().push();
        for (;;) {
          const platform::KeyEvent e2 = platform::poll();
          if (e2.any) break;
        }
        continue;
      }
      Event e;
      e.title = title;
      e.starts_at = iso;
      e.source = "manual";
      e.status = "accepted";
      std::string aerr;
      ctx.calendar->add_event(e, nullptr, &aerr);
      // Reload the view, selecting the row we just added (last).
      events.clear();
      ctx.calendar->upcoming("1970-01-01T00:00:00Z", &events, &err);
      sel = static_cast<int>(events.size()) - 1;
      if (sel < 0) sel = 0;
      continue;
    }
  }
}

}  // namespace app
}  // namespace advdeck
