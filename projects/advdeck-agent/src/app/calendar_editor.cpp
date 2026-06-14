// src/app/calendar_editor.cpp
//
// Per-event editor route. Per PHASE-5-INTERFACES.md §4.3. The
// editor shows the editable fields (title, starts_at, ends_at,
// remind_at, project) one per row. 'e' enters field-edit mode for
// the highlighted row; 'a' commits (delete-then-add with a fresh
// id so the id regenerates if the user changed the date); 'd'
// deletes the underlying event; Esc returns to the calendar list
// without saving.
//
// The on-screen body is the same multi-line string returned by
// render_calendar_editor() so host tests can assert on it without
// driving the keyboard.

#include "app/calendar_editor.h"
#include "app/routes.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>
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

// Field indices. Order matches the order rendered on screen.
constexpr int kFieldTitle = 0;
constexpr int kFieldStartsAt = 1;
constexpr int kFieldEndsAt = 2;
constexpr int kFieldRemindAt = 3;
constexpr int kFieldProject = 4;
constexpr int kFieldCount = 5;

const char* field_label(int idx) {
  switch (idx) {
    case kFieldTitle:    return "title";
    case kFieldStartsAt: return "starts_at";
    case kFieldEndsAt:   return "ends_at";
    case kFieldRemindAt: return "remind_at";
    case kFieldProject:  return "project";
  }
  return "?";
}

// In-flight state. We keep a single working Event while the user
// edits; 'a' commits by overwriting the on-disk event (delete +
// add_event) and 'd' deletes the original.
struct EditorState {
  Event working;
  int field = kFieldTitle;     // highlighted field
  std::string status;          // last error or transient status line
  bool storage_mounted = false;
};

// Pointer to the field the cursor is on. Caller does not own the
// returned pointer; do not free.
std::string* field_value(EditorState* state, int idx) {
  switch (idx) {
    case kFieldTitle:    return &state->working.title;
    case kFieldStartsAt: return &state->working.starts_at;
    case kFieldEndsAt:   return &state->working.ends_at;
    case kFieldRemindAt: return &state->working.remind_at;
    case kFieldProject:  return &state->working.project;
  }
  return nullptr;
}
const std::string* field_value(const EditorState* state, int idx) {
  switch (idx) {
    case kFieldTitle:    return &state->working.title;
    case kFieldStartsAt: return &state->working.starts_at;
    case kFieldEndsAt:   return &state->working.ends_at;
    case kFieldRemindAt: return &state->working.remind_at;
    case kFieldProject:  return &state->working.project;
  }
  return nullptr;
}

// Pick the next free evt-YYYYMMDD-NNN id for "today" against the
// current set of events. Mirrors CalendarStore::next_id_for_date
// without exposing the helper publicly. We do not backfill
// starts_at; the user enters it in the editor.
std::string next_new_event_id(const std::vector<Event>& events) {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char date_buf[16];
  std::strftime(date_buf, sizeof(date_buf), "%Y%m%d", &tm);
  std::string yyyymmdd(date_buf);
  std::set<std::string> used;
  for (const auto& e : events) {
    if (e.id.size() >= 13 && e.id.compare(0, 4, "evt-") == 0 &&
        e.id.compare(4, 8, yyyymmdd) == 0) {
      used.insert(e.id);
    }
  }
  for (int n = 0; n < 1000; ++n) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "evt-%s-%03d", yyyymmdd.c_str(), n);
    std::string cand(buf);
    if (used.count(cand) == 0) return cand;
  }
  // Overflow: deterministic 2-letter suffix.
  static const char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  for (int attempt = 0; attempt < 64; ++attempt) {
    char buf[24];
    int a = (attempt * 7 + 13) % 36;
    int b = (attempt * 11 + 29) % 36;
    std::snprintf(buf, sizeof(buf), "evt-%s-%c%c", yyyymmdd.c_str(),
                  kAlpha[a], kAlpha[b]);
    std::string cand(buf);
    if (used.count(cand) == 0) return cand;
  }
  return "evt-" + yyyymmdd + "-XX";
}

// Populate `state` from the on-disk event with `event_id`. If
// event_id is empty we synthesize a new id via next_new_event_id.
// On lookup failure we keep the id so the user can edit / re-save
// without bouncing them out of the screen.
void load_state(Ctx& ctx, const std::string& event_id, EditorState* state) {
  state->working = Event();
  state->working.source = "manual";
  state->working.status = "accepted";
  if (ctx.calendar == nullptr) return;
  std::vector<Event> events;
  std::string err;
  std::string r = ctx.calendar->load(&events, &err);
  (void)r;
  if (event_id.empty()) {
    state->working.id = next_new_event_id(events);
    return;
  }
  for (const auto& e : events) {
    if (e.id == event_id) {
      state->working = e;
      return;
    }
  }
  // Event not found: keep the id (the user can re-save to create
  // it). Status flag tells the user what's going on.
  state->working.id = event_id;
  state->status = "event not found, will create on save";
}

// Build the screen text. One line per field plus header/footer
// markers. Empty fields render as the literal "(empty)" so the
// host tests can assert "title is empty". The selected field is
// prefixed with "> " to make it greppable.
std::string build_screen(const std::string& event_id,
                         const EditorState& state) {
  std::string out;
  char header[160];
  std::snprintf(header, sizeof(header),
                "header=CalendarEditor id=%s new=%s",
                state.working.id.empty() ? "(new)" : state.working.id.c_str(),
                event_id.empty() ? "true" : "false");
  out += header;
  out += '\n';
  for (int i = 0; i < kFieldCount; ++i) {
    const std::string* value = field_value(&state, i);
    const char* marker = (i == state.field) ? "> " : "  ";
    const char* shown = (value && !value->empty()) ? value->c_str() : "(empty)";
    char line[160];
    std::snprintf(line, sizeof(line), "%s%s:%s", marker,
                  field_label(i), shown);
    out += line;
    out += '\n';
  }
  if (!state.status.empty()) {
    out += "status:";
    out += state.status;
    out += '\n';
  }
  out += "footer=[e] edit  [a] save  [d] delete  [esc] cancel\n";
  return out;
}

// In-place line editor: reads characters into `*value` until Enter
// (commit) or Esc (cancel). Returns true if the user committed
// (Enter), false if they cancelled.
bool edit_field_line(EditorState* state) {
  std::string* value = field_value(state, state->field);
  if (value == nullptr) return false;
  std::string buffer = *value;
  for (;;) {
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(state->storage_mounted);
    disp().text(4, 16, 0xFFFF, "Edit field");
    disp().text(4, 30, 0x07FF, field_label(state->field));
    disp().text(4, 44, 0xFFFF, "> " + buffer);
    disp().text(4, 122, 0x7BEF, "[enter] ok  [esc] cancel");
    disp().push();
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) return false;
    if (ev.enter) {
      *value = std::move(buffer);
      state->status.clear();
      return true;
    }
    if (ev.backspace) {
      if (!buffer.empty()) buffer.pop_back();
    } else if (ev.c != '\0') {
      buffer.push_back(ev.c);
    }
  }
}

// Persist the working state to the on-disk calendar. We delete the
// existing event (if any) and add_event the working copy; add_event
// assigns a fresh id. The on-disk id changes on every save (so
// the user cannot reliably re-edit the same id by event id), but
// the calendar list reloads on the way back so this is invisible.
void save_state(Ctx& ctx, EditorState* state) {
  if (ctx.calendar == nullptr) {
    state->status = "no calendar store";
    return;
  }
  if (!state->working.id.empty()) {
    std::string derr;
    ctx.calendar->delete_event(state->working.id, &derr);
    (void)derr;  // The id may not exist (new event).
  }
  Event fresh = state->working;
  fresh.id.clear();
  std::string aerr;
  std::string r = ctx.calendar->add_event(fresh, nullptr, &aerr);
  if (!r.empty()) {
    state->status = "save failed: " + r;
    return;
  }
  state->status = "saved";
}

void delete_state(Ctx& ctx, EditorState* state) {
  if (ctx.calendar == nullptr) {
    state->status = "no calendar store";
    return;
  }
  if (state->working.id.empty()) {
    state->status = "nothing to delete";
    return;
  }
  std::string err;
  std::string r = ctx.calendar->delete_event(state->working.id, &err);
  if (!r.empty()) {
    state->status = "delete failed: " + r;
    return;
  }
  state->status = "deleted";
}

void draw_screen(const std::string& event_id, const EditorState& state) {
  (void)event_id;
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(state.storage_mounted);
  disp().text(4, 16, 0xFFFF, "Calendar Editor");
  int y = 30;
  for (int i = 0; i < kFieldCount; ++i) {
    const std::string* value = field_value(&state, i);
    char line[80];
    std::snprintf(line, sizeof(line), "%c %s: %s",
                  (i == state.field ? '>' : ' '),
                  field_label(i),
                  (value && !value->empty()) ? value->c_str() : "(empty)");
    disp().text(4, y, i == state.field ? 0x07FF : 0xFFFF, line);
    y += 12;
  }
  if (!state.status.empty()) {
    disp().text(4, 110, 0xF800, state.status);
  }
  disp().text(4, 122, 0x7BEF, "[e] edit  [a] save  [d] del  [esc] back");
  disp().push();
}

}  // namespace

std::string render_calendar_editor(Ctx& ctx, const std::string& event_id) {
  EditorState state;
  state.storage_mounted = ctx.storage.is_mounted();
  load_state(ctx, event_id, &state);
  return build_screen(event_id, state);
}

Route route_calendar_editor_impl(Ctx& ctx, const std::string& event_id) {
  EditorState state;
  state.storage_mounted = ctx.storage.is_mounted();
  load_state(ctx, event_id, &state);
  for (;;) {
    draw_screen(event_id, state);
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) return Route::Calendar;
    if (ev.c == 'j' && state.field < kFieldCount - 1) {
      ++state.field;
      continue;
    }
    if (ev.c == 'k' && state.field > 0) {
      --state.field;
      continue;
    }
    if (ev.c == 'e' || ev.c == 'E') {
      edit_field_line(&state);
      continue;
    }
    if (ev.c == 'a' || ev.c == 'A') {
      save_state(ctx, &state);
      if (state.status == "saved") return Route::Calendar;
      continue;
    }
    if (ev.c == 'd' || ev.c == 'D') {
      delete_state(ctx, &state);
      if (state.status == "deleted") return Route::Calendar;
      continue;
    }
  }
}

}  // namespace app
}  // namespace advdeck
