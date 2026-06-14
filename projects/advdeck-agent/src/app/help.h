// src/app/help.h
//
// A12 UX polish: the help route. Single-screen keybinding map
// reachable from the home menu via '?'.

#ifndef ADVDECK_SRC_APP_HELP_H_
#define ADVDECK_SRC_APP_HELP_H_

#include <string>

#include "app/routes.h"

namespace advdeck {
namespace app {

// Render the help screen as a multi-line string. Host-testable.
// The list of keys is intentionally short: a single screen on
// the 240x135 panel can fit ~10 rows of 8-px text.
std::string render_help_route(Ctx& ctx);

// Real implementation of the Help route. Renders the help
// screen and waits for any key to return to the previous
// route (per the dispatcher contract, it returns Route::Home
// when the user dismisses; the dispatcher handles the
// re-route).
Route route_help_impl(Ctx& ctx);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_HELP_H_
