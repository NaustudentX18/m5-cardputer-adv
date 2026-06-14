// test/host/test_outbox_compact.cpp
//
// Host tests for OutboxQueue::compact_done — drops `done` rows
// whose result dir mtime is older than the threshold. We control
// the mtime directly by writing to the result dir with a known
// timestamp and then "touching" it via the host filesystem.

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "advdeck/expect.h"
#include "advdeck/outbox_queue.h"
#include "advdeck/storage.h"

namespace fs = std::filesystem;

namespace {

advdeck::HostStorage make_storage_with_unique_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_b21_oc_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return advdeck::HostStorage(base.string());
}

void compact_done_drops_old_done_rows() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("p", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  q.mark_in_flight(id, &err);
  q.mark_terminal(id, "done", &err);
  // Create the result dir with an old mtime.
  std::string rdir = s.join(q.results_dir(), id);
  std::string e = s.ensure_dir(rdir);
  EXPECT_EQ(std::string(""), e);
  // Set the mtime to 30 days ago. file_time_type may differ from
  // system_clock::time_point (it's __file_clock on libstdc++), so
  // we go via file_time_type directly.
  std::string host_rdir = rdir;
  if (!host_rdir.empty() && host_rdir.front() == '/') host_rdir.erase(0, 1);
  fs::path resolved = fs::path(s.root()) / fs::path(host_rdir);
  fs::file_time_type now_ft = fs::file_time_type::clock::now();
  auto old = now_ft - std::chrono::hours(24 * 30);
  std::error_code ec;
  fs::last_write_time(resolved, old, ec);
  // Threshold = 7 days: should be dropped.
  int removed = -1;
  std::string r = q.compact_done(7, &removed, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(1, removed);
  // Verify: pending.jsonl is now empty.
  std::vector<advdeck::PendingRequest> all;
  q.load_all(&all, &err);
  EXPECT_EQ(std::size_t(0), all.size());
}

void compact_done_keeps_new_done_rows() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("p", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  q.mark_in_flight(id, &err);
  q.mark_terminal(id, "done", &err);
  std::string rdir = s.join(q.results_dir(), id);
  std::string e = s.ensure_dir(rdir);
  EXPECT_EQ(std::string(""), e);
  // Don't touch the mtime — the result dir was just created and
  // the filesystem mtime is "now". With a 7-day threshold, the
  // row should be kept.
  int removed = -1;
  std::string r = q.compact_done(7, &removed, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(0, removed);
  std::vector<advdeck::PendingRequest> all;
  q.load_all(&all, &err);
  EXPECT_EQ(std::size_t(1), all.size());
  EXPECT_EQ(id, all[0].id);
  EXPECT_EQ(std::string("done"), all[0].status);
}

}  // namespace

ADVDECK_REGISTER_TEST(outbox_compact_done_drops_old_done_rows,
                       compact_done_drops_old_done_rows);
ADVDECK_REGISTER_TEST(outbox_compact_done_keeps_new_done_rows,
                       compact_done_keeps_new_done_rows);
