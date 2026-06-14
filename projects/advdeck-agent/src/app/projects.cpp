// src/app/projects.cpp
//
// The project list is a scrollable table of all projects sorted by
// modified_at descending, with slug as a tie-breaker. The detail view
// is read-only by default but supports an in-place edit (e-key) and a
// jump to the task list (t-key) and back (esc).
//
// All M5 / display calls are routed through platform::Display, which
// is a no-op on host. The buffer-mutating parts (sort, slug pick,
// edit) are pure C++ and would be straightforward to host-test; the
// only test we ship for now covers the text editor.

#include "app/projects.h"
#include "app/routes.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "advdeck/project_store.h"
#include "advdeck/storage.h"
#include "platform/display.h"
#include "platform/keyboard.h"
#include "ui/status_bar.h"
#include "ui/text_editor.h"

namespace advdeck {
namespace app {

namespace {

platform::Display& disp() {
  static platform::Display d;
  return d;
}

constexpr int kHeaderY = 14;            // below the 12-px status bar
constexpr int kListTop = 26;
constexpr int kRowH = 10;
constexpr int kLeft = 4;
constexpr int kFooterY = platform::Display::kHeight - kRowH;
constexpr int kVisibleRows =
    (platform::Display::kHeight - kListTop - kRowH) / kRowH;
constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kSel = 0x07E0;
constexpr uint16_t kDim = 0x7BEF;

// Project rows are roughly "<title>  YYYY-MM-DD". Truncate the title
// to fit; the date stamp is always 10 chars + a leading space.
std::string clip(const std::string& s, int max_chars) {
  if (max_chars <= 1) return "~";
  if (static_cast<int>(s.size()) <= max_chars) return s;
  return s.substr(0, max_chars - 1) + "~";
}

// The list uses a "modified_at desc, then slug asc" sort. Empty
// timestamps sort last (treat as "ancient"); A12 will revisit.
bool list_order(const ProjectSummary& a, const ProjectSummary& b) {
  if (a.modified_at != b.modified_at) {
    return a.modified_at > b.modified_at;
  }
  return a.slug < b.slug;
}

void draw_list(const std::vector<ProjectSummary>& items, int sel) {
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(true);
  disp().text(kLeft, kHeaderY, kFg, "Projects");
  if (items.empty()) {
    disp().text(kLeft, kListTop, kDim, "(no projects)");
    disp().text(kLeft, kListTop + kRowH, kDim, "press 'n' to capture one");
  } else {
    for (int i = 0; i < kVisibleRows; ++i) {
      const int idx = sel - (kVisibleRows - 1) + i;
      if (idx < 0 || idx >= static_cast<int>(items.size())) continue;
      const ProjectSummary& p = items[idx];
      const int y = kListTop + i * kRowH;
      if (idx == sel) {
        disp().rect(0, y - 1, platform::Display::kWidth, kRowH, kSel);
      }
      std::string date =
          p.modified_at.size() >= 10 ? p.modified_at.substr(0, 10) : "";
      const int max_title =
          (platform::Display::kWidth - kLeft - 12 /*"  YYYY-MM-DD"*/) / 6;
      const std::string title = clip(p.title.empty() ? p.slug : p.title,
                                     max_title);
      const std::string line = title + "  " + date;
      disp().text(kLeft, y, kFg, line);
    }
  }
  disp().text(kLeft, kFooterY, kDim, "[n] new  [esc] back");
  disp().push();
}

}  // namespace

Route route_project_list_impl(Ctx& ctx, std::string* out_slug) {
  if (out_slug) out_slug->clear();

  std::string err;
  std::vector<ProjectSummary> items = ctx.projects.list_projects(&err);
  // Phase 1: a list_projects error is non-fatal — show what we have
  // and let the user try again. A12 will surface the error in the
  // status bar.
  std::sort(items.begin(), items.end(), list_order);

  int sel = 0;
  if (!items.empty() && sel >= static_cast<int>(items.size())) sel = 0;

  draw_list(items, sel);
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) return Route::Home;
    if (ev.c == 'n' || ev.c == 'N') return Route::Capture;
    if (!items.empty()) {
      if (ev.enter) {
        if (out_slug) *out_slug = items[sel].slug;
        return Route::ProjectDetail;
      }
      if (ev.c == ';' || ev.c == 'j' || ev.c == '>') {
        if (sel < static_cast<int>(items.size()) - 1) {
          ++sel;
          draw_list(items, sel);
        }
        continue;
      }
      if (ev.c == '.' || ev.c == 'k' || ev.c == '<') {
        if (sel > 0) {
          --sel;
          draw_list(items, sel);
        }
        continue;
      }
    }
  }
}

namespace {

void draw_detail(const std::string& slug, const std::string& title,
                 const std::string& idea, int scroll_y) {
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(true);
  disp().text(kLeft, kHeaderY, kFg, clip(title, 38));
  // Render a slice of idea lines starting at scroll_y. Each line is
  // clipped to the panel width.
  int row = 0;
  std::size_t i = 0;
  while (i < idea.size() && row < scroll_y) {
    std::size_t j = idea.find('\n', i);
    if (j == std::string::npos) j = idea.size();
    ++row;
    i = j + 1;
  }
  const int max_chars = (platform::Display::kWidth - kLeft) / 6;
  int drawn = 0;
  while (i < idea.size() && drawn < 8) {
    std::size_t j = idea.find('\n', i);
    if (j == std::string::npos) j = idea.size();
    std::string line = idea.substr(i, j - i);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const int y = kListTop + drawn * kRowH;
    disp().text(kLeft, y, kFg, clip(line, max_chars));
    ++drawn;
    i = j + 1;
  }
  if (drawn == 0) {
    disp().text(kLeft, kListTop, kDim, "(no idea.md yet)");
    disp().text(kLeft, kListTop + kRowH, kDim, "press 'e' to write one");
  }
  disp().text(kLeft, kFooterY, kDim,
              "[e] edit  [t] tasks  [;.] scroll  [esc] back");
  disp().push();
}

}  // namespace

Route route_project_detail_impl(Ctx& ctx, const std::string& slug) {
  std::string err;
  std::string idea = ctx.projects.read_idea(slug, &err);
  std::string title;
  {
    std::vector<ProjectSummary> list = ctx.projects.list_projects(&err);
    for (const auto& p : list) {
      if (p.slug == slug) {
        title = p.title;
        break;
      }
    }
    if (title.empty()) title = slug;
  }
  int scroll_y = 0;
  draw_detail(slug, title, idea, scroll_y);
  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) return Route::ProjectList;
    if (ev.c == 't' || ev.c == 'T') return Route::TaskList;
    if (ev.c == 'e' || ev.c == 'E') {
      // Edit mode. Pre-fill with the current idea text.
      ui::TextEditor editor(idea, "Edit idea (" + slug + "):");
      ui::EditorResult r = editor.run();
      if (r.confirmed) {
        std::string werr;
        ctx.projects.write_idea(slug, r.text, &werr);
        if (!werr.empty()) {
          // Surface the error in the title row for one frame.
          disp().clear();
          ui::StatusBar bar(disp());
          bar.draw(false);
          disp().text(kLeft, kHeaderY, 0xF800, "Write failed:");
          disp().text(kLeft, kListTop, 0xFFFF, werr);
          disp().text(kLeft, kFooterY, kDim, "[any key] back to idea");
          disp().push();
          for (;;) {
            const platform::KeyEvent e2 = platform::poll();
            if (e2.any) break;
          }
        }
        idea = r.text;
        scroll_y = 0;
        draw_detail(slug, title, idea, scroll_y);
      }
      continue;
    }
    // Scroll within the idea text.
    if (ev.c == ';' || ev.c == 'j' || ev.c == '>') {
      ++scroll_y;
      draw_detail(slug, title, idea, scroll_y);
      continue;
    }
    if (ev.c == '.' || ev.c == 'k' || ev.c == '<') {
      if (scroll_y > 0) {
        --scroll_y;
        draw_detail(slug, title, idea, scroll_y);
      }
      continue;
    }
  }
}

}  // namespace app
}  // namespace advdeck
