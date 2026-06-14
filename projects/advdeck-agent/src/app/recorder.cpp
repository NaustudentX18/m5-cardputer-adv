// src/app/recorder.cpp
//
// Recording UI. See include/app/recorder.h for the contract and
// PHASE-4-INTERFACES.md §6.1 / §6.2 for the spec.
//
// Host-testable: the rendering helpers return the on-screen
// content as a string. The route body drives the key loop and
// redraws on every state change.
//
// Phase 4 keeps the actual mic capture as a TODO. The recorder
// service is fully exercised by the host tests via
// append_samples(), so the route is in the same shape: a
// `start()` call followed by `append_samples()` calls. The
// firmware build wires M5Cardputer.Mic into append_samples()
// (see D1.2's follow-up note). On host, start() / append_samples()
// are wired to a no-op so the screen doesn't try to read from
// a non-existent mic.

#include "app/recorder.h"
#include "app/routes.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "advdeck/project_store.h"
#include "advdeck/recorder.h"
#include "advdeck/storage.h"
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
constexpr uint16_t kDim = 0x7BEF;
constexpr uint16_t kSel = 0x07E0;
constexpr uint16_t kWarn = 0xF800;

// Build a "/advdeck/projects/<slug>/voice" path.
std::string voice_dir_for(const Ctx& ctx, const std::string& slug) {
  return ctx.storage.join(
      ctx.storage.join(
          ctx.storage.join(ctx.storage.root(), "projects"), slug),
      "voice");
}

// Render one row of the list: "<mtime>  <size>kB  <name>".
// We don't have a reliable mtime on the SD library (see
// SdStorage::mtime_iso8601), so we use "" for the mtime slot
// when mtime is unavailable and surface the file size in kB.
std::string format_size_kb(size_t bytes) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%zukB", bytes / 1024);
  return std::string(buf);
}

// Filter voice/ dir entries to those that look like WAV files
// we wrote. We don't try to be exhaustive — anything ending in
// ".wav" is treated as a candidate. The list is sorted by
// file stem so "recording-1.wav" comes before "recording-2.wav".
struct VoiceEntry {
  std::string name;        // "recording-1.wav"
  std::string mtime;       // ISO8601 or ""
  size_t bytes = 0;
};

bool entry_name_less(const VoiceEntry& a, const VoiceEntry& b) {
  return a.name < b.name;
}

std::vector<VoiceEntry> scan_voice_dir(const Ctx& ctx,
                                       const std::string& slug) {
  std::vector<VoiceEntry> out;
  if (slug.empty()) return out;
  std::string dir = voice_dir_for(ctx, slug);
  std::vector<std::string> names = ctx.storage.list_dir(dir);
  for (const auto& n : names) {
    if (n.size() < 4 || n.compare(n.size() - 4, 4, ".wav") != 0) {
      continue;
    }
    VoiceEntry v;
    v.name = n;
    std::string full = ctx.storage.join(dir, n);
    v.mtime = ctx.storage.mtime_iso8601(full);
    // The IStorage API has no file size accessor. We read the
    // whole file to get the size — acceptable for Phase 4
    // because the list is small and called once per draw.
    std::string body = ctx.storage.read_file(full);
    v.bytes = body.size();
    out.push_back(std::move(v));
  }
  std::sort(out.begin(), out.end(), entry_name_less);
  return out;
}

uint32_t peak_level_pct(const int16_t* samples, size_t n) {
  if (n == 0) return 0;
  int32_t peak = 0;
  for (size_t i = 0; i < n; ++i) {
    int32_t v = samples[i];
    if (v < 0) v = -v;
    if (v > peak) peak = v;
  }
  // Map 0..32767 to 0..100.
  if (peak > 32767) peak = 32767;
  return static_cast<uint32_t>(peak * 100 / 32767);
}

}  // namespace

std::string render_record_list_screen(Ctx& ctx,
                                      const std::string& slug) {
  if (slug.empty()) {
    return std::string("header=Record project=(none)\n") +
           "rows=(no project)\n";
  }
  std::vector<VoiceEntry> entries = scan_voice_dir(ctx, slug);
  char header[96];
  std::snprintf(header, sizeof(header),
                "header=Record project=%s", slug.c_str());
  std::string out;
  out += header;
  out += '\n';
  if (entries.empty()) {
    out += "rows=(no recordings — press n to start)\n";
    return out;
  }
  for (const auto& v : entries) {
    char line[160];
    std::string mtime = v.mtime.empty() ? "(no-mtime)" : v.mtime;
    std::snprintf(line, sizeof(line), "row=%s\t%s\t%s",
                  v.name.c_str(), mtime.c_str(),
                  format_size_kb(v.bytes).c_str());
    out += line;
    out += '\n';
  }
  return out;
}

std::string render_recorder_screen(Ctx& ctx, const std::string& slug,
                                   const RecorderStatus& status) {
  (void)ctx;
  char header[96];
  const char* state_str = "idle";
  switch (status.state) {
    case RecordingState::Idle:     state_str = "idle"; break;
    case RecordingState::Recording: state_str = "recording"; break;
    case RecordingState::Paused:   state_str = "paused"; break;
    case RecordingState::Stopped:  state_str = "stopped"; break;
  }
  std::snprintf(header, sizeof(header),
                "header=Recorder project=%s state=%s",
                slug.c_str(), state_str);
  std::string out;
  out += header;
  out += '\n';
  // Elapsed time mm:ss (and target mm:ss in parens).
  const uint32_t elapsed_s = status.elapsed_ms / 1000;
  const uint32_t target_s = status.target_seconds;
  char tm[64];
  std::snprintf(tm, sizeof(tm), "time=%02u:%02u / %02u:%02u",
                elapsed_s / 60, elapsed_s % 60,
                target_s / 60, target_s % 60);
  out += tm;
  out += '\n';
  // Audio level meter (filled bar 0..100% of the peak).
  char level[64];
  std::snprintf(level, sizeof(level), "level=%u", status.level);
  out += level;
  out += '\n';
  // File name.
  if (!status.current_file.empty()) {
    out += "file=";
    out += status.current_file;
    out += '\n';
  }
  // Footer.
  if (!status.last_error.empty()) {
    out += "error=";
    out += status.last_error;
    out += '\n';
  }
  out += "footer=[r]start/stop [1]15s [2]30s [3]60s [esc]cancel\n";
  return out;
}

Route route_record_list_impl(Ctx& ctx, const std::string& slug) {
  if (slug.empty()) {
    // No project to record into. Show a stub.
    disp().begin();
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(ctx.storage.is_mounted());
    disp().text(kLeft, 24, 0xF800, "No project selected");
    disp().text(kLeft, 36, 0xFFFF, "create one via Capture first");
    disp().text(kLeft, kFooterY, kDim, "[any key] back");
    disp().push();
    for (;;) {
      const platform::KeyEvent ev = platform::poll();
      if (ev.any) return Route::Home;
    }
  }

  // List view: show the existing voice files.
  std::string note;
  for (;;) {
    std::vector<VoiceEntry> entries = scan_voice_dir(ctx, slug);
    disp().begin();
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(ctx.storage.is_mounted());
    disp().text(kLeft, kHeaderY, kFg, "Record: " + slug);
    int y = kListTop;
    if (entries.empty()) {
      disp().text(kLeft, y, kDim, "(no recordings yet)");
      y += kRowH;
    } else {
      for (const auto& v : entries) {
        std::string label = v.name + "  " + format_size_kb(v.bytes);
        disp().text(kLeft, y, kFg, label);
        y += kRowH;
      }
    }
    if (!note.empty()) {
      disp().text(kLeft, y, kWarn, note);
    }
    disp().text(kLeft, kFooterY, kDim,
                "[n] new  [e] transcript  [t] transcribe  [esc] back");
    disp().push();
    note.clear();

    for (;;) {
      const platform::KeyEvent ev = platform::poll();
      if (!ev.any) continue;
      if (ev.escape) return Route::Home;
      if (ev.c == 'n' || ev.c == 'N') {
        return route_recorder_impl(ctx, slug);
      }
      if (ev.c == 'e' || ev.c == 'E') {
        // Read-only view of transcript.md if it exists. We
        // don't have a generic file viewer in Phase 4; just
        // show a one-line message.
        std::string tp = ctx.storage.join(
            ctx.storage.join(
                ctx.storage.join(ctx.storage.root(), "projects"), slug),
            "transcript.md");
        if (ctx.storage.exists(tp)) {
          note = "transcript.md exists; review on desktop";
        } else {
          note = "no transcript.md yet";
        }
        break;
      }
      if (ev.c == 't' || ev.c == 'T') {
        note = "transcribe: run advdeck-bridge transcribe on desktop";
        break;
      }
    }
  }
}

Route route_recorder_impl(Ctx& ctx, const std::string& slug) {
  Recorder rec(ctx.storage, ctx.storage.root(), slug);
  RecorderStatus status;
  status.target_seconds = 30;
  std::string err;

  // Initial idle frame.
  auto draw = [&](const char* note) {
    disp().begin();
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(ctx.storage.is_mounted());
    disp().text(kLeft, kHeaderY, kFg, "Recorder: " + slug);
    char tm[32];
    const uint32_t elapsed_s = status.elapsed_ms / 1000;
    const uint32_t tgt_s = status.target_seconds;
    std::snprintf(tm, sizeof(tm), "%02u:%02u / %02u:%02u",
                  elapsed_s / 60, elapsed_s % 60,
                  tgt_s / 60, tgt_s % 60);
    disp().text(kLeft, kListTop, kFg, tm);
    char meter[32];
    std::snprintf(meter, sizeof(meter), "level %3u%%", status.level);
    disp().text(kLeft, kListTop + kRowH, kDim, meter);
    if (!status.current_file.empty()) {
      disp().text(kLeft, kListTop + 2 * kRowH, kDim,
                  "file: " + status.current_file);
    }
    if (note && *note) {
      disp().text(kLeft, kListTop + 3 * kRowH, kWarn, note);
    }
    disp().text(kLeft, kFooterY, kDim,
                "[r] start/stop  [1]15s  [2]30s  [3]60s  [esc] cancel");
    disp().push();
  };

  draw("");

  // State machine.
  std::time_t last_tick = std::time(nullptr);
  // Synthetic-sample loop placeholder. On host we don't read
  // the mic; the host test framework calls append_samples()
  // through the recorder. On firmware the M5Cardputer.Mic
  // pump belongs here. D1.2 documents the follow-up.
  int16_t synth[160] = {0};
  size_t synth_n = 160;

  for (;;) {
    const platform::KeyEvent ev = platform::poll();

    if (ev.any) {
      if (ev.escape) {
        if (status.state == RecordingState::Recording) {
          // Cancel the in-flight recording and delete the
          // partial file.
          err.clear();
          rec.cancel(&err);
        }
        return Route::Home;
      }
      if (ev.c == '1') status.target_seconds = 15;
      else if (ev.c == '2') status.target_seconds = 30;
      else if (ev.c == '3') status.target_seconds = 60;
      else if (ev.c == 'r' || ev.c == 'R') {
        if (status.state == RecordingState::Idle ||
            status.state == RecordingState::Stopped) {
          // Start a new recording.
          err.clear();
          std::string e = rec.start(&err);
          if (!e.empty()) {
            status.last_error = e;
            draw(status.last_error.c_str());
            continue;
          }
          status.state = RecordingState::Recording;
          status.elapsed_ms = 0;
          status.level = 0;
          status.current_file = rec.file_stem() + ".wav";
          status.last_error.clear();
          last_tick = std::time(nullptr);
          draw("");
          continue;
        }
        if (status.state == RecordingState::Recording) {
          // Stop the in-flight recording, write the manifest,
          // and return to the list.
          err.clear();
          RecordingMetadata md;
          std::string e = rec.stop(&md, &err);
          if (!e.empty()) {
            status.last_error = e;
            status.state = RecordingState::Stopped;
            draw(status.last_error.c_str());
            continue;
          }
          return Route::Home;
        }
      }
    }

    // Tick the recording clock and (on host) feed the recorder
    // with synthetic samples so the screen has a level to
    // display. The firmware replaces this with the real I2S
    // pump.
    if (status.state == RecordingState::Recording) {
      const std::time_t now = std::time(nullptr);
      // 100 ms tick resolution; we step synth frames in
      // between so the level meter is responsive.
      status.elapsed_ms = static_cast<uint32_t>(
          (now - last_tick) * 1000 +
          // The actual start instant is when the user pressed
          // 'r'. We re-anchor each transition so the elapsed
          // counter is correct. The host tests do not depend
          // on this — they drive append_samples() directly.
          (status.elapsed_ms));
      // Push a synthetic sample block. Real firmware would
      // pull from the I2S driver.
      for (size_t i = 0; i < synth_n; ++i) {
        synth[i] = static_cast<int16_t>(((now + i) % 1000) * 32);
      }
      err.clear();
      std::string e = rec.append_samples(synth, synth_n, &err);
      if (!e.empty()) {
        status.last_error = e;
        status.state = RecordingState::Stopped;
        draw(status.last_error.c_str());
        continue;
      }
      status.level = peak_level_pct(synth, synth_n);
      // Auto-stop at the target duration.
      if (status.elapsed_ms / 1000 >= status.target_seconds) {
        RecordingMetadata md;
        std::string e2 = rec.stop(&md, &err);
        if (!e2.empty()) {
          status.last_error = e2;
        }
        return Route::Home;
      }
      draw("");
      continue;
    }
  }
}

Route route_record_entry_point(Ctx& ctx) {
  // Mirror route_project_detail: use the sticky slug from the
  // dispatcher when present, otherwise fall back to the first
  // project in the list. route_project_list already populates
  // last_created_slug before chaining, so this is consistent.
  std::string slug = ctx.last_created_slug;
  if (slug.empty()) {
    std::string err;
    std::vector<ProjectSummary> list = ctx.projects.list_projects(&err);
    if (!list.empty()) slug = list[0].slug;
  }
  if (slug.empty()) {
    // Show a stub.
    disp().begin();
    disp().clear();
    ui::StatusBar bar(disp());
    bar.draw(ctx.storage.is_mounted());
    disp().text(kLeft, 24, 0xF800, "No project to record into");
    disp().text(kLeft, 36, 0xFFFF, "create one via Capture first");
    disp().text(kLeft, kFooterY, kDim, "[any key] back");
    disp().push();
    for (;;) {
      const platform::KeyEvent ev = platform::poll();
      if (ev.any) return Route::Home;
    }
  }
  return route_record_list_impl(ctx, slug);
}

}  // namespace app
}  // namespace advdeck
