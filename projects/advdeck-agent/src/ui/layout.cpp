// src/ui/layout.cpp
//
// A12 UX polish: scroll-indicator helper. Pure string-out so the
// host test can drive it. The on-screen rendering can use the
// string as a marker list ('|' for filled, '.' for empty) and
// translate each char to a 1-px-wide column. The marker string
// has 8 lines (== a "tall enough to see" indicator on a 108-px
// content area when the row height is roughly 14 px).
//
// The fraction is `scroll_y / (total_height - content_height)`
// when the content is taller than the viewport; we round to the
// nearest cell. At the top the first cell is filled; at the
// bottom the last cell is filled. In between, the filled region
// grows monotonically.

#include "ui/layout.h"

#include <cstddef>
#include <string>

namespace advdeck {
namespace ui {

namespace {

constexpr int kIndicatorRows = 8;

}  // namespace

std::string render_scroll_indicator(int content_height,
                                    int total_height,
                                    int scroll_y) {
  // Viewport fits the content; no bar needed.
  if (total_height <= content_height) return "";
  // Defensive: clamp to sane ranges.
  if (content_height < 1) content_height = 1;
  if (total_height < 1) total_height = 1;
  if (scroll_y < 0) scroll_y = 0;
  const int max_scroll = total_height - content_height;
  if (scroll_y > max_scroll) scroll_y = max_scroll;

  // Filled cells: the bar grows from 1 cell at scroll_y=0 to
  // kIndicatorRows at scroll_y=max_scroll. Using at least 1 cell
  // ensures the top marker is visible; using exactly
  // kIndicatorRows at the bottom ensures the bottom marker is
  // visible. Integer math keeps the function deterministic.
  int filled;
  if (max_scroll == 0) {
    filled = kIndicatorRows;
  } else {
    // Round to nearest cell.
    filled = (scroll_y * kIndicatorRows + max_scroll / 2) / max_scroll;
    if (scroll_y == 0) filled = 1;
    if (scroll_y == max_scroll) filled = kIndicatorRows;
    if (filled < 1) filled = 1;
    if (filled > kIndicatorRows) filled = kIndicatorRows;
  }

  // 8 lines, each line is a single '|' or '.' followed by a
  // newline. Callers can split('\n') to get the row markers.
  std::string out;
  out.reserve(kIndicatorRows * 2);
  for (int i = 0; i < kIndicatorRows; ++i) {
    out.push_back(i < filled ? '|' : '.');
    out.push_back('\n');
  }
  return out;
}

}  // namespace ui
}  // namespace advdeck
