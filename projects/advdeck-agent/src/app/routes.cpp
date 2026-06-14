// src/app/routes.cpp
//
// Phase 1 stubs. Each route draws a short label and waits for any
// key, then returns to the home route. A03 (Capture / ProjectList /
// ProjectDetail) and A04/A05 (TaskList / Calendar) will replace these
// bodies in later tasks. We deliberately keep them tiny so the
// dispatcher is the only place main.cpp needs to know about.

#include "app/routes.h"

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
  return render_route_label(ctx, "Capture");
}

Route route_project_list(Ctx& ctx) {
  return render_route_label(ctx, "Projects");
}

Route route_project_detail(Ctx& ctx, const std::string& slug) {
  // Slug shown so A03 can replace this with a real detail view.
  return render_route_label(ctx, "Project: " + slug);
}

Route route_task_list(Ctx& ctx, const std::string& slug) {
  (void)slug;
  return render_route_label(ctx, "Tasks");
}

Route route_calendar(Ctx& ctx) {
  return render_route_label(ctx, "Calendar");
}

}  // namespace app
}  // namespace advdeck
