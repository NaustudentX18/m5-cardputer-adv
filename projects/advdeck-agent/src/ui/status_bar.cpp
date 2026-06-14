// src/ui/status_bar.cpp
//
// 12-px-tall strip, the only "chrome" in Phase 1. Routes assume the
// top 12 pixels are reserved; content is drawn at y >= 14.

#include "ui/status_bar.h"

#include <cstdint>

namespace advdeck {
namespace ui {

namespace {
constexpr int kBarHeight = 12;
constexpr uint16_t kBg = 0x0000;  // black, reserved
constexpr uint16_t kFg = 0xFFFF;  // white
constexpr uint16_t kWarn = 0xF800;  // red, for SD:NONE
}  // namespace

void StatusBar::draw(bool sd_ok) {
  (void)kBg;  // reserved for A12 (filled background)
  display_.rect(0, 0, platform::Display::kWidth, kBarHeight, kFg);
  const uint16_t label_color = sd_ok ? kFg : kWarn;
  display_.text(2, 2, label_color, sd_ok ? "SD:ok" : "SD:NONE");
  display_.text(platform::Display::kWidth - 28, 2, kFg, "00:00");
}

}  // namespace ui
}  // namespace advdeck
