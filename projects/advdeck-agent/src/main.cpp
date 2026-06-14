// src/main.cpp
//
// AdvDeck Agent — boot + main loop. Phase 1 scope:
//   1. Bring up M5Cardputer and the display.
//   2. Try to mount the SD card via the IStorage facade; the Phase 1
//      SdStorage stub returns false, which the status bar reports as
//      "SD:NONE". The UI must still be usable in that state — A03
//      can capture into RAM and flush on next mount.
//   3. Land on the home route. The main loop polls the keyboard and
//      dispatches the active route.
//
// The Cardputer's screen is 240x135. `M5Cardputer.begin(cfg, true)` is
// the canonical init that also brings up the keyboard / I2C expander.

#include <Arduino.h>
#include <M5Cardputer.h>

#include <cstdint>
#include <string>

#include "advdeck/project_store.h"
#include "advdeck/storage.h"
#include "app/routes.h"
#include "platform/display.h"
#include "platform/keyboard.h"
#include "ui/status_bar.h"

namespace {

advdeck::platform::Display& display() {
  static advdeck::platform::Display d;
  return d;
}

// Held in static storage so their lifetime covers the full program.
// SdStorage is a real, complete type when ADVDECK_FIRMWARE is defined;
// the Phase 1 impl returns errors from every method except trivial
// accessors (see src/platform/sd_storage.cpp). The compiler optimizes
// the empty host case away.
#ifdef ADVDECK_FIRMWARE
advdeck::SdStorage g_storage;
#else
struct NullStorage : advdeck::IStorage {
  bool mount() override { return false; }
  bool is_mounted() const override { return false; }
  bool exists(const std::string&) const override { return false; }
  std::string ensure_dir(const std::string&) override { return "host build"; }
  std::string write_file(const std::string&, const std::string&) override {
    return "host build";
  }
  std::string read_file(const std::string&) override { return ""; }
  std::string read_file_or(const std::string&, const std::string& f) override {
    return f;
  }
  std::vector<std::string> list_dir(const std::string&) override { return {}; }
  std::string join(const std::string& a, const std::string& b) override {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
  }
  std::string root() const override { return "/advdeck"; }
};
NullStorage g_storage;
#endif

advdeck::ProjectStore g_projects(g_storage, "/advdeck");

void draw_boot(bool sd_ok) {
  display().clear();
  advdeck::ui::StatusBar bar(display());
  bar.draw(sd_ok);
  display().text(4, 24, 0xFFFF, "AdvDeck Agent");
  display().text(4, 36, 0x07FF, sd_ok ? "booting..." : "booting (no SD)");
  display().push();
}

}  // namespace

void setup() {
  // Standard Cardputer init: cfg.enablePower() = false (Cardputer has
  // no power IC controlled by GPIO), enable buzzer = false, second
  // arg = true means "init display & I2C peripherals".
  auto cfg = M5.config();
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  M5Cardputer.begin(cfg, true);

  display().begin();
  const bool sd_ok = g_storage.mount();
  draw_boot(sd_ok);
  // Small delay so the boot frame is visible even on fast boards.
  delay(150);
}

void loop() {
// Tiny dispatcher. The home route drives the top-level menu and
// returns the next route. ProjectList and Capture are themselves
// blocking screens; we run them once per loop turn. Routes that
// need a slug (ProjectDetail, TaskList) read it from
// ctx.last_created_slug after a picker (route_project_list) or a
// creator (route_capture) wrote it there. We keep a sticky
// `current_slug` so route_project_detail -> TaskList -> ProjectList
// chains stay on the same project without forcing the user to
// re-pick.
advdeck::app::Ctx ctx{g_storage, g_projects, nullptr, nullptr, nullptr};
std::string current_slug;
advdeck::app::Route next = advdeck::app::Route::Home;
for (;;) {
  next = advdeck::app::route_home(ctx);
  if (next == advdeck::app::Route::Home) {
    // Esc on the home menu — exit the app loop.
    break;
  }
  while (next != advdeck::app::Route::Home) {
    switch (next) {
      case advdeck::app::Route::Capture:
        next = advdeck::app::route_capture(ctx);
        if (next == advdeck::app::Route::ProjectDetail) {
          current_slug = ctx.last_created_slug;
        }
        break;
      case advdeck::app::Route::ProjectList: {
        const advdeck::app::Route r = advdeck::app::route_project_list(ctx);
        if (r == advdeck::app::Route::ProjectDetail) {
          current_slug = ctx.last_created_slug;
        }
        next = r;
        break;
      }
      case advdeck::app::Route::ProjectDetail:
        if (!ctx.last_created_slug.empty()) {
          current_slug = ctx.last_created_slug;
        }
        next = advdeck::app::route_project_detail(ctx, current_slug);
        break;
      case advdeck::app::Route::TaskList:
        next = advdeck::app::route_task_list(ctx, current_slug);
        break;
      case advdeck::app::Route::Calendar:
        next = advdeck::app::route_calendar(ctx);
        break;
      case advdeck::app::Route::Home:
        next = advdeck::app::Route::Home;
        break;
    }
  }
  // Drop back to the top of the outer loop, which re-shows the home
  // menu.
}

// Yield to the watchdog / USB stack. 5 ms keeps the UI responsive
// (key repeat feels snappy) without spinning.
delay(5);
}
