// src/app/recorder.h
//
// Recording UI. Per PHASE-4-INTERFACES.md §6.1 and §6.2.
//
// The route is split into:
//   - render_record_list_screen: list the existing voice/*.wav
//     files for the current project. Host-testable.
//   - render_recorder_screen: the recording screen contents as
//     a multi-line string. Host-testable.
//   - route_record_list_impl: the blocking key loop for the
//     list view.
//   - route_recorder_impl: the blocking key loop for the
//     recorder itself.
//
// Phase 4 keeps transcription explicit (the user manually runs
// the bridge CLI on a desktop). The 't' key on the record list
// exits the list and the on-screen footer documents this. The
// 'n' key starts a new recording (calls route_recorder_impl).

#ifndef ADVDECK_SRC_APP_RECORDER_H_
#define ADVDECK_SRC_APP_RECORDER_H_

#include <cstdint>
#include <string>

#include "app/routes.h"

namespace advdeck {
namespace app {

// Recording state machine. Mirrors the §6.2 spec.
enum class RecordingState {
  Idle,
  Recording,
  Paused,
  Stopped,
};

// In-flight statistics for the recorder screen. The render
// function uses these so the host test can drive every
// transition.
struct RecorderStatus {
  RecordingState state = RecordingState::Idle;
  uint32_t elapsed_ms = 0;
  uint32_t target_seconds = 30;   // 15/30/60 via 1/2/3 keys
  uint32_t sample_rate = 16000;
  uint16_t channels = 1;
  uint16_t bits_per_sample = 16;
  uint32_t level = 0;             // 0..100 from the most recent chunk
  std::string current_file;       // "recording-1.wav" or ""
  std::string last_error;         // for transient footer display
};

// Render the record list screen as a multi-line string. Used
// by both the host tests and the on-screen renderer. The
// string includes the project slug header, one row per
// existing recording (mtime + size), and a footer. Returns
// "" when the slug is empty.
std::string render_record_list_screen(Ctx& ctx,
                                      const std::string& slug);

// Render the recorder screen as a multi-line string. The
// host tests assert on substrings of this. `state` carries
// the live status; the route updates it from the keyboard.
std::string render_recorder_screen(Ctx& ctx, const std::string& slug,
                                   const RecorderStatus& status);

// Real implementation of the RecordList route. The home menu
// "Record" entry picks a project (or aborts to Home) and then
// dispatches to the list view. The list view shows existing
// voice files and binds:
//   n — new recording (-> route_recorder_impl)
//   e — show existing transcript.md read-only (placeholder)
//   t — placeholder for transcribe; exits the list and
//       displays a "run the bridge" message
//   Esc — return home
Route route_record_list_impl(Ctx& ctx, const std::string& slug);

// Real implementation of the Recorder route. State machine
// keys (per §6.2):
//   r — start, then stop
//   1/2/3 — set target to 15s / 30s / 60s
//   Esc — cancel (deletes the partial file)
Route route_recorder_impl(Ctx& ctx, const std::string& slug);

// The home menu "Record" entry point. Picks the current
// project (mirroring route_project_detail's fallback to the
// first project) and calls route_record_list_impl.
Route route_record_entry_point(Ctx& ctx);

}  // namespace app
}  // namespace advdeck

#endif  // ADVDECK_SRC_APP_RECORDER_H_
