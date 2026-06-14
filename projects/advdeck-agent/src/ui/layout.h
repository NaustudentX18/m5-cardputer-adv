// include/advdeck/ui/layout.h
//
// A12 UX polish: shared layout constants for the 240x135 Cardputer
// screen, plus the host-testable scroll-indicator helper.
//
// The top 12 px is reserved for the status bar, the bottom 12 px
// for the footer, and the 108 px in between is the content area.
// Routes draw their header at y = 14 (just below the status bar)
// and their footer at y = 122 (just above the 12-px footer strip).
//
// All routes share these constants so a "where does the scroll
// indicator go?" decision lives in one place. The scroll bar is
// 4 px wide and lives on the right edge of the content area.

#ifndef ADVDECK_INCLUDE_ADVDECK_UI_LAYOUT_H_
#define ADVDECK_INCLUDE_ADVDECK_UI_LAYOUT_H_

#include <string>

namespace advdeck {
namespace ui {

// Status bar height, in pixels.
constexpr int kTopBarHeight = 12;

// Footer height, in pixels.
constexpr int kFooterHeight = 12;

// First content y (just below the status bar).
constexpr int kContentTop = 14;

// Last content y (just above the 12-px footer). Routes that draw a
// footer caption put their text at this y.
constexpr int kContentBottom = 122;

// Width of the right-edge vertical scroll indicator, in pixels.
constexpr int kScrollBarWidth = 4;

// Build the on-screen content of the right-edge scroll indicator
// as a multi-line string. The host test asserts on substrings of
// this; the firmware can either use it directly (M5GFX drawString)
// or render the marker list itself.
//
//   content_height: height of the viewport (px).
//   total_height:   total content height (px).
//   scroll_y:       how far the viewport has scrolled (px).
//
// The returned string has 8 lines. The filled fraction is
// min(1.0, (total_height - content_height) > 0 ? scroll_y /
// (total_height - content_height) : 0). When total_height is less
// than or equal to content_height the function returns "" — the
// viewport can show everything, no scroll bar needed.
std::string render_scroll_indicator(int content_height,
                                    int total_height,
                                    int scroll_y);

}  // namespace ui
}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_UI_LAYOUT_H_
