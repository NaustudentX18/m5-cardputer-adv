// src/ui/status_bar.cpp
//
// 12-px-tall strip, the only "chrome" in Phase 1. A12 adds the
// current time ("HH:MM") to the right side, a battery / bridge
// status placeholder, and the SD:ok / SD:NONE marker on the left.
//
// Time is local time derived from UTC + a hardcoded offset
// (AEST = UTC+10 for the Brisbane deployment). A12.5 will read
// the offset from config.json so a traveler can re-skin the
// device without recompiling.

#include "ui/status_bar.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace advdeck {
namespace ui {

namespace {

constexpr int kBarHeight = 12;
constexpr uint16_t kFg = 0xFFFF;     // white
constexpr uint16_t kWarn = 0xF800;   // red, for SD:NONE
constexpr uint16_t kDim = 0x7BEF;    // grey, for "no bridge"

// UTC offset in hours. The Cardputer-Adv's RTC is set per
// deployment via the bridge's first sync. A12.5 swaps this for
// a per-deployment config.json value.
constexpr int kUtcOffsetHours = 10;  // AEST (Brisbane, no DST)

uint16_t battery_pct_millivolts(int mv) {
  // A12 placeholder: the real M5Cardputer.Axp2101 driver would
  // own this. We expose a fake range so the status bar shows
  // something on host tests.
  (void)mv;
  return 0;
}

// Local-time "HH:MM" derived from UTC time() + a hardcoded
// offset. The host test (test_ui_layout) does not depend on
// the value; it just checks that the draw() call doesn't crash
// and that the result is a 5-char string. Production wiring
// replaces this with an NTP-fed clock in a later phase.
std::string local_hhmm() {
  std::time_t t = std::time(nullptr);
  // Shift by the UTC offset. Wall time is computed in UTC so we
  // avoid DST hazards; the offset is a fixed constant.
  t += static_cast<std::time_t>(kUtcOffsetHours) * 3600;
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[8];
  std::strftime(buf, sizeof(buf), "%H:%M", &tm);
  return std::string(buf);
}

}  // namespace

void StatusBar::draw(bool sd_ok) {
  display_.rect(0, 0, platform::Display::kWidth, kBarHeight, kFg);
  const uint16_t label_color = sd_ok ? kFg : kWarn;
  display_.text(2, 2, label_color, sd_ok ? "SD:ok" : "SD:NONE");
  // Battery placeholder on the left half — replaced by the
  // AXP2101 driver in a follow-up. We keep the slot reserved
  // so the right edge of the bar doesn't drift.
  display_.text(40, 2, kDim, "B--");
  // "BR" or "BR:--" depending on whether the bridge is
  // reachable. Phase 1 always renders "BR:--" because the
  // host build has no Wi-Fi.
  display_.text(70, 2, kDim, "BR:..");
  // Local time on the right.
  display_.text(platform::Display::kWidth - 28, 2, kFg, local_hhmm());
  (void)battery_pct_millivolts;
}

}  // namespace ui
}  // namespace advdeck
