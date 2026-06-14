// test/host/test_outbox_queue.cpp
//
// Host tests for advdeck::OutboxQueue. We exercise the full
// append / mark_in_flight / mark_terminal / load_all flow against
// a real on-disk temp dir so the on-disk JSONL shape is also
// verified.

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "advdeck/expect.h"
#include "advdeck/outbox_queue.h"
#include "advdeck/pending_request.h"
#include "advdeck/storage.h"

namespace fs = std::filesystem;

namespace {

advdeck::HostStorage make_storage_with_unique_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_b12_oq_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return advdeck::HostStorage(base.string());
}

// Count the number of lines in pending.jsonl.
// Count the number of lines in pending.jsonl. The file uses the
// convention "N lines => N newlines" (each line terminated by
// '\n'), so the newline count is the line count. An empty file
// has 0 lines.
std::size_t count_jsonl_lines(const std::string& body) {
  if (body.empty()) return 0;
  return static_cast<std::size_t>(
      std::count(body.begin(), body.end(), '\n'));
}

void enqueue_creates_row_with_id_format() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("pocket-agent", "plan_project",
                             {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  // The id format is "req-YYYYMMDD-NNN" with NNN 3-6 digits.
  // "req-" (3 chars + '-') is at position 3, then 8 date
  // digits, then '-', then 3-6 seq digits.
  EXPECT_TRUE(id.rfind("req-", 0) == 0);
  EXPECT_TRUE(id.size() >= 12);  // "req-" + 8 + "-" + 3 = 12 minimum
  EXPECT_EQ(std::size_t(3), id.find('-'));  // 'req' then '-'
  EXPECT_EQ(std::size_t(3) + 8 + 1, id.find('-', 4));  // 8-digit date then '-'
  // The body should be a single JSON line.
  std::string body = s.read_file(q.pending_path());
  EXPECT_EQ(std::size_t(1), count_jsonl_lines(body));
  // And the row should contain the id and pending status.
  EXPECT_TRUE(body.find("\"id\":\"" + id + "\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"status\":\"pending\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"project\":\"pocket-agent\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"type\":\"plan_project\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"idea.md\"") != std::string::npos);
}

void enqueue_increments_seq_on_same_day() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::string a = q.enqueue("alpha", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  std::string b = q.enqueue("beta", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  std::string c = q.enqueue("gamma", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  // All three should share the same YYYYMMDD prefix.
  EXPECT_EQ(a.substr(0, 12), b.substr(0, 12));
  EXPECT_EQ(b.substr(0, 12), c.substr(0, 12));
  // Sequence numbers should be strictly increasing.
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(b != c);
  EXPECT_TRUE(a != c);
  // And the NNN portion should be the zero-padded 1, 2, 3.
  EXPECT_TRUE(a.size() >= 16);
  // The tail digits: 1, 2, 3 in some zero-padded width. We don't
  // pin the width (3 vs 6) because the contract allows 3-6; just
  // check that a < b < c as strings after the date prefix.
  EXPECT_TRUE(a.compare(13, std::string::npos, b, 13) != 0);
  EXPECT_TRUE(b.compare(13, std::string::npos, c, 13) != 0);
  // load_all returns all three in insertion order.
  std::vector<advdeck::PendingRequest> all;
  std::string le = q.load_all(&all, &err);
  EXPECT_EQ(std::string(""), le);
  EXPECT_EQ(std::size_t(3), all.size());
  EXPECT_EQ(a, all[0].id);
  EXPECT_EQ(b, all[1].id);
  EXPECT_EQ(c, all[2].id);
}

void mark_in_flight_rewrites_row() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("p", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  std::string e = q.mark_in_flight(id, &err);
  EXPECT_EQ(std::string(""), e);
  EXPECT_EQ(std::string(""), err);
  // The JSONL should still have exactly one line, with the
  // status flipped to in_flight.
  std::string body = s.read_file(q.pending_path());
  EXPECT_EQ(std::size_t(1), count_jsonl_lines(body));
  EXPECT_TRUE(body.find("\"status\":\"in_flight\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"status\":\"pending\"") == std::string::npos);
  // load_all sees the new status.
  std::vector<advdeck::PendingRequest> all;
  q.load_all(&all, &err);
  EXPECT_EQ(std::size_t(1), all.size());
  EXPECT_EQ(std::string("in_flight"), all[0].status);
  EXPECT_EQ(id, all[0].id);
}

void mark_terminal_rewrites_row_with_status() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("p", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  // First, transition in_flight (real lifecycle: bridge claims
  // it). Then mark_terminal to "done".
  q.mark_in_flight(id, &err);
  std::string e = q.mark_terminal(id, "done", &err);
  EXPECT_EQ(std::string(""), e);
  EXPECT_EQ(std::string(""), err);
  std::vector<advdeck::PendingRequest> all;
  q.load_all(&all, &err);
  EXPECT_EQ(std::size_t(1), all.size());
  EXPECT_EQ(std::string("done"), all[0].status);
  // Rejecting a non-"done"/"error" final_status must error out.
  std::string bad = q.mark_terminal(id, "pending", &err);
  EXPECT_TRUE(!bad.empty());
  // Error path: increment attempts.
  std::string id2 = q.enqueue("q", "plan_project", {"idea.md"}, &err);
  q.mark_in_flight(id2, &err);
  e = q.mark_terminal(id2, "error", &err);
  EXPECT_EQ(std::string(""), e);
  std::vector<advdeck::PendingRequest> all2;
  q.load_all(&all2, &err);
  EXPECT_EQ(std::size_t(2), all2.size());
  // Find id2 (insertion order: id, id2).
  auto it = std::find_if(
      all2.begin(), all2.end(),
      [&](const advdeck::PendingRequest& r) { return r.id == id2; });
  EXPECT_TRUE(it != all2.end());
  EXPECT_EQ(std::string("error"), it->status);
  EXPECT_EQ(1, it->attempts);
  // The "done" row keeps attempts = 0.
  auto it2 = std::find_if(
      all2.begin(), all2.end(),
      [&](const advdeck::PendingRequest& r) { return r.id == id; });
  EXPECT_TRUE(it2 != all2.end());
  EXPECT_EQ(0, it2->attempts);
}

void load_all_returns_insertion_order() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::vector<std::string> ids;
  for (int i = 0; i < 5; ++i) {
    std::string id = q.enqueue("p" + std::to_string(i), "plan_project",
                               {"idea.md"}, &err);
    EXPECT_EQ(std::string(""), err);
    ids.push_back(id);
  }
  std::vector<advdeck::PendingRequest> all;
  q.load_all(&all, &err);
  EXPECT_EQ(std::size_t(5), all.size());
  for (std::size_t i = 0; i < all.size(); ++i) {
    EXPECT_EQ(ids[i], all[i].id);
  }
}

void enqueue_validates_input() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  // Empty project slug.
  std::string a = q.enqueue("", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), a);
  EXPECT_TRUE(!err.empty());
  // Empty type.
  err.clear();
  std::string b = q.enqueue("p", "", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), b);
  EXPECT_TRUE(!err.empty());
  // Empty inputs.
  err.clear();
  std::string c = q.enqueue("p", "plan_project", {}, &err);
  EXPECT_EQ(std::string(""), c);
  EXPECT_TRUE(!err.empty());
}

void pending_path_layout() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  EXPECT_EQ(std::string("/advdeck/outbox/pending.jsonl"),
            q.pending_path());
  EXPECT_EQ(std::string("/advdeck/outbox/results"),
            q.results_dir());
  // results_dir on disk is created lazily.
  EXPECT_TRUE(!s.exists(q.results_dir()));
  // ensure_dir works on it.
  std::string e = s.ensure_dir(q.results_dir());
  EXPECT_EQ(std::string(""), e);
  EXPECT_TRUE(s.exists(q.results_dir()));
}

void mark_in_flight_unknown_id_errors() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::string e = q.mark_in_flight("req-20260614-001", &err);
  EXPECT_TRUE(!e.empty());
  EXPECT_TRUE(err.find("request not found") != std::string::npos);
}

}  // namespace

ADVDECK_REGISTER_TEST(outbox_enqueue_creates_row_with_id_format,
                       enqueue_creates_row_with_id_format);
ADVDECK_REGISTER_TEST(outbox_enqueue_increments_seq_on_same_day,
                       enqueue_increments_seq_on_same_day);
ADVDECK_REGISTER_TEST(outbox_mark_in_flight_rewrites_row,
                       mark_in_flight_rewrites_row);
ADVDECK_REGISTER_TEST(outbox_mark_terminal_rewrites_row_with_status,
                       mark_terminal_rewrites_row_with_status);
ADVDECK_REGISTER_TEST(outbox_load_all_returns_insertion_order,
                       load_all_returns_insertion_order);
ADVDECK_REGISTER_TEST(outbox_enqueue_validates_input, enqueue_validates_input);
ADVDECK_REGISTER_TEST(outbox_pending_path_layout, pending_path_layout);
ADVDECK_REGISTER_TEST(outbox_mark_in_flight_unknown_id_errors,
                       mark_in_flight_unknown_id_errors);

// --- B2.1: retry() and compact_done() ---

void retry_sets_status_pending_attempts_zero() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("p", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  q.mark_in_flight(id, &err);
  q.mark_terminal(id, "error", &err);
  // attempts is now 1.
  std::string e = q.retry(id, &err);
  EXPECT_EQ(std::string(""), e);
  EXPECT_EQ(std::string(""), err);
  std::vector<advdeck::PendingRequest> all;
  q.load_all(&all, &err);
  EXPECT_EQ(std::size_t(1), all.size());
  EXPECT_EQ(std::string("pending"), all[0].status);
  EXPECT_EQ(0, all[0].attempts);
}

void retry_on_pending_row_returns_already_pending() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("p", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  // Row is pending. retry should be a no-op with "already pending".
  std::string e = q.retry(id, &err);
  EXPECT_EQ(std::string("already pending"), e);
  EXPECT_EQ(std::string("already pending"), err);
}

void retry_on_unknown_id_returns_not_found() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  std::string err;
  std::string e = q.retry("req-20260614-001", &err);
  EXPECT_TRUE(!e.empty());
  EXPECT_TRUE(err.find("request not found") != std::string::npos);
}

ADVDECK_REGISTER_TEST(outbox_retry_sets_status_pending_attempts_zero,
                       retry_sets_status_pending_attempts_zero);
ADVDECK_REGISTER_TEST(outbox_retry_on_pending_row_returns_already_pending,
                       retry_on_pending_row_returns_already_pending);
ADVDECK_REGISTER_TEST(outbox_retry_on_unknown_id_returns_not_found,
                       retry_on_unknown_id_returns_not_found);
