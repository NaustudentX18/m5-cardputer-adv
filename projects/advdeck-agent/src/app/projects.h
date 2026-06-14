// src/app/projects.h
//
// Project browser routes: a list of all projects (sorted by
// modified_at desc, then slug) and a read-only detail view of one
// project that also offers an e-edit idea mode. Real implementations
// live in projects.cpp; the dispatcher in routes.cpp calls them.

#ifndef ADVDECK_SRC_APP_PROJECTS_H_
#define ADVDECK_SRC_APP_PROJECTS_H_

#include <string>
#include <vector>

#include "app/routes.h"

namespace advdeck {
namespace app {

// Real implementation of the ProjectList route. Returns one of:
//   - Route::ProjectDetail, after the user picked a row. The chosen
//     slug is written to *out_slug (so the dispatcher can route the
//     follow-up call). On entry, *out_slug is cleared.
//   - Route::Capture, if the user pressed 'n' to start a new idea.
//   - Route::Home, if the user pressed Esc.
Route route_project_list_impl(Ctx& ctx, std::string* out_slug);

// Real implementation of the ProjectDetail route. Reads idea.md for
// `slug` and shows it read-only. Keys:
//   - 'e' enters idea-edit mode (a TextEditor bound to the current
//     idea text). On save, write_idea is called; on cancel the edit
//     is dropped.
//   - 't' returns Route::TaskList so the dispatcher can open the
//     task list for this project.
//   - Esc returns Route::ProjectList.
Route route_project_detail_impl(Ctx& ctx, const std::string& slug);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_PROJECTS_H_
