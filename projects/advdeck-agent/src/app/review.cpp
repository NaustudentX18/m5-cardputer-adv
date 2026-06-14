// src/app/review.cpp
//
// B3.1 — Review screen for bridge-produced artifacts. See
// PHASE-3-INTERFACES.md §9.3 for the on-screen layout and key
// bindings. See review.h for the per-function contract.
//
// The screen drives:
//   - render_review_summary: returns a multi-line string the
//     host tests can assert on. The on-screen body is built from
//     the same string the route draws.
//   - render_recent_staging: returns a string listing the most
//     recent pending entries, used by the home "Review" pick.
//   - route_review_impl: the blocking screen. Shows the summary
//     first, then enters a sub-mode for e / t / c / a that
//     pretty-prints the corresponding artifact read-only. Enter
//     accepts (calls StagingQueue::accept) and Esc rejects
//     (calls StagingQueue::reject). Either way returns Home.

#include "app/review.h"
#include "app/routes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "advdeck/project_store.h"
#include "advdeck/staging_queue.h"
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

constexpr int kLeft = 4;
constexpr int kRowH = 10;
constexpr int kHeaderY = 14;
constexpr int kListTop = 26;
constexpr int kFooterY = platform::Display::kHeight - kRowH;
constexpr uint16_t kFg = 0xFFFF;
constexpr uint16_t kSel = 0x07E0;
constexpr uint16_t kDim = 0x7BEF;
constexpr uint16_t kOk = 0x07E0;
constexpr uint16_t kWarn = 0xF800;

// Per the spec: first 5 brief lines, first 3 task titles.
constexpr int kBriefPreviewLines = 5;
constexpr int kTaskPreviewTitles = 3;

// Read a file via the IStorage facade. Returns "" on missing.
std::string read_text(IStorage& storage, const std::string& path) {
  if (path.empty()) return "";
  return storage.read_file(path);
}

// Pull the first `max_lines` non-empty lines out of a UTF-8 / LF
// text blob. We don't try to wrap or trim; the host tests assert
// on substring matches, and the on-screen renderer trims further.
std::vector<std::string> head_lines(const std::string& body,
                                    int max_lines) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < body.size() && static_cast<int>(out.size()) < max_lines) {
    std::size_t j = body.find('\n', i);
    if (j == std::string::npos) j = body.size();
    std::string line = body.substr(i, j - i);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    out.push_back(std::move(line));
    i = j + 1;
  }
  return out;
}

// Pull the first line of a text blob.
std::string first_line(const std::string& body) {
  if (body.empty()) return "";
  std::size_t j = body.find('\n');
  if (j == std::string::npos) j = body.size();
  std::string line = body.substr(0, j);
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return line;
}

// Pretty-print a tasks.json / calendar-suggestions.json body so
// the on-screen view is readable. We accept any well-formed JSON;
// if parsing fails we fall back to the raw text.
std::string format_json_pretty(const std::string& body) {
  if (body.empty()) return "";
  try {
    auto j = nlohmann::json::parse(body);
    return j.dump(2) + "\n";
  } catch (...) {
    return body;
  }
}

// Pull the project title for a slug out of the ProjectStore, or
// fall back to the slug if the project is missing. Mirrors the
// way other routes look up the project label.
std::string project_title_for(Ctx& ctx, const std::string& slug) {
  std::string err;
  std::vector<ProjectSummary> list = ctx.projects.list_projects(&err);
  for (const auto& p : list) {
    if (p.slug == slug) return p.title;
  }
  return slug;
}

// Compose the summary string. Layout (each line ends with \n):
//   header=Review request=<id> project=<slug> title=<title>
//   brief:<line>     (up to 5 lines)
//   brief:(missing)  (if brief.md is missing)
//   Tasks: <N> total
//   task:<title>     (first 3 only)
//   calendar: <N>
//   agent-prompt:<first line>  or  agent-prompt: (missing)
//
// The string is empty when the request is unknown. This is the
// single source of truth — the route body draws the same content.
std::string build_summary(Ctx& ctx, const StagingEntry& entry) {
  if (!ctx.staging) return "";
  std::string staging_dir = ctx.storage.join(
      ctx.staging->staging_dir(), entry.request_id);
  if (!ctx.storage.exists(staging_dir)) return "";

  std::string out;
  char header[160];
  std::snprintf(header, sizeof(header),
                "header=Review request=%s project=%s title=%s",
                entry.request_id.c_str(), entry.project.c_str(),
                project_title_for(ctx, entry.project).c_str());
  out += header;
  out += '\n';

  // Brief: first 5 lines.
  std::string brief = read_text(
      ctx.storage, ctx.storage.join(staging_dir, "brief.md"));
  if (brief.empty()) {
    out += "brief:(missing)\n";
  } else {
    for (const auto& line : head_lines(brief, kBriefPreviewLines)) {
      out += "brief:";
      out += line;
      out += '\n';
    }
  }

  // Task count + first 3 titles. Accepts either
  // { "tasks": [ { "title": "..." } ] } or a top-level array. The
  // first form matches the bridge's result-manifest contract.
  std::string tasks_body = read_text(
      ctx.storage, ctx.storage.join(staging_dir, "tasks.json"));
  if (tasks_body.empty()) {
    out += "Tasks: 0 total\n";
  } else {
    try {
      auto j = nlohmann::json::parse(tasks_body);
      const nlohmann::json* arr = nullptr;
      if (j.is_object() && j.contains("tasks") && j["tasks"].is_array()) {
        arr = &j["tasks"];
      } else if (j.is_array()) {
        arr = &j;
      }
      if (!arr) {
        out += "Tasks: ? (unrecognized shape)\n";
      } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Tasks: %zu total",
                      arr->size());
        out += buf;
        out += '\n';
        const std::size_t take = std::min<std::size_t>(
            arr->size(), static_cast<std::size_t>(kTaskPreviewTitles));
        for (std::size_t i = 0; i < take; ++i) {
          std::string title;
          const auto& el = (*arr)[i];
          if (el.is_object() && el.contains("title") &&
              el["title"].is_string()) {
            title = el["title"].get<std::string>();
          } else if (el.is_string()) {
            title = el.get<std::string>();
          } else {
            title = "(untitled)";
          }
          out += "task:";
          out += title;
          out += '\n';
        }
      }
    } catch (...) {
      out += "Tasks: ? (unparseable)\n";
    }
  }

  // Calendar suggestions count.
  std::string cal_body = read_text(
      ctx.storage,
      ctx.storage.join(staging_dir, "calendar-suggestions.json"));
  if (cal_body.empty()) {
    out += "calendar: 0\n";
  } else {
    try {
      auto j = nlohmann::json::parse(cal_body);
      const nlohmann::json* arr = nullptr;
      if (j.is_object() && j.contains("events") &&
          j["events"].is_array()) {
        arr = &j["events"];
      } else if (j.is_array()) {
        arr = &j;
      }
      const std::size_t n = arr ? arr->size() : 1;
      char buf[32];
      std::snprintf(buf, sizeof(buf), "calendar: %zu", n);
      out += buf;
      out += '\n';
    } catch (...) {
      out += "calendar: 1\n";
    }
  }

  // Agent prompt first line.
  std::string prompt_body = read_text(
      ctx.storage, ctx.storage.join(staging_dir, "agent-prompt.md"));
  std::string prompt_line = first_line(prompt_body);
  if (prompt_line.empty()) {
    out += "agent-prompt: (missing)\n";
  } else {
    out += "agent-prompt:";
    out += prompt_line;
    out += '\n';
  }
  return out;
}

// Draw a multi-line body string to the screen. Honors the
// <max_rows> cap so a giant file doesn't push the footer off
// the screen. We keep this in sync with the same body the host
// tests see in render_review_summary.
void draw_body(const std::string& header, const std::string& body,
               const std::string& footer, int max_rows) {
  disp().clear();
  ui::StatusBar bar(disp());
  bar.draw(false);
  disp().text(kLeft, kHeaderY, kFg, header);
  int row = 0;
  std::size_t i = 0;
  while (i < body.size() && row < max_rows) {
    std::size_t j = body.find('\n', i);
    if (j == std::string::npos) j = body.size();
    std::string line = body.substr(i, j - i);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    disp().text(kLeft, kListTop + row * kRowH, kFg, line);
    ++row;
    i = j + 1;
  }
  disp().text(kLeft, kFooterY, kDim, footer);
  disp().push();
}

// Pretty-print a read-only view of one of the six artifacts.
void show_artifact_view(const std::string& title,
                        const std::string& body,
                        const std::string& empty_label) {
  const int max_rows =
      (platform::Display::kHeight - kListTop - kRowH) / kRowH;
  std::string payload = body.empty() ? empty_label : body;
  draw_body(title, payload, "[Esc] back  [Enter] accept", max_rows);
}

// Read a single artifact's body. Pretty-prints tasks.json /
// calendar-suggestions.json so the on-screen view is readable.
std::string read_artifact_body(Ctx& ctx, const std::string& staging_dir,
                               const std::string& label) {
  std::string body = read_text(
      ctx.storage, ctx.storage.join(staging_dir, label));
  if (body.empty()) return "";
  if (label == "tasks.json" || label == "calendar-suggestions.json") {
    return format_json_pretty(body);
  }
  return body;
}

}  // namespace

std::string render_review_summary(Ctx& ctx, const std::string& request_id) {
  if (!ctx.staging || request_id.empty()) return "";
  StagingEntry entry;
  std::string err;
  std::string r = ctx.staging->read_meta(request_id, &entry, &err);
  if (!r.empty()) return "";
  return build_summary(ctx, entry);
}

std::string render_recent_staging(Ctx& ctx, int max_rows) {
  if (!ctx.staging) return "";
  std::vector<StagingEntry> entries;
  std::string err;
  std::string r = ctx.staging->list_pending(&entries, &err);
  if (!r.empty()) return "";
  std::string out;
  out += "header=Pending reviews count=";
  out += std::to_string(entries.size());
  out += '\n';
  if (entries.empty()) return out;
  // List the most recent `max_rows` (entries is sorted ascending
  // by arrived_at). We print the last `max_rows`.
  const std::size_t take = std::min<std::size_t>(
      static_cast<std::size_t>(max_rows), entries.size());
  const std::size_t start = entries.size() - take;
  for (std::size_t i = start; i < entries.size(); ++i) {
    const auto& e = entries[i];
    char line[160];
    std::snprintf(line, sizeof(line),
                  "row=%s\t%s\t%s",
                  e.status.c_str(), e.arrived_at.c_str(),
                  e.project.c_str());
    out += line;
    out += '\n';
  }
  return out;
}

Route route_review_impl(Ctx& ctx, const std::string& request_id) {
  if (!ctx.staging) {
    disp().begin();
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(false);
    disp().text(kLeft, 24, kFg, "Review (no staging)");
    disp().text(kLeft, kFooterY, kDim, "[Esc] back");
    disp().push();
    for (;;) {
      const platform::KeyEvent ev = platform::poll();
      if (ev.any) return Route::Home;
    }
  }

  // Compose the summary once per redraw. The host tests use
  // render_review_summary directly; the route body reuses the
  // same string for the on-screen body so they cannot drift.
  std::string summary = render_review_summary(ctx, request_id);
  if (summary.empty()) {
    // Unknown id or no longer pending. Show a stub and return
    // home; the dispatcher should normally have guarded this
    // case but we are defensive here.
    disp().begin();
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(false);
    disp().text(kLeft, 24, kWarn, "Review: unknown request");
    disp().text(kLeft, kFooterY, kDim, "[Esc] back");
    disp().push();
    for (;;) {
      const platform::KeyEvent ev = platform::poll();
      if (ev.any) return Route::Home;
    }
  }

  // Strip the header line for the body and use it as the
  // screen title. The summary starts with
  //   header=Review request=... project=... title=...
  std::string header_line;
  std::string body;
  {
    std::size_t nl = summary.find('\n');
    if (nl == std::string::npos) {
      header_line = summary;
    } else {
      header_line = summary.substr(0, nl);
      body = summary.substr(nl + 1);
    }
  }
  // Drop the "header=" prefix in the on-screen header; keep it
  // for the host-testable string.
  std::string title = header_line;
  if (title.compare(0, 7, "header=") == 0) title.erase(0, 7);

  const int max_rows =
      (platform::Display::kHeight - kListTop - kRowH) / kRowH;
  draw_body(title, body, "[Enter] accept  [Esc] reject", max_rows);

  // Compute the staging dir for sub-mode reads.
  std::string staging_dir = ctx.storage.join(
      ctx.staging->staging_dir(), request_id);

  for (;;) {
    const platform::KeyEvent ev = platform::poll();
    if (!ev.any) continue;
    if (ev.escape) {
      std::string err;
      ctx.staging->reject(request_id, &err);
      // err is best-effort; we still return home so the user
      // isn't trapped on the screen.
      return Route::Home;
    }
    if (ev.enter) {
      std::string err;
      ctx.staging->accept(request_id, &err);
      return Route::Home;
    }
    if (ev.c == 'e' || ev.c == 'E') {
      show_artifact_view(
          "brief.md",
          read_artifact_body(ctx, staging_dir, "brief.md"),
          "(brief.md is empty)");
    } else if (ev.c == 't' || ev.c == 'T') {
      show_artifact_view(
          "tasks.json",
          read_artifact_body(ctx, staging_dir, "tasks.json"),
          "(tasks.json is empty)");
    } else if (ev.c == 'c' || ev.c == 'C') {
      show_artifact_view(
          "calendar-suggestions.json",
          read_artifact_body(ctx, staging_dir, "calendar-suggestions.json"),
          "(calendar-suggestions.json is empty)");
    } else if (ev.c == 'a' || ev.c == 'A') {
      show_artifact_view(
          "agent-prompt.md",
          read_artifact_body(ctx, staging_dir, "agent-prompt.md"),
          "(agent-prompt.md is empty)");
    }
  }
}

}  // namespace app
}  // namespace advdeck
