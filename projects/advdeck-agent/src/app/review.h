// src/app/review.h
//
// B3.1 — Review screen for bridge-produced artifacts awaiting user
// accept / reject. Per PHASE-3-INTERFACES.md §9.3, the route shows
// the project slug + title, the first 5 lines of brief.md, the
// task count + first 3 task titles, the calendar suggestion count,
// the agent-prompt first line, and binds:
//   Enter — accept (StagingQueue::accept) and return home
//   Esc   — reject (StagingQueue::reject) and return home
//   e, t, c, a — read-only view of brief.md / tasks.json /
//                calendar-suggestions.json / agent-prompt.md
//
// `render_review_summary(Ctx&, request_id)` is the host-testable
// helper that returns the on-screen content as a string for the
// summary view (slug, title, brief first 5 lines, task count +
// first 3 titles, calendar count, agent-prompt first line). It
// returns "" for an unknown request id or a missing staging entry.
// The blocking route in `route_review_impl` calls it once per
// redraw and drives the key loop on top.

#ifndef ADVDECK_SRC_APP_REVIEW_H_
#define ADVDECK_SRC_APP_REVIEW_H_

#include <string>

#include "app/routes.h"

namespace advdeck {
namespace app {

// Build a multi-line summary of the staging entry for `request_id`
// for the summary view. The host tests assert on substrings of
// this string; the route also re-uses it as the on-screen body.
// Returns "" if the request is unknown or has no staging entry.
std::string render_review_summary(Ctx& ctx, const std::string& request_id);

// Build a multi-line listing of the most recent `max_rows` staging
// entries (status, arrived_at, project). Used by the home menu's
// "Review" entry to pick the next request_id. The host tests and
// the dispatcher both call this. Returns "" when there are no
// pending entries.
std::string render_recent_staging(Ctx& ctx, int max_rows = 5);

// Real implementation of the Review route. Drives the key loop:
//   Enter — StagingQueue::accept(request_id) -> return Home
//   Esc   — StagingQueue::reject(request_id) -> return Home
//   e, t, c, a — read-only view of brief.md / tasks.json /
//                calendar-suggestions.json / agent-prompt.md
// Returns Route::Home on exit.
Route route_review_impl(Ctx& ctx, const std::string& request_id);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_REVIEW_H_
