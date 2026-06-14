// src/app/sync.cpp
//
// B2.1 — Sync status UI. Per PHASE-3-INTERFACES.md §9.1, the screen
// shows pending / in_flight / done / errored counts, the last 5 done
// rows, the pending + errored rows, and binds:
//   r — retry the highlighted errored row (calls OutboxQueue::retry)
//   c — compact done rows whose result dir mtime is older than 7 days
//   Esc — back to home
//
// The route is split into two pieces: render_sync_screen() returns
// the screen content as a string for host tests, and the route body
// drives the key loop and redraws. This pattern matches the other
// routes in this app: A04, A05, A03 all have a *_impl that owns the
// key loop.

#include "app/sync.h"
#include "app/routes.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "advdeck/outbox_queue.h"
#include "platform/display.h"
#include "platform/keyboard.h"
#include "ui/status_bar.h"

namespace advdeck {
namespace app {

namespace {

platform::Display& disp() {
  static platform::Display d;
  return d;
}

constexpr int kHeaderY = 14;
constexpr int kRowH = 10;
constexpr int kFooterY = platform::Display::kHeight - kRowH;
constexpr int kListTop = 26;
constexpr int kLeft = 4;
constexpr int kMaxRows =
    (platform::Display::kHeight - kListTop - kRowH) / kRowH;
constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kSel = 0x07E0;
constexpr uint16_t kDim = 0x7BEF;
constexpr uint16_t kWarn = 0xF800;  // red for errored rows

// Number of days the sync UI's 'c' compact uses. Mirrors the
// header comment on OutboxQueue::compact_done: 7 days is the
// typical "I've moved on" lifetime for a finished request.
constexpr int kCompactDays = 7;

std::string clip(const std::string& s, int max_chars) {
  if (static_cast<int>(s.size()) <= max_chars) return s;
  return s.substr(0, max_chars);
}

// Visible row in the sync list. We project the PendingRequest into
// a flat string for rendering. errored rows are highlighted in
// red; everything else in default white.
struct VisibleRow {
  std::string id;
  std::string project;
  std::string created_at;
  std::string status;       // "pending" | "in_flight" | "done" | "error"
  bool errored() const { return status == "error"; }
};

// Build the list of rows the screen shows: the most recent 5 done
// rows first, then pending, then errored. We sort each group by
// created_at descending so the freshest row is on top.
std::vector<VisibleRow> build_rows(
    const std::vector<PendingRequest>& rows) {
  std::vector<VisibleRow> out;
  out.reserve(rows.size());
  for (const auto& r : rows) {
    VisibleRow v;
    v.id = r.id;
    v.project = r.project;
    v.created_at = r.created_at;
    v.status = r.status;
    out.push_back(std::move(v));
  }
  // Stable sort: errored first, then pending, then in_flight, then
  // done. Within each group, newer created_at first. We do it in
  // two passes for clarity.
  auto by_time_desc = [](const VisibleRow& a, const VisibleRow& b) {
    return a.created_at > b.created_at;
  };
  std::vector<VisibleRow> errored, pending, in_flight, done;
  for (auto& v : out) {
    if (v.status == "error") errored.push_back(std::move(v));
    else if (v.status == "pending") pending.push_back(std::move(v));
    else if (v.status == "in_flight") in_flight.push_back(std::move(v));
    else if (v.status == "done") done.push_back(std::move(v));
  }
  std::sort(errored.begin(), errored.end(), by_time_desc);
  std::sort(pending.begin(), pending.end(), by_time_desc);
  std::sort(in_flight.begin(), in_flight.end(), by_time_desc);
  // "Last 5 done" is the 5 most recent — sort by time desc, take
  // the head.
  std::sort(done.begin(), done.end(), by_time_desc);
  if (done.size() > 5) done.resize(5);
  std::vector<VisibleRow> ordered;
  ordered.reserve(errored.size() + pending.size() + in_flight.size() +
                  done.size());
  for (auto& v : errored) ordered.push_back(std::move(v));
  for (auto& v : pending) ordered.push_back(std::move(v));
  for (auto& v : in_flight) ordered.push_back(std::move(v));
  for (auto& v : done) ordered.push_back(std::move(v));
  return ordered;
}

void draw_screen(Ctx& ctx, const std::vector<VisibleRow>& rows,
                 int sel, const SyncCounts& counts, const char* footer_note) {
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(ctx.storage.is_mounted());
  char header[64];
  std::snprintf(header, sizeof(header),
                "Sync  P:%d I:%d D:%d E:%d",
                counts.pending, counts.in_flight, counts.done,
                counts.errored);
  disp().text(kLeft, kHeaderY, kFg, header);
  if (rows.empty()) {
    disp().text(kLeft, kListTop, kDim, "(no outbox rows)");
  } else {
    const int visible = std::min<int>(kMaxRows, static_cast<int>(rows.size()));
    const int first = std::max(0, sel - visible + 1);
    for (int i = 0; i < visible; ++i) {
      const VisibleRow& r = rows[first + i];
      const int y = kListTop + i * kRowH;
      char line[96];
      std::snprintf(line, sizeof(line), "%-7s %s  %s",
                    r.status.c_str(), r.id.c_str(),
                    clip(r.project, 16).c_str());
      const bool is_sel = (first + i) == sel;
      const uint16_t color = is_sel ? kSel
                                     : (r.errored() ? kWarn : kFg);
      disp().text(kLeft, y, color, line);
    }
  }
  std::string footer = "[r] retry  [c] compact  [Esc] back";
  if (footer_note && *footer_note) {
    footer += "  ";
    footer += footer_note;
  }
  disp().text(kLeft, kFooterY, kDim, footer);
  disp().push();
}

}  // namespace

SyncCounts compute_sync_counts(
    const std::vector<PendingRequest>& rows) {
  SyncCounts c;
  for (const auto& r : rows) {
    if (r.status == "pending") ++c.pending;
    else if (r.status == "in_flight") ++c.in_flight;
    else if (r.status == "done") ++c.done;
    else if (r.status == "error") ++c.errored;
  }
  return c;
}

std::string render_sync_screen(Ctx& ctx) {
  // The screen is host-testable. We build a string of "\n"
  // separated rows. The route body never reads this string; it
  // draws via the same logic. The host test asserts on the
  // count summary line and on the row ordering, which are what
  // the UI cares about.
  if (!ctx.outbox) {
    return "header=Sync counts=P:0 I:0 D:0 E:0\n"
           "rows=(no outbox)\n";
  }
  std::vector<PendingRequest> rows;
  std::string err;
  std::string r = ctx.outbox->load_all(&rows, &err);
  if (!r.empty()) {
    // load_all returned an error; surface as one row.
    return "header=Sync error=" + r + "\n";
  }
  SyncCounts c = compute_sync_counts(rows);
  std::vector<VisibleRow> visible = build_rows(rows);
  char header[80];
  std::snprintf(header, sizeof(header),
                "header=Sync counts=P:%d I:%d D:%d E:%d",
                c.pending, c.in_flight, c.done, c.errored);
  std::string out;
  out += header;
  out += '\n';
  if (visible.empty()) {
    out += "rows=(none)\n";
    return out;
  }
  for (const auto& v : visible) {
    char line[96];
    std::snprintf(line, sizeof(line), "row=%s\t%s\t%s\t%s",
                  v.status.c_str(), v.id.c_str(), v.created_at.c_str(),
                  v.project.c_str());
    out += line;
    out += '\n';
  }
  return out;
}

Route route_sync_impl(Ctx& ctx) {
  if (!ctx.outbox) {
    // No outbox wired in. Show a stub and return.
    disp().begin();
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(false);
    disp().text(kLeft, 24, 0xFFFF, "Sync (no outbox)");
    disp().text(kLeft, kFooterY, kDim, "[Esc] back");
    disp().push();
    for (;;) {
      const platform::KeyEvent ev = platform::poll();
      if (ev.any) return Route::Home;
    }
  }

  auto load_visible = [&](std::vector<PendingRequest>* rows,
                          std::vector<VisibleRow>* visible,
                          SyncCounts* counts) {
    std::string err;
    ctx.outbox->load_all(rows, &err);
    *counts = compute_sync_counts(*rows);
    *visible = build_rows(*rows);
  };

  std::vector<PendingRequest> rows;
  std::vector<VisibleRow> visible;
  SyncCounts counts;
  load_visible(&rows, &visible, &counts);

  int sel = 0;
  if (sel >= static_cast<int>(visible.size())) sel = 0;
  // Default the selection to the first errored row, so 'r' has
  // something to act on without forcing the user to navigate.
  for (std::size_t i = 0; i < visible.size(); ++i) {
    if (visible[i].errored()) {
      sel = static_cast<int>(i);
      break;
    }
  }
  std::string footer_note;
  draw_screen(ctx, visible, sel, counts, footer_note.c_str());

  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) return Route::Home;
    if (ev.c == 'j' || ev.c == ';' || ev.c == '>') {
      if (sel < static_cast<int>(visible.size()) - 1) {
        ++sel;
        draw_screen(ctx, visible, sel, counts, footer_note.c_str());
      }
      continue;
    }
    if (ev.c == 'k' || ev.c == '.' || ev.c == '<') {
      if (sel > 0) {
        --sel;
        draw_screen(ctx, visible, sel, counts, footer_note.c_str());
      }
      continue;
    }
    if (ev.c == 'r' || ev.c == 'R') {
      if (sel < 0 || sel >= static_cast<int>(visible.size())) continue;
      if (!visible[sel].errored()) {
        footer_note = "row is not errored";
        draw_screen(ctx, visible, sel, counts, footer_note.c_str());
        continue;
      }
      std::string e;
      std::string r = ctx.outbox->retry(visible[sel].id, &e);
      if (!r.empty()) {
        footer_note = e;
      } else {
        footer_note = "retried " + visible[sel].id;
      }
      // Reload so the user sees the row flip out of errored.
      load_visible(&rows, &visible, &counts);
      if (sel >= static_cast<int>(visible.size()))
        sel = static_cast<int>(visible.size()) - 1;
      if (sel < 0) sel = 0;
      draw_screen(ctx, visible, sel, counts, footer_note.c_str());
      continue;
    }
    if (ev.c == 'c' || ev.c == 'C') {
      int removed = 0;
      std::string e;
      std::string r = ctx.outbox->compact_done(kCompactDays, &removed, &e);
      if (!r.empty()) {
        footer_note = "compact: " + r;
      } else {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "compacted %d", removed);
        footer_note = buf;
      }
      load_visible(&rows, &visible, &counts);
      if (sel >= static_cast<int>(visible.size()))
        sel = static_cast<int>(visible.size()) - 1;
      if (sel < 0) sel = 0;
      draw_screen(ctx, visible, sel, counts, footer_note.c_str());
      continue;
    }
  }
}

}  // namespace app
}  // namespace advdeck
