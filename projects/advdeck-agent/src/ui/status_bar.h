// src/ui/status_bar.h
//
// The 12-pixel-tall top strip that the user sees on every screen:
//   SD:ok | SD:NONE
//   00:00                (clock placeholder for Phase 1)
//
// The bar is part of the route draw contract: each route handler calls
// draw() once after clear() so the strip is consistent across screens.

#ifndef ADVDECK_SRC_UI_STATUS_BAR_H_
#define ADVDECK_SRC_UI_STATUS_BAR_H_

#include "platform/display.h"

namespace advdeck {
namespace ui {

class StatusBar {
 public:
  explicit StatusBar(platform::Display& display) : display_(display) {}

  // Paint the strip. `sd_ok` selects the SD:ok / SD:NONE label. The
  // clock is hard-coded to "00:00" for Phase 1; A12 will wire it to
  // the RTC / NTP once that is in place.
  void draw(bool sd_ok);

 private:
  platform::Display& display_;
};

}  // namespace ui
}  // namespace advdeck

#endif  // ADVDECK_SRC_UI_STATUS_BAR_H_
