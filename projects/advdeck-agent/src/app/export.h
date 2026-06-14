// src/app/export.h
//
// B2.1 — Export trigger route. Per PHASE-3-INTERFACES.md §9.1, this
// route picks the current project (the same slug the dispatcher
// used to open the project detail) and calls
// AgentPackExporter::export_project. On success, shows the export
// path; on failure, shows the error.

#ifndef ADVDECK_SRC_APP_EXPORT_H_
#define ADVDECK_SRC_APP_EXPORT_H_

#include <string>

#include "app/routes.h"

namespace advdeck {
namespace app {

// Real implementation of the Export route. Returns Route::Home on
// exit. Definition in export.cpp.
Route route_export_impl(Ctx& ctx, const std::string& current_slug);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_EXPORT_H_
