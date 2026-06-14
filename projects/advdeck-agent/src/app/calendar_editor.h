// src/app/calendar_editor.h
//
// Per-event editor route. Per PHASE-5-INTERFACES.md §4.3. The
// editor shows the editable fields (title, starts_at, ends_at,
// remind_at, project) one per row. 'e' enters field-edit mode for
// the highlighted row; 'a' commits (calls CalendarStore::add_event);
// 'd' deletes; Esc returns to the calendar list without saving.
//
// The on-screen body is host-testable via render_calendar_editor().

#ifndef ADVDECK_SRC_APP_CALENDAR_EDITOR_H_
#define ADVDECK_SRC_APP_CALENDAR_EDITOR_H_

#include <string>

#include "app/routes.h"

namespace advdeck {
namespace app {

// Render the editor screen as a multi-line string. Used by both
// the host tests and the on-screen renderer. `event_id` may be
// empty, in which case the editor uses a freshly-allocated id and
// a default starts_at of "" (reminder-only event).
std::string render_calendar_editor(Ctx& ctx, const std::string& event_id);

// Real implementation of the CalendarEditor route. Returns
// Route::Calendar on exit. Definition in calendar_editor.cpp.
Route route_calendar_editor_impl(Ctx& ctx, const std::string& event_id);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_CALENDAR_EDITOR_H_
