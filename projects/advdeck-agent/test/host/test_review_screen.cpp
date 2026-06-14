// test/host/test_review_screen.cpp
//
// Host tests for the B3.1 review screen. We drive the
// `render_review_summary` host-testable helper plus the staging
// flow that backs it:
//   - The unknown-id case returns "".
//   - A staged entry's summary contains the project slug, the
//     word "Tasks:" and the task count.
//   - Calling StagingQueue::accept after a stage moves the
//     artifacts into the project folder.
//
// The route body (route_review_impl) is not exercised directly —
// it drives a blocking keyboard loop. The summary helper is what
// the host tests assert on.

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "advdeck/expect.h"
#include "advdeck/outbox_queue.h"
#include "advdeck/project_store.h"
#include "advdeck/staging_queue.h"
#include "advdeck/storage.h"
#include "app/review.h"
#include "app/routes.h"

namespace fs = std::filesystem;

namespace {

advdeck::HostStorage make_storage_with_unique_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_b31_rs_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return advdeck::HostStorage(base.string());
}

std::string write_result_artifact(advdeck::HostStorage& s,
                                  const std::string& request_id,
                                  const std::string& name,
                                  const std::string& body) {
  std::string dir = s.join(s.join(s.join("/advdeck", "outbox"), "results"),
                           request_id);
  std::string e = s.ensure_dir(dir);
  (void)e;
  std::string path = s.join(dir, name);
  s.write_file(path, body);
  return path;
}

void write_result_manifest(advdeck::HostStorage& s,
                           const std::string& request_id,
                           const std::string& slug) {
  std::string dir = s.join(s.join(s.join("/advdeck", "outbox"), "results"),
                           request_id);
  s.ensure_dir(dir);
  std::string body = "{\"request_id\":\"" + request_id +
                     "\",\"status\":\"ok\",\"project\":\"" + slug +
                     "\",\"artifacts\":[]}\n";
  s.write_file(s.join(dir, "result.json"), body);
}

void render_review_summary_returns_empty_for_unknown_request_id() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  advdeck::app::Ctx ctx{s, ps, nullptr, nullptr, nullptr,
                        &q, &st, nullptr};
  // No staging entry has been written; the helper must return "".
  std::string out = advdeck::app::render_review_summary(ctx, "req-0");
  EXPECT_EQ(std::string(""), out);
  // Even an empty id is treated as unknown.
  out = advdeck::app::render_review_summary(ctx, "");
  EXPECT_EQ(std::string(""), out);
}

void render_review_summary_includes_project_slug_and_task_count() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  std::string err;
  // Create the project the staging entry is for.
  std::string slug = ps.create_project("Pocket Agent", "body", &err);
  EXPECT_EQ(std::string(""), err);
  // Enqueue a request, write a result manifest + brief + a
  // 3-task tasks.json, then stage.
  std::string id = q.enqueue(slug, "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  write_result_manifest(s, id, slug);
  write_result_artifact(s, id, "brief.md",
                         "Line 1\nLine 2\nLine 3\n");
  std::string tasks_body =
      "{\"version\":1,\"tasks\":["
      "{\"title\":\"First task\"},"
      "{\"title\":\"Second task\"},"
      "{\"title\":\"Third task\"}"
      "]}\n";
  write_result_artifact(s, id, "tasks.json", tasks_body);
  std::string r = st.stage(id, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);
  // Now ask the helper for a summary.
  advdeck::app::Ctx ctx{s, ps, nullptr, nullptr, nullptr,
                        &q, &st, nullptr};
  std::string out = advdeck::app::render_review_summary(ctx, id);
  // The slug, the "Tasks:" label, and the task count "3" must all
  // appear in the rendered string.
  EXPECT_TRUE(out.find(slug) != std::string::npos);
  EXPECT_TRUE(out.find("Tasks:") != std::string::npos);
  EXPECT_TRUE(out.find("3") != std::string::npos);
  // First brief line should also be present (line 1).
  EXPECT_TRUE(out.find("Line 1") != std::string::npos);
}

void accept_after_stage_moves_files_to_project_folder() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  std::string err;
  std::string slug = ps.create_project("Pocket Agent", "body", &err);
  EXPECT_EQ(std::string(""), err);
  std::string id = q.enqueue(slug, "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  write_result_manifest(s, id, slug);
  write_result_artifact(s, id, "brief.md", "B\n");
  // Stage and then accept.
  std::string r = st.stage(id, &err);
  EXPECT_EQ(std::string(""), r);
  r = st.accept(id, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);
  // The project folder must now contain brief.md.
  std::string project_brief = s.read_file(
      s.join(s.join(s.join("/advdeck", "projects"), slug), "brief.md"));
  EXPECT_EQ(std::string("B\n"), project_brief);
}

}  // namespace

ADVDECK_REGISTER_TEST(
    review_render_review_summary_returns_empty_for_unknown_request_id,
    render_review_summary_returns_empty_for_unknown_request_id);
ADVDECK_REGISTER_TEST(
    review_render_review_summary_includes_project_slug_and_task_count,
    render_review_summary_includes_project_slug_and_task_count);
ADVDECK_REGISTER_TEST(review_accept_after_stage_moves_files_to_project_folder,
                     accept_after_stage_moves_files_to_project_folder);
