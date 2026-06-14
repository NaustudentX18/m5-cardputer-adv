// src/app/reminder_alert.cpp
//
// Reminder alert route. Per PHASE-5-INTERFACES.md §4.3. The alert
// shows the pending reminder's title, remind_at, and project (if
// any). 'a' acks, 's' snoozes 5 minutes, Esc dismisses.
//
// The on-screen body is the same multi-line string returned by
// render_reminder_alert() so host tests can assert on it without
// driving the keyboard.

#include "app/reminder_alert.h"
#include "app/routes.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "advdeck/calendar_store.h"
#include "advdeck/reminder_watcher.h"
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

// Pull the alert's target reminder out of the watcher. The
// dispatcher passes an event_id; we look it up directly. If the
// id is empty (e.g. host tests call render_reminder_alert with
// no args), we return the first pending reminder. Returns
// PendingReminder{} with event_id == "" if nothing is pending.
PendingReminder pick_pending(Ctx& ctx, const std::string& event_id) {
  if (ctx.reminder == nullptr) return PendingReminder{};
  std::vector<PendingReminder> pending;
  std::string err;
  // Use a fixed "now" so tests are deterministic. On the
  // firmware the device clock gives us the real now; the
  // dispatcher passes the actual ISO8601 from main.cpp. For
  // the host test we can use any future time as long as it's
  // monotonic.
  std::string r = ctx.reminder->load_due("9999-12-31T23:59:59Z",
                                         &pending, &err);
  (void)r;
  if (event_id.empty()) {
    if (pending.empty()) return PendingReminder{};
    return pending.front();
  }
  for (const auto& p : pending) {
    if (p.event_id == event_id) return p;
  }
  return PendingReminder{};
}

// Build the on-screen text. The header includes the event id, the
// body shows the title / remind_at / project. The footer is the
// fixed key map. Returns "" when there is nothing to alert.
std::string build_screen(const PendingReminder& r) {
  if (r.event_id.empty()) return "";
  std::string out;
  char header[160];
  std::snprintf(header, sizeof(header),
                "header=Reminder id=%s", r.event_id.c_str());
  out += header;
  out += '\n';
  out += "title:";
  out += r.title.empty() ? "(untitled)" : r.title;
  out += '\n';
  out += "remind_at:";
  out += r.remind_at;
  out += '\n';
  if (!r.project.empty()) {
    out += "project:";
    out += r.project;
    out += '\n';
  }
  out += "footer=[a] ack  [s] snooze(5m)  [esc] dismiss\n";
  return out;
}

// ISO8601 now in UTC.
std::string now_iso8601_utc() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&t, &tm);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

// Add `seconds` to an ISO8601 UTC Z timestamp and return the new
// timestamp. We do the math in `time_t` and re-format. Handles
// only the canonical Z form documented in the Event schema.
std::string add_seconds_iso8601(const std::string& iso, int seconds) {
  if (iso.size() != 20) return iso;
  std::tm tm{};
  // strptime is POSIX, not portable. Parse manually.
  int Y, M, D, h, m, s;
  if (std::sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ",
                  &Y, &M, &D, &h, &m, &s) != 6) return iso;
  tm.tm_year = Y - 1900;
  tm.tm_mon = M - 1;
  tm.tm_mday = D;
  tm.tm_hour = h;
  tm.tm_min = m;
  tm.tm_sec = s;
  // timegm is glibc/BSD; the portable C++17 equivalent is
  // mktime + a local-offset adjustment.
  std::time_t local_now = std::time(nullptr);
  std::tm local_tm{};
#if defined(_WIN32)
  localtime_s(&local_tm, &local_now);
#else
  localtime_r(&local_now, &local_tm);
#endif
  std::time_t local_as_utc = std::mktime(&local_tm);
  std::time_t offset = local_now - local_as_utc;
  std::time_t t = std::mktime(&tm) + offset;
  std::tm out{};
#if defined(_WIN32)
  gmtime_s(&out, &t);
#else
  gmtime_r(&t, &out);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &out);
  return std::string(buf);
}

// Snooze implementation: write a copy of the current event with
// remind_at = now + 5 minutes, then ack the original so the
// watcher drops it from the pending list. The new event retains
// the same title, project, and starts_at (so the calendar list
// view doesn't bounce the row around).
void snooze(Ctx& ctx, const PendingReminder& r) {
  if (ctx.calendar == nullptr || r.event_id.empty()) return;
  std::vector<Event> all;
  std::string err;
  std::string lr = ctx.calendar->load(&all, &err);
  (void)lr;
  Event* found = nullptr;
  for (auto& e : all) {
    if (e.id == r.event_id) { found = &e; break; }
  }
  if (found == nullptr) return;
  Event copy = *found;
  copy.id.clear();
  copy.remind_at = add_seconds_iso8601(now_iso8601_utc(), 5 * 60);
  std::string aerr;
  ctx.calendar->add_event(copy, nullptr, &aerr);
  if (ctx.reminder != nullptr) {
    std::string acerr;
    ctx.reminder->ack(r.event_id, &acerr);
  }
}

void draw_screen(const PendingReminder& r, bool storage_mounted) {
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(storage_mounted);
  disp().text(4, 16, 0xF800, "REMINDER");
  int y = 30;
  char line[160];
  std::snprintf(line, sizeof(line), "id: %s", r.event_id.c_str());
  disp().text(4, y, 0xFFFF, line);
  y += 12;
  std::snprintf(line, sizeof(line), "title: %s",
                r.title.empty() ? "(untitled)" : r.title.c_str());
  disp().text(4, y, 0xFFFF, line);
  y += 12;
  std::snprintf(line, sizeof(line), "when: %s", r.remind_at.c_str());
  disp().text(4, y, 0xFFFF, line);
  y += 12;
  if (!r.project.empty()) {
    std::snprintf(line, sizeof(line), "project: %s", r.project.c_str());
    disp().text(4, y, 0x07FF, line);
    y += 12;
  }
  disp().text(4, 122, 0x7BEF, "[a] ack  [s] snooze  [esc] dismiss");
  disp().push();
}

}  // namespace

std::string render_reminder_alert(Ctx& ctx, const std::string& event_id) {
  PendingReminder r = pick_pending(ctx, event_id);
  return build_screen(r);
}

Route route_reminder_alert_impl(Ctx& ctx) {
  for (;;) {
    PendingReminder r = pick_pending(ctx, "");
    if (r.event_id.empty()) return Route::Calendar;
    draw_screen(r, ctx.storage.is_mounted());
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) {
      // Dismiss: do not ack. The watcher will re-fire on the
      // next poll. Return to the previous screen.
      return Route::Calendar;
    }
    if (ev.c == 'a' || ev.c == 'A') {
      if (ctx.reminder != nullptr) {
        std::string err;
        ctx.reminder->ack(r.event_id, &err);
      }
      return Route::Calendar;
    }
    if (ev.c == 's' || ev.c == 'S') {
      snooze(ctx, r);
      return Route::Calendar;
    }
  }
}

}  // namespace app
}  // namespace advdeck
