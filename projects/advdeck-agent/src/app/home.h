// src/app/home.h
//
// A12 UX polish: the home route. The top-level menu, with a
// persistent footer that names the route-specific keybindings
// and a '?' shortcut to the help route.

#ifndef ADVDECK_SRC_APP_HOME_H_
#define ADVDECK_SRC_APP_HOME_H_

#include "app/routes.h"

namespace advdeck {
namespace app {

// Real implementation of the Home route. Renders the top-level
// menu, the A12 footer, and intercepts '?' to jump to the help
// route. The thin wrapper in routes.cpp calls this.
Route route_home_impl(Ctx& ctx);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_HOME_H_
