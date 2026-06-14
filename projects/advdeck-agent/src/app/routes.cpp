// src/app/routes.cpp
//
// Per-screen dispatcher. Each route draws once when entered, then
// is driven by the main loop. Phase 1 keeps most routes tiny stubs;
// A04 (this task) replaces the TaskList body with a real per-project
// task list, and A03 / A05 will fill in the rest. main.cpp owns the
// loop and the active route; this file just maps Route -> draw+wait.

#include "app/capture.h"
#include "app/projects.h"
#include "app/calendar.h"
#include "app/routes.h"

#include "app/tasks.h"
#include "platform/display.h"
#include "platform/keyboard.h"
#include "ui/menu.h"
#include "ui/status_bar.h"
namespace advdeck {
namespace app {

namespace {
platform::Display& disp() {
  static platform::Display d;
  return d;
}
}  // namespace

Route render_route_label(Ctx& ctx, const std::string& label) {
  (void)ctx;
  disp().begin();
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(/*sd_ok=*/false);
  disp().text(4, 20, 0xFFFF, label);
  disp().push();
  // Block until any key. We intentionally do not redraw; A03 will.
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (ev.any) return Route::Home;
  }
}

Route route_home(Ctx& ctx) {
  (void)ctx;
  disp().begin();
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(/*sd_ok=*/false);
  disp().text(4, 20, 0xFFFF, "AdvDeck Agent");
  disp().text(4, 32, 0x07FF, "[Enter] menu");
  disp().text(4, 42, 0x07FF, "[Esc]   back");
  disp().push();

  // The home route drives the top-level menu. Phase 1 only routes
  // capture / projects / tasks / calendar. Each entry just shows its
  // label for now; A03..A05 will replace the body.
  const std::vector<std::string> items = {
      "Capture", "Projects", "Tasks", "Calendar",
  };
  const ui::Menu menu(disp());
  const ui::MenuResult picked = menu.run(items, 0);
  if (picked.cancelled) return Route::Home;
  switch (picked.index) {
    case 0: return Route::Capture;
    case 1: return Route::ProjectList;
    case 2: return Route::TaskList;
    case 3: return Route::Calendar;
    default: return Route::Home;
  }
}

Route route_capture(Ctx& ctx) {
  // A03 implementation. The impl uses the text editor and creates a
  // new project, then signals Route::ProjectDetail by writing the
  // slug to ctx.last_created_slug. The dispatcher in main.cpp reads
  // that field to translate ProjectDetail into a real route call.
  return route_capture_impl(ctx);
}

Route route_project_list(Ctx& ctx) {
  // A03 implementation. The impl writes the picked slug to
  // ctx.last_created_slug so the dispatcher can chain to the detail
  // route.
  std::string slug;
  const Route r = route_project_list_impl(ctx, &slug);
  if (r == Route::ProjectDetail) {
    ctx.last_created_slug = std::move(slug);
  }
  return r;
}

Route route_project_detail(Ctx& ctx, const std::string& slug) {
  // A03 implementation. 'e' enters idea-edit mode (the text editor);
  // 't' returns Route::TaskList so the dispatcher can open A04's
  // task view. Esc returns to the project list.
  return route_project_detail_impl(ctx, slug);
}

Route route_task_list(Ctx& ctx, const std::string& slug) {
  return route_task_list_impl(ctx, slug);
}

Route route_calendar(Ctx& ctx) {
  return route_calendar_impl(ctx);
}

}  // namespace app
}  // namespace advdeck
