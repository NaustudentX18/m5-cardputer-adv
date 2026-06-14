// src/app/tasks.h
//
// Per-project tasks route. The route loads tasks.json for the
// selected project, shows a scrollable list, and binds a few keys:
//   ' '    toggle status todo -> doing -> done -> todo on the
//          highlighted task
//   del    delete the highlighted task
//   n      add a new task (single-line title input at the bottom)
//   esc    go back to the previous route (returns Route::Home for
//          the Phase 1 stub; later we may return a "back" sentinel)
//
// This header is small because the route is a single function. The
// implementation lives in tasks.cpp. The function signature matches
// the declaration in src/app/routes.h.

#ifndef ADVDECK_SRC_APP_TASKS_H_
#define ADVDECK_SRC_APP_TASKS_H_

#include "app/routes.h"

namespace advdeck {
namespace app {

// Real implementation of the TaskList route. Returns Route::Home on
// exit. Definition in tasks.cpp.
Route route_task_list_impl(Ctx& ctx, const std::string& slug);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_TASKS_H_
