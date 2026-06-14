// src/app/reminder_alert.h
//
// Reminder alert route. Per PHASE-5-INTERFACES.md §4.3. The alert
// shows the pending reminder's title, remind_at, and project (if
// any). 'a' acks (calls ReminderWatcher::ack), 's' snoozes (writes
// a new event with remind_at = now + 5min and acks the old one),
// Esc dismisses without acking (the alert will re-fire on the next
// poll).
//
// The on-screen body is host-testable via render_reminder_alert().

#ifndef ADVDECK_SRC_APP_REMINDER_ALERT_H_
#define ADVDECK_SRC_APP_REMINDER_ALERT_H_

#include <string>

#include "app/routes.h"

namespace advdeck {
namespace app {

// Render the alert screen as a multi-line string. The first
// pending reminder (per ReminderWatcher::load_due, sorted by
// remind_at ascending) is shown. Returns "" if the watcher has
// no pending reminders — the on-screen route would just bounce
// back to the previous screen in that case.
std::string render_reminder_alert(Ctx& ctx, const std::string& event_id);

// Real implementation of the Reminder route. Returns
// Route::Calendar (or whatever the dispatcher should fall back to)
// on exit. Definition in reminder_alert.cpp.
Route route_reminder_alert_impl(Ctx& ctx);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_REMINDER_ALERT_H_
