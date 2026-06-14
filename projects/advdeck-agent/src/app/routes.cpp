// src/app/routes.cpp
//
// Per-screen dispatcher thin wrappers. main.cpp owns the loop and
// the active route; each route function does the work and returns
// the next Route. Phase 1 keeps the routes tiny: the wrappers
// here just delegate to the real implementations in the per-route
// .cpp files. A12 polished the screen rendering without changing
// the dispatcher shape.

#include "app/calendar.h"
#include "app/calendar_editor.h"
#include "app/capture.h"
#include "app/export.h"
#include "app/help.h"
#include "app/home.h"
#include "app/projects.h"
#include "app/recorder.h"
#include "app/reminder_alert.h"
#include "app/review.h"
#include "app/sync.h"
#include "app/tasks.h"

#include "advdeck/outbox_queue.h"
#include "advdeck/staging_queue.h"
#include "app/routes.h"

namespace advdeck {
namespace app {

Route render_route_label(Ctx& /*ctx*/, const std::string& /*label*/) {
  // Phase 1 stub. The dispatcher does not call this for the live
  // routes; A12 removed the platform::Display dependency from
  // here so this file no longer needs to include the M5GFX
  // display.h header. The function is kept as a no-op so existing
  // callers (none in Phase 5) continue to link.
  return Route::Home;
}

Route route_home(Ctx& ctx) {
  return route_home_impl(ctx);
}

Route route_capture(Ctx& ctx) {
  return route_capture_impl(ctx);
}

Route route_project_list(Ctx& ctx) {
  // The project list returns the picked slug via the out_slug
  // arg; the dispatcher stashes it in last_created_slug.
  std::string slug;
  return route_project_list_impl(ctx, &slug);
}

Route route_project_detail(Ctx& ctx, const std::string& slug) {
  return route_project_detail_impl(ctx, slug);
}

Route route_task_list(Ctx& ctx, const std::string& slug) {
  return route_task_list_impl(ctx, slug);
}

Route route_calendar(Ctx& ctx) {
  return route_calendar_impl(ctx);
}

Route route_calendar_editor(Ctx& ctx, const std::string& event_id) {
  // E1.1 thin wrapper. The real key loop lives in
  // calendar_editor.cpp's route_calendar_editor_impl so the host
  // tests can build the screen string without driving a blocking
  // display.
  return route_calendar_editor_impl(ctx, event_id);
}

Route route_sync(Ctx& ctx) {
  return route_sync_impl(ctx);
}

Route route_export(Ctx& ctx) {
  // The dispatcher passes the current slug via last_created_slug
  // (the same channel route_project_list and route_capture use).
  std::string slug;
  if (!ctx.last_created_slug.empty()) {
    slug = ctx.last_created_slug;
    ctx.last_created_slug.clear();
  } else {
    // Fall back to the first project if the user came from the
    // home menu straight to Export without picking a project.
    std::string err;
    std::vector<ProjectSummary> list = ctx.projects.list_projects(&err);
    if (!list.empty()) slug = list[0].slug;
  }
  return route_export_impl(ctx, slug);
}

Route route_review(Ctx& ctx, const std::string& request_id) {
  // B3.1 thin wrapper. The actual key loop lives in review.cpp's
  // route_review_impl so the host tests can build the summary
  // string without driving a blocking display.
  return route_review_impl(ctx, request_id);
}

Route route_record(Ctx& ctx) {
  // D1.1: home menu entry point. Picks the current project
  // (mirroring route_project_detail's fallback to the first
  // project) and dispatches to the list view.
  return route_record_entry_point(ctx);
}

Route route_record_list(Ctx& ctx, const std::string& slug) {
  // D1.1: thin wrapper. The actual key loop lives in
  // recorder.cpp's route_record_list_impl so the host tests
  // can build the screen string without driving a blocking
  // display.
  return route_record_list_impl(ctx, slug);
}

Route route_reminder_alert(Ctx& ctx) {
  // E1.1 thin wrapper. The actual key loop lives in
  // reminder_alert.cpp's route_reminder_alert_impl so the host
  // tests can build the screen string without driving a blocking
  // display.
  return route_reminder_alert_impl(ctx);
}

Route route_help(Ctx& ctx) {
  return route_help_impl(ctx);
}

}  // namespace app
}  // namespace advdeck
