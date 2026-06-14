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
  M5Cardputer.update();
  advdeck::app::Ctx ctx{
      /*storage=*/g_storage,
      /*projects=*/g_projects,
      /*tasks_for=*/nullptr,
      /*tasks_ctx=*/nullptr,
      /*calendar=*/nullptr,
  };

  // Phase 1: the home route drives the top-level menu and returns
  // the next route. The dispatcher then runs the chosen route, which
  // for now is a label + wait-for-key stub. A03..A05 will replace
  // these bodies; the dispatcher shape stays.
  advdeck::app::Route next = advdeck::app::route_home(ctx);
  (void)next;

  // Yield to the watchdog / USB stack. 5 ms keeps the UI responsive
  // (key repeat feels snappy) without spinning.
  delay(5);
}
