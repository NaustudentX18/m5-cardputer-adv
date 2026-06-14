// src/app/sync.h
//
// B2.1 — Sync status UI. Per PHASE-3-INTERFACES.md §9.1, this route
// shows pending / in_flight / done / errored counts, the last 5 done
// rows, the pending + errored rows, and binds 'r' (retry the
// highlighted errored row) and 'c' (compact done rows older than 7
// days). Esc returns home.
//
// `render_sync_screen(Ctx&)` is the host-testable helper that
// returns the on-screen content as a string. The blocking route
// in `route_sync_impl` calls it once per redraw.

#ifndef ADVDECK_SRC_APP_SYNC_H_
#define ADVDECK_SRC_APP_SYNC_H_

#include <string>
#include <vector>

#include "advdeck/pending_request.h"
#include "app/routes.h"

namespace advdeck {
namespace app {

// Public summary counts used by the sync screen and any future
// status indicator. Counts come from OutboxQueue::load_all.
struct SyncCounts {
  int pending = 0;
  int in_flight = 0;
  int done = 0;
  int errored = 0;
};

// Build the count summary from a vector of rows (any status). Public
// so host tests can call it directly without going through the
// OutboxQueue.
SyncCounts compute_sync_counts(
    const std::vector<PendingRequest>& rows);

// Render the full sync screen as a multi-line string. The route
// uses this for drawing; host tests call it directly. Each line
// below the header is "\t" separated: <status>\t<id>\t<created_at>\t<project>.
// The string ends with a trailing newline.
std::string render_sync_screen(Ctx& ctx);

// Real implementation of the Sync route. Returns Route::Home on
// exit. Definition in sync.cpp.
Route route_sync_impl(Ctx& ctx);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_SYNC_H_
