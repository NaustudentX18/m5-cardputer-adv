// test/host/test_recorder_screen.cpp
//
// Host tests for the recording UI. We drive the host-testable
// render helpers and the record list helpers, plus the
// recorder's voice-dir scan logic.

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

#include "advdeck/expect.h"
#include "advdeck/outbox_queue.h"
#include "advdeck/recorder.h"
#include "advdeck/staging_queue.h"
#include "advdeck/storage.h"
#include "app/recorder.h"
#include "app/routes.h"
#include "app/recorder.h"
#include "app/routes.h"

namespace fs = std::filesystem;

namespace {
fs::path make_temp_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_d11_uiscreen_" +
                   std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return base;
}

advdeck::HostStorage fresh_storage() {
  return advdeck::HostStorage(make_temp_root().string());
}

void render_recorder_screen_shows_target_duration() {
  auto s = fresh_storage();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  advdeck::app::Ctx ctx{s, ps, nullptr, nullptr, nullptr,
                        &q, &st, nullptr};
  advdeck::app::RecorderStatus status;
  status.state = advdeck::app::RecordingState::Idle;
  status.target_seconds = 15;
  status.elapsed_ms = 0;
  status.level = 0;
  std::string out =
      advdeck::app::render_recorder_screen(ctx, "p1", status);
  EXPECT_TRUE(out.find("00:00 / 00:15") != std::string::npos);
  EXPECT_TRUE(out.find("state=idle") != std::string::npos);
  EXPECT_TRUE(out.find("[1]15s") != std::string::npos);
  // Now switch the target to 60s.
  status.target_seconds = 60;
  out = advdeck::app::render_recorder_screen(ctx, "p1", status);
  EXPECT_TRUE(out.find("00:00 / 01:00") != std::string::npos);
}

void render_recorder_screen_shows_elapsed_time_during_recording() {
  auto s = fresh_storage();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  advdeck::app::Ctx ctx{s, ps, nullptr, nullptr, nullptr,
                        &q, &st, nullptr};
  advdeck::app::RecorderStatus status;
  status.state = advdeck::app::RecordingState::Recording;
  status.target_seconds = 30;
  status.elapsed_ms = 12500;  // 12.5 seconds
  status.level = 42;
  status.current_file = "recording-1.wav";
  std::string out =
      advdeck::app::render_recorder_screen(ctx, "p1", status);
  EXPECT_TRUE(out.find("state=recording") != std::string::npos);
  EXPECT_TRUE(out.find("00:12 / 00:30") != std::string::npos);
  EXPECT_TRUE(out.find("level=42") != std::string::npos);
  EXPECT_TRUE(out.find("file=recording-1.wav") != std::string::npos);
}

void render_recorder_screen_shows_no_files_message_when_voice_dir_empty() {
  auto s = fresh_storage();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  advdeck::app::Ctx ctx{s, ps, nullptr, nullptr, nullptr,
                        &q, &st, nullptr};
  std::string out =
      advdeck::app::render_record_list_screen(ctx, "p-empty");
  // The header should mention the project slug.
  EXPECT_TRUE(out.find("header=Record project=p-empty") !=
              std::string::npos);
  // And it should tell the user to press 'n'.
  EXPECT_TRUE(out.find("(no recordings") != std::string::npos);
  // Slug is empty -> the helper still returns something
  // useful (a "no project" marker).
  std::string empty_out =
      advdeck::app::render_record_list_screen(ctx, "");
  EXPECT_TRUE(empty_out.find("no project") != std::string::npos);
}

}  // namespace

ADVDECK_REGISTER_TEST(screen_render_recorder_screen_shows_target_duration,
                      render_recorder_screen_shows_target_duration);
ADVDECK_REGISTER_TEST(
    screen_render_recorder_screen_shows_elapsed_time_during_recording,
    render_recorder_screen_shows_elapsed_time_during_recording);
ADVDECK_REGISTER_TEST(
    screen_render_recorder_screen_shows_no_files_message_when_voice_dir_empty,
    render_recorder_screen_shows_no_files_message_when_voice_dir_empty);
