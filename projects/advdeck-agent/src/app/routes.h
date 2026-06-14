// src/app/routes.h
//
// Per-screen dispatcher. main.cpp owns the loop and the active route;
// each route draws once when entered, then is driven by `tick(ctx,
// key_event)` from the main loop. Phase 1 keeps the routes tiny:
// print the route name to the display, wait for any key, and return
// to the home route. A03 / A04 / A05 will fill in real content.

#ifndef ADVDECK_SRC_APP_ROUTES_H_
#define ADVDECK_SRC_APP_ROUTES_H_

#include <string>

#include "advdeck/project_store.h"
#include "advdeck/storage.h"

namespace advdeck {
class TaskStore;     // A04
class CalendarStore; // A05
class StagingQueue;  // B2.1 — accepts/rejects bridge-produced artifacts
class OutboxQueue;   // B2.1 — sync UI reads + retries rows
class AgentPackExporter; // B2.1 — Export route trigger
namespace app {

struct Ctx {
  IStorage& storage;
  ProjectStore& projects;
  // Lazy: a per-project TaskStore the dispatcher creates on demand.
  // The function pointer is non-null only after A04 wires it up.
  TaskStore* (*tasks_for)(const std::string& project_slug, void* ctx) =
      nullptr;
  void* tasks_ctx = nullptr;
  // A05 will provide a single CalendarStore for the whole app.
  // Forward-declared so we don't pull its header in here.
  CalendarStore* calendar = nullptr;
  // B2.1: a single OutboxQueue shared by the sync UI and any
  // future callers (the export route looks at it too). Lazy —
  // main.cpp constructs it once the dispatcher starts.
  OutboxQueue* outbox = nullptr;
  // B2.1: a single StagingQueue. B3.1's review screen consumes
  // the same instance.
  StagingQueue* staging = nullptr;
  // B2.1: AgentPackExporter for the Export menu entry. C1.2 owns
  // the implementation; the dispatcher lazy-inits this the same
  // way it lazy-inits calendar.
  AgentPackExporter* exporter = nullptr;

  // Set by route_capture_impl after a successful create_project so
  // the dispatcher can resolve a returned Route::ProjectDetail into a
  // real slug. Cleared (or replaced) by the dispatcher once consumed.
  std::string last_created_slug;
  // Explicit ctor so the dispatcher in main.cpp can brace-initialize
  // even though the struct has default member initializers (which
  // would otherwise make it non-aggregate under C++14).
  Ctx(IStorage& s, ProjectStore& p,
      TaskStore* (*tf)(const std::string&, void*) = nullptr,
      void* tc = nullptr,
      CalendarStore* cal = nullptr,
      OutboxQueue* ob = nullptr,
      StagingQueue* sq = nullptr,
      AgentPackExporter* ex = nullptr)
      : storage(s), projects(p), tasks_for(tf), tasks_ctx(tc),
        calendar(cal), outbox(ob), staging(sq), exporter(ex) {}
};

enum class Route { Home, Capture, ProjectList, ProjectDetail, TaskList,
                   Calendar, Sync, Export, Review, Record };

// Enter (draw) and tick (handle one key event) for each route. tick
// returns the route to switch to; Route::Home to stay.
Route route_home(Ctx& ctx);
Route route_capture(Ctx& ctx);
Route route_project_list(Ctx& ctx);
Route route_project_detail(Ctx& ctx, const std::string& slug);
Route route_task_list(Ctx& ctx, const std::string& slug);
Route route_calendar(Ctx& ctx);
Route route_sync(Ctx& ctx);
Route route_export(Ctx& ctx);
Route route_review(Ctx& ctx, const std::string& request_id);
Route route_record(Ctx& ctx);
Route route_record_list(Ctx& ctx, const std::string& slug);

// One-shot render-and-wait. Draws the route label and blocks until
// any key is pressed, then returns the route the dispatcher should
// jump to (Home for the Phase 1 stubs). Defined in routes.cpp.
Route render_route_label(Ctx& ctx, const std::string& label);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_ROUTES_H_
