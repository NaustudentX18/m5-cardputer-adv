// src/platform/display.h
//
// Phase 1 display facade. The Cardputer's screen is small (240x135) and
// direct M5GFX calls are verbose; this wrapper exposes the small set of
// primitives the UI / status bar / menu actually need.
//
// On firmware we keep a heap-allocated LGFX_Sprite as a back buffer so
// frames are atomic. The sprite is created in begin() and destroyed in
// the destructor. On host (no ADVDECK_FIRMWARE) the class is a no-op so
// host tests can link without pulling in M5GFX.

#ifndef ADVDECK_SRC_PLATFORM_DISPLAY_H_
#define ADVDECK_SRC_PLATFORM_DISPLAY_H_

#include <cstdint>
#include <string>

#ifdef ADVDECK_FIRMWARE
#include <M5GFX.h>
#endif

namespace advdeck {
namespace platform {

class Display {
 public:
  // Width / height in current rotation. The Cardputer boots in
  // landscape 240x135 with ROTATION=1; we re-read on begin().
  static constexpr int kWidth = 240;
  static constexpr int kHeight = 135;

  Display();
  ~Display();

  // Bind to the M5Cardputer canvas. Safe to call once after M5.begin().
  void begin();

  // Wipe the back buffer to black.
  void clear();

  // Draw a 1x-scaled text string at (x, y) in the given 16-bit RGB565
  // color. Coordinates are in pixels from the top-left.
  void text(int x, int y, uint16_t color, const std::string& s);

  // Draw a 1-px outlined rectangle (no fill). Coordinates are inclusive
  // for the perimeter; out-of-bounds coordinates are clipped, not
  // rejected.
  void rect(int x, int y, int w, int h, uint16_t color);

  // Push the back buffer to the panel. Call once per frame.
  void push();

 private:
#ifdef ADVDECK_FIRMWARE
  LGFX_Sprite* sprite_ = nullptr;
#endif
};

}  // namespace platform
}  // namespace advdeck

#endif  // ADVDECK_SRC_PLATFORM_DISPLAY_H_
