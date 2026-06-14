// test/host/test_sync_ui.cpp
//
// Host test for the sync UI's host-testable render_sync_screen()
// helper. The UI is a function that takes a Ctx& and returns a
// string; that's what this test exercises.

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "advdeck/expect.h"
#include "advdeck/outbox_queue.h"
#include "advdeck/project_store.h"
#include "advdeck/storage.h"
#include "app/routes.h"
#include "app/sync.h"

namespace fs = std::filesystem;

namespace {

advdeck::HostStorage make_storage_with_unique_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_b21_su_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return advdeck::HostStorage(base.string());
}

void render_sync_screen_counts_known_pending_jsonl() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  // Build a known mix: 2 pending, 1 in_flight, 3 done, 2 errored.
  std::vector<std::string> ids;
  for (int i = 0; i < 8; ++i) {
    std::string id = q.enqueue("p", "plan_project", {"idea.md"}, &err);
    EXPECT_EQ(std::string(""), err);
    ids.push_back(id);
  }
  // 2 pending (already): ids[0], ids[1] stay pending
  // 1 in_flight: ids[2]
  q.mark_in_flight(ids[2], &err);
  // 3 done: ids[3..5]
  for (int i = 3; i <= 5; ++i) {
    q.mark_in_flight(ids[i], &err);
    q.mark_terminal(ids[i], "done", &err);
  }
  // 2 errored: ids[6], ids[7]
  for (int i = 6; i <= 7; ++i) {
    q.mark_in_flight(ids[i], &err);
    q.mark_terminal(ids[i], "error", &err);
  }
  advdeck::app::Ctx ctx{s, ps, nullptr, nullptr, nullptr,
                        &q, nullptr, nullptr};
  std::string out = advdeck::app::render_sync_screen(ctx);
  // Header line: "header=Sync counts=P:2 I:1 D:3 E:2"
  EXPECT_TRUE(out.find("counts=P:2 I:1 D:3 E:2") != std::string::npos);
  // Errored rows come first in the visible list, two of them.
  std::size_t pos = 0;
  int errored_count = 0;
  while ((pos = out.find("row=error\t", pos)) != std::string::npos) {
    ++errored_count;
    ++pos;
  }
  EXPECT_EQ(2, errored_count);
  // Pending rows: 2 of them.
  int pending_count = 0;
  pos = 0;
  while ((pos = out.find("row=pending\t", pos)) != std::string::npos) {
    ++pending_count;
    ++pos;
  }
  EXPECT_EQ(2, pending_count);
  // In-flight: 1.
  int in_flight_count = 0;
  pos = 0;
  while ((pos = out.find("row=in_flight\t", pos)) != std::string::npos) {
    ++in_flight_count;
    ++pos;
  }
  EXPECT_EQ(1, in_flight_count);
  // Done: capped at 5, we have 3.
  int done_count = 0;
  pos = 0;
  while ((pos = out.find("row=done\t", pos)) != std::string::npos) {
    ++done_count;
    ++pos;
  }
  EXPECT_EQ(3, done_count);
}

}  // namespace

ADVDECK_REGISTER_TEST(sync_render_sync_screen_counts_known_pending_jsonl,
                       render_sync_screen_counts_known_pending_jsonl);
