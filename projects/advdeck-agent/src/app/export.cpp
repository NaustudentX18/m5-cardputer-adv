// src/app/export.cpp
//
// B2.1 — Export trigger route. Per PHASE-3-INTERFACES.md §9.1, this
// route picks the current project (the slug passed in by the
// dispatcher) and calls AgentPackExporter::export_project. On
// success, shows the export path; on failure, shows the error.
//
// C1.2 owns the AgentPackExporter implementation. The stub shipped
// in this branch returns a clear error so the user sees the right
// feedback; the real C1.2 implementation will replace the stub.

#include "app/export.h"
#include "app/routes.h"

#include <cstdint>
#include <string>

#include "advdeck/agent_pack_exporter.h"
#include "advdeck/storage.h"
#include "platform/display.h"
#include "platform/keyboard.h"
#include "ui/status_bar.h"

namespace advdeck {
namespace app {

namespace {

platform::Display& disp() {
  static platform::Display d;
  return d;
}

constexpr int kLeft = 4;
constexpr int kRowH = 10;
constexpr int kHeaderY = 14;
constexpr int kListTop = 26;
constexpr int kFooterY = platform::Display::kHeight - kRowH;
constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kDim = 0x7BEF;
constexpr uint16_t kWarn = 0xF800;
constexpr uint16_t kOk = 0x07E0;

void show_message(const std::string& title, const std::string& body,
                  bool ok) {
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(false);
  disp().text(kLeft, kHeaderY, kFg, title);
  int row = 0;
  std::size_t i = 0;
  while (i < body.size() && row < 8) {
    std::size_t j = body.find('\n', i);
    if (j == std::string::npos) j = body.size();
    std::string line = body.substr(i, j - i);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    disp().text(kLeft, kListTop + row * kRowH,
                ok ? kOk : kWarn, line);
    ++row;
    i = j + 1;
  }
  disp().text(kLeft, kFooterY, kDim, "[any key] back");
  disp().push();
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (ev.any) return;
  }
}

}  // namespace

Route route_export_impl(Ctx& ctx, const std::string& current_slug) {
  if (current_slug.empty()) {
    show_message("Export", "no project selected", false);
    return Route::Home;
  }
  if (!ctx.exporter) {
    show_message("Export", "exporter not wired", false);
    return Route::Home;
  }
  std::string err;
  std::string out = ctx.exporter->export_project(
      current_slug, "dry-run", "phase-3", nullptr, &err);
  if (out.empty()) {
    show_message("Export failed", err, false);
    return Route::Home;
  }
  show_message("Export done", out, true);
  return Route::Home;
}

}  // namespace app
}  // namespace advdeck
