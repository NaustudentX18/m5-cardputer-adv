// test/host/test_staging_queue.cpp
//
// Host tests for advdeck::StagingQueue. We drive the full
// stage -> accept / reject -> list_pending flow against a real
// on-disk temp dir.

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

namespace fs = std::filesystem;

namespace {

advdeck::HostStorage make_storage_with_unique_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_b21_sq_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return advdeck::HostStorage(base.string());
}

// Write one of the six artifacts to outbox/results/<id>/. Returns
// the on-storage path used.
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

// Write a result.json for the request, with a project slug, so
// staging() can find the slug.
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

void stage_copies_artifacts_from_results_to_staging() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("p1", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  write_result_artifact(s, id, "brief.md", "# brief\n");
  write_result_artifact(s, id, "plan.md", "# plan\n");
  write_result_manifest(s, id, "p1");
  std::string r = st.stage(id, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);
  // The staging dir contains the two artifacts we wrote.
  std::string sdir = s.join(s.join(s.join("/advdeck", "outbox"), "staging"),
                            id);
  EXPECT_TRUE(s.exists(sdir));
  std::string brief = s.read_file(s.join(sdir, "brief.md"));
  std::string plan = s.read_file(s.join(sdir, "plan.md"));
  EXPECT_EQ(std::string("# brief\n"), brief);
  EXPECT_EQ(std::string("# plan\n"), plan);
}

void stage_writes_meta_with_status_pending() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("p1", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  write_result_manifest(s, id, "p1");
  std::string r = st.stage(id, &err);
  EXPECT_EQ(std::string(""), r);
  // Read meta.json and assert on its fields.
  std::string meta = s.read_file(
      s.join(s.join(s.join(s.join("/advdeck", "outbox"), "staging"), id),
             "meta.json"));
  EXPECT_TRUE(meta.find("\"status\":\"pending\"") != std::string::npos);
  EXPECT_TRUE(meta.find("\"project\":\"p1\"") != std::string::npos);
  EXPECT_TRUE(meta.find("\"request_id\":\"" + id + "\"") !=
              std::string::npos);
}

void accept_moves_files_from_staging_and_results_into_project() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  std::string err;
  std::string slug = ps.create_project("P", "idea\n", &err);
  EXPECT_EQ(std::string(""), err);
  std::string id = q.enqueue(slug, "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  write_result_artifact(s, id, "brief.md", "B\n");
  write_result_artifact(s, id, "plan.md", "P\n");
  write_result_manifest(s, id, slug);
  std::string r = st.stage(id, &err);
  EXPECT_EQ(std::string(""), r);
  r = st.accept(id, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string(""), err);
  // The project folder now has the artifacts.
  // Spot-check: read the brief from the project dir.
  std::string brief = s.read_file(
      s.join(s.join(s.join("/advdeck", "projects"), slug), "brief.md"));
  EXPECT_EQ(std::string("B\n"), brief);
  std::string plan = s.read_file(
      s.join(s.join(s.join("/advdeck", "projects"), slug), "plan.md"));
  EXPECT_EQ(std::string("P\n"), plan);
}

void accept_marks_meta_status_accepted() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  std::string err;
  std::string slug = ps.create_project("P", "idea\n", &err);
  EXPECT_EQ(std::string(""), err);
  std::string id = q.enqueue(slug, "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  write_result_manifest(s, id, slug);
  std::string r = st.stage(id, &err);
  EXPECT_EQ(std::string(""), r);
  r = st.accept(id, &err);
  EXPECT_EQ(std::string(""), r);
  // meta.json should now say "accepted".
  advdeck::StagingEntry e;
  r = st.read_meta(id, &e, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string("accepted"), e.status);
  EXPECT_EQ(slug, e.project);
  EXPECT_EQ(id, e.request_id);
}

void reject_moves_files_from_staging_to_rejected() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("p1", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  write_result_artifact(s, id, "brief.md", "B\n");
  write_result_manifest(s, id, "p1");
  std::string r = st.stage(id, &err);
  EXPECT_EQ(std::string(""), r);
  r = st.reject(id, &err);
  EXPECT_EQ(std::string(""), r);
  // The rejected dir contains the brief.
  std::string rdir = s.join(s.join(s.join("/advdeck", "outbox"), "rejected"),
                            id);
  EXPECT_TRUE(s.exists(rdir));
  std::string brief = s.read_file(s.join(rdir, "brief.md"));
  EXPECT_EQ(std::string("B\n"), brief);
}

void reject_marks_meta_status_rejected() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  std::string err;
  std::string id = q.enqueue("p1", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  write_result_manifest(s, id, "p1");
  std::string r = st.stage(id, &err);
  EXPECT_EQ(std::string(""), r);
  r = st.reject(id, &err);
  EXPECT_EQ(std::string(""), r);
  advdeck::StagingEntry e;
  r = st.read_meta(id, &e, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::string("rejected"), e.status);
}

void list_pending_returns_only_pending() {
  auto s = make_storage_with_unique_root();
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::StagingQueue st(s, "/advdeck");
  std::string err;
  // Stage three, then accept one and reject another.
  std::string a = q.enqueue("p1", "plan_project", {"idea.md"}, &err);
  std::string b = q.enqueue("p1", "plan_project", {"idea.md"}, &err);
  std::string c = q.enqueue("p1", "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  for (const std::string& id : {a, b, c}) {
    write_result_manifest(s, id, "p1");
    std::string r = st.stage(id, &err);
    EXPECT_EQ(std::string(""), r);
  }
  std::string r = st.accept(b, &err);
  EXPECT_EQ(std::string(""), r);
  r = st.reject(c, &err);
  EXPECT_EQ(std::string(""), r);
  // List pending: only `a` should be in the result.
  std::vector<advdeck::StagingEntry> pending;
  r = st.list_pending(&pending, &err);
  EXPECT_EQ(std::string(""), r);
  EXPECT_EQ(std::size_t(1), pending.size());
  EXPECT_EQ(a, pending[0].request_id);
  EXPECT_EQ(std::string("pending"), pending[0].status);
}

}  // namespace

ADVDECK_REGISTER_TEST(staging_stage_copies_artifacts_from_results_to_staging,
                       stage_copies_artifacts_from_results_to_staging);
ADVDECK_REGISTER_TEST(staging_stage_writes_meta_with_status_pending,
                       stage_writes_meta_with_status_pending);
ADVDECK_REGISTER_TEST(staging_accept_moves_files_from_staging_and_results_into_project,
                       accept_moves_files_from_staging_and_results_into_project);
ADVDECK_REGISTER_TEST(staging_accept_marks_meta_status_accepted,
                       accept_marks_meta_status_accepted);
ADVDECK_REGISTER_TEST(staging_reject_moves_files_from_staging_to_rejected,
                       reject_moves_files_from_staging_to_rejected);
ADVDECK_REGISTER_TEST(staging_reject_marks_meta_status_rejected,
                       reject_marks_meta_status_rejected);
ADVDECK_REGISTER_TEST(staging_list_pending_returns_only_pending,
                       list_pending_returns_only_pending);
