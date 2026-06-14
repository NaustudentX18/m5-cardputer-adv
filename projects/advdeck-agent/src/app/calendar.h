// src/app/calendar.h
//
// Calendar route: list events, allow adding a new one with the
// "TITLE | YYYY-MM-DDTHH:MM:SSZ" mini-format, and go back to home
// on Esc. Phase 1 is intentionally minimal — no edit, no fancy
// picker. A12 will polish the UX.

#ifndef ADVDECK_SRC_APP_CALENDAR_H_
#define ADVDECK_SRC_APP_CALENDAR_H_

#include "app/routes.h"

namespace advdeck {
namespace app {

// Draws the calendar screen and runs the key loop until the user
// picks an event to toggle, adds a new one, or hits Esc to return
// Real implementation of the Calendar route. Returns Route::Home on
// exit. Definition in calendar.cpp.
Route route_calendar_impl(Ctx& ctx);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_CALENDAR_H_
