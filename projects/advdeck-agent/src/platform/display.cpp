// src/platform/display.cpp
//
// Display facade impl. The Phase 1 build creates a heap-allocated
// LGFX_Sprite back buffer in begin() and pushes it to the panel in
// push(). The host (no ADVDECK_FIRMWARE) build is a no-op so unit
// tests / SDL builds can substitute a fake without pulling in M5GFX.

#include "platform/display.h"

#ifdef ADVDECK_FIRMWARE
#include <M5Cardputer.h>
#endif

namespace advdeck {
namespace platform {

Display::Display() = default;
Display::~Display() {
#ifdef ADVDECK_FIRMWARE
  delete sprite_;
#endif
}

void Display::begin() {
#ifdef ADVDECK_FIRMWARE
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextWrap(false);
  if (sprite_ == nullptr) {
    sprite_ = new LGFX_Sprite(&M5Cardputer.Display);
    sprite_->setColorDepth(16);
    sprite_->createSprite(M5Cardputer.Display.width(),
                          M5Cardputer.Display.height());
  }
#endif
}

void Display::clear() {
#ifdef ADVDECK_FIRMWARE
  if (sprite_ == nullptr) return;
  sprite_->fillScreen(BLACK);
#endif
}

void Display::text(int x, int y, uint16_t color, const std::string& s) {
#ifdef ADVDECK_FIRMWARE
  if (sprite_ == nullptr) return;
  sprite_->setTextColor(color);
  sprite_->setCursor(x, y);
  sprite_->print(s.c_str());
#endif
}

void Display::rect(int x, int y, int w, int h, uint16_t color) {
#ifdef ADVDECK_FIRMWARE
  if (sprite_ == nullptr || w <= 0 || h <= 0) return;
  sprite_->drawRect(x, y, w, h, color);
#endif
}

void Display::push() {
#ifdef ADVDECK_FIRMWARE
  if (sprite_ == nullptr) return;
  sprite_->pushSprite(0, 0);
#endif
}

}  // namespace platform
}  // namespace advdeck
