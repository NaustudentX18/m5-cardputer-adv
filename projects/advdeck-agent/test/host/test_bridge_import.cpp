// test/host/test_bridge_import.cpp
//
// Host tests for advdeck::BridgeImport. We drive the full
// enqueue -> bridge writes result -> firmware import flow:
//   1. Create a project with a ProjectStore.
//   2. Enqueue a "plan_project" request for that project.
//   3. Write a result manifest into
//      outbox/results/<id>/result.json plus the artifact files
//      (mimicking what the dry-run bridge CLI does in Phase 2).
//   4. Call BridgeImport::import and assert on the ImportResult.

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "advdeck/bridge_import.h"
#include "advdeck/expect.h"
#include "advdeck/outbox_queue.h"
#include "advdeck/project_store.h"
#include "advdeck/storage.h"

namespace fs = std::filesystem;

namespace {

advdeck::HostStorage make_storage_with_unique_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_b12_bi_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return advdeck::HostStorage(base.string());
}

// Helper: write a file to the per-request results dir for a given
// request id. Returns the on-storage path used.
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
                           const nlohmann::json& manifest) {
  std::string dir = s.join(s.join(s.join("/advdeck", "outbox"), "results"),
                           request_id);
  s.ensure_dir(dir);
  std::string body = manifest.dump(2);
  body.push_back('\n');
  s.write_file(s.join(dir, "result.json"), body);
}

void import_valid_result_copies_artifacts() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::BridgeImport importer(s, "/advdeck");
  std::string err;
  // 1) Create a project so the import has a destination.
  std::string slug = ps.create_project("My Cool Idea", "# Title\n\nbody\n",
                                        &err);
  EXPECT_EQ(std::string(""), err);
  // 2) Enqueue a request for it.
  std::string id = q.enqueue(slug, "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  // 3) Bridge writes a result manifest + a few artifacts.
  write_result_artifact(s, id, "brief.md", "# Brief\n\nA brief.\n");
  write_result_artifact(s, id, "plan.md", "# Plan\n\nA plan.\n");
  write_result_artifact(s, id, "tasks.json",
                        "{\"version\":1,\"tasks\":[]}\n");
  nlohmann::json manifest = {
      {"request_id", id},
      {"status", "ok"},
      {"artifacts", nlohmann::json::array(
                        {"brief.md", "plan.md", "tasks.json"})},
      {"warnings", nlohmann::json::array()},
  };
  write_result_manifest(s, id, manifest);
  // 4) Import and assert.
  advdeck::ImportResult result;
  std::string ie = importer.import(id, &result, &err);
  EXPECT_EQ(std::string(""), ie);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(id, result.request_id);
  EXPECT_EQ(std::size_t(3), result.imported_files.size());
  std::set<std::string> landed(result.imported_files.begin(),
                                result.imported_files.end());
  EXPECT_TRUE(landed.count("/advdeck/projects/" + slug + "/brief.md") == 1);
  EXPECT_TRUE(landed.count("/advdeck/projects/" + slug + "/plan.md") == 1);
  EXPECT_TRUE(landed.count(
                  "/advdeck/projects/" + slug + "/tasks.json") == 1);
  // Files on disk should contain the body the bridge wrote.
  EXPECT_EQ(std::string("# Brief\n\nA brief.\n"),
            s.read_file("/advdeck/projects/" + slug + "/brief.md"));
  EXPECT_EQ(std::string("# Plan\n\nA plan.\n"),
            s.read_file("/advdeck/projects/" + slug + "/plan.md"));
  EXPECT_EQ(std::string("{\"version\":1,\"tasks\":[]}\n"),
            s.read_file("/advdeck/projects/" + slug + "/tasks.json"));
}

void import_error_manifest_returns_ok_false() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::BridgeImport importer(s, "/advdeck");
  std::string err;
  std::string slug = ps.create_project("My Cool Idea", "body", &err);
  EXPECT_EQ(std::string(""), err);
  std::string id = q.enqueue(slug, "plan_project", {"idea.md"}, &err);
  EXPECT_EQ(std::string(""), err);
  // Write an error manifest.
  nlohmann::json manifest = {
      {"request_id", id},
      {"status", "error"},
      {"error_code", "bridge_timeout"},
      {"message", "upstream timeout after 30s"},
      {"retryable", true},
  };
  write_result_manifest(s, id, manifest);
  advdeck::ImportResult result;
  std::string ie = importer.import(id, &result, &err);
  EXPECT_TRUE(!ie.empty());
  EXPECT_TRUE(!result.ok);
  EXPECT_TRUE(result.retryable);
  EXPECT_TRUE(result.error_message.find("upstream timeout") !=
              std::string::npos);
  // The project folder should not have been written into.
  EXPECT_TRUE(!s.exists("/advdeck/projects/" + slug + "/brief.md"));
  // The error manifest's `error_code` is in the allowed enum.
}

void import_missing_result_returns_ok_false() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::BridgeImport importer(s, "/advdeck");
  std::string err;
  std::string slug = ps.create_project("p", "x", &err);
  EXPECT_EQ(std::string(""), err);
  std::string id = q.enqueue(slug, "plan_project", {"idea.md"}, &err);
  // Don't write any result.json — the importer should report a
  // missing-result error.
  advdeck::ImportResult result;
  std::string ie = importer.import(id, &result, &err);
  EXPECT_TRUE(!ie.empty());
  EXPECT_TRUE(!result.ok);
  EXPECT_TRUE(result.error_message.find("missing") != std::string::npos);
}

void import_malformed_manifest_returns_ok_false() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::BridgeImport importer(s, "/advdeck");
  std::string err;
  std::string slug = ps.create_project("p", "x", &err);
  EXPECT_EQ(std::string(""), err);
  std::string id = q.enqueue(slug, "plan_project", {"idea.md"}, &err);
  // Write a malformed manifest. The importer must fail to
  // parse it and report a clear error.
  // Write a manifest with a top-level object but missing the
  // required fields. The importer must reject it.
  write_result_manifest(s, id, nlohmann::json{});
  advdeck::ImportResult result;
  std::string ie = importer.import(id, &result, &err);
  EXPECT_TRUE(!ie.empty());
  EXPECT_TRUE(!result.ok);
  // The error message should mention the validation failure. We
  // accept any of the common shape complaints.
  EXPECT_TRUE(!result.error_message.empty());
  EXPECT_TRUE(
      result.error_message.find("request_id") != std::string::npos ||
      result.error_message.find("status") != std::string::npos ||
      result.error_message.find("expected") != std::string::npos);
}

void import_rejects_unknown_artifact() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::BridgeImport importer(s, "/advdeck");
  std::string err;
  std::string slug = ps.create_project("p", "x", &err);
  EXPECT_EQ(std::string(""), err);
  std::string id = q.enqueue(slug, "plan_project", {"idea.md"}, &err);
  // Manifest lists an artifact that isn't in our allowed set
  // (e.g. "secret.md"). The importer should warn about it
  // and skip.
  write_result_artifact(s, id, "brief.md", "ok");
  write_result_artifact(s, id, "secret.md", "leak");
  nlohmann::json manifest = {
      {"request_id", id},
      {"status", "ok"},
      {"artifacts", nlohmann::json::array({"brief.md", "secret.md"})},
      {"warnings", nlohmann::json::array()},
  };
  write_result_manifest(s, id, manifest);
  advdeck::ImportResult result;
  std::string ie = importer.import(id, &result, &err);
  EXPECT_EQ(std::string(""), ie);
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(std::size_t(1), result.imported_files.size());
  EXPECT_EQ(std::size_t(1), result.warnings.size());
  EXPECT_TRUE(result.warnings[0].find("secret.md") != std::string::npos);
  // The allowed file should still be on disk.
  EXPECT_EQ(std::string("ok"),
            s.read_file("/advdeck/projects/" + slug + "/brief.md"));
  // The forbidden file should not.
  EXPECT_TRUE(!s.exists("/advdeck/projects/" + slug + "/secret.md"));
}

void import_id_mismatch_returns_ok_false() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  advdeck::OutboxQueue q(s, "/advdeck");
  advdeck::BridgeImport importer(s, "/advdeck");
  std::string err;
  std::string slug = ps.create_project("p", "x", &err);
  std::string id = q.enqueue(slug, "plan_project", {"idea.md"}, &err);
  // Manifest claims a different id.
  nlohmann::json manifest = {
      {"request_id", "req-20260101-001"},
      {"status", "ok"},
      {"artifacts", nlohmann::json::array({"brief.md"})},
      {"warnings", nlohmann::json::array()},
  };
  write_result_manifest(s, id, manifest);
  advdeck::ImportResult result;
  std::string ie = importer.import(id, &result, &err);
  EXPECT_TRUE(!ie.empty());
  EXPECT_TRUE(!result.ok);
  EXPECT_TRUE(result.error_message.find("does not match") !=
              std::string::npos);
}

void import_validates_id_format() {
  auto s = make_storage_with_unique_root();
  advdeck::BridgeImport importer(s, "/advdeck");
  advdeck::ImportResult result;
  std::string err;
  std::string e = importer.import("not-a-real-id", &result, &err);
  EXPECT_TRUE(!e.empty());
}

}  // namespace

ADVDECK_REGISTER_TEST(bridge_import_valid_result_copies_artifacts,
                       import_valid_result_copies_artifacts);
ADVDECK_REGISTER_TEST(bridge_import_error_manifest_returns_ok_false,
                       import_error_manifest_returns_ok_false);
ADVDECK_REGISTER_TEST(bridge_import_missing_result_returns_ok_false,
                       import_missing_result_returns_ok_false);
ADVDECK_REGISTER_TEST(bridge_import_malformed_manifest_returns_ok_false,
                       import_malformed_manifest_returns_ok_false);
ADVDECK_REGISTER_TEST(bridge_import_rejects_unknown_artifact,
                       import_rejects_unknown_artifact);
ADVDECK_REGISTER_TEST(bridge_import_id_mismatch_returns_ok_false,
                       import_id_mismatch_returns_ok_false);
ADVDECK_REGISTER_TEST(bridge_import_validates_id_format,
                       import_validates_id_format);
