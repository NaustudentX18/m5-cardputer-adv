// test/host/test_task_store.cpp
//
// Host tests for advdeck::TaskStore. Mirrors the patterns in
// test_project_store.cpp: each test builds a fresh HostStorage
// rooted at a unique temp dir, exercises a slice of the public API,
// and asserts on the on-disk state via std::filesystem.

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "advdeck/expect.h"
#include "advdeck/storage.h"
#include "advdeck/task_store.h"

namespace fs = std::filesystem;

namespace {

advdeck::HostStorage make_storage_with_unique_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_a04_ts_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return advdeck::HostStorage(base.string());
}

// Build a TaskStore rooted at the logical SD path
//   /advdeck/projects/<slug>/
// HostStorage strips the leading slash, so the file lands at
// <host_temp>/advdeck/projects/<slug>/tasks.json — same shape as
// ProjectStore uses for idea.md.
std::unique_ptr<advdeck::TaskStore> make_store(advdeck::HostStorage& s,
                                               const std::string& slug) {
  std::string dir = s.join("/advdeck/projects", slug);
  std::string e = s.ensure_dir(dir);
  if (!e.empty()) return nullptr;
  return std::unique_ptr<advdeck::TaskStore>(
      new advdeck::TaskStore(s, dir));
}

void empty_load_on_missing_file() {
  auto s = make_storage_with_unique_root();
  auto store = make_store(s, "missing");
  EXPECT_TRUE(store != nullptr);
  std::vector<advdeck::Task> tasks;
  std::string err;
  std::string rc = store->load(&tasks, &err);
  EXPECT_EQ(std::string(""), rc);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::size_t(0), tasks.size());
}

void add_task_persists() {
  auto s = make_storage_with_unique_root();
  auto store = make_store(s, "alpha");
  std::string err;
  advdeck::Task added;
  std::string rc = store->add_task("Wire up the dashboard", &added, &err);
  EXPECT_EQ(std::string(""), rc);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::string("Wire up the dashboard"), added.title);
  EXPECT_EQ(std::string("todo"), added.status);
  EXPECT_TRUE(!added.id.empty());
  EXPECT_TRUE(!added.created_at.empty());
  EXPECT_EQ(added.created_at, added.updated_at);

  // On disk: tasks.json exists under the project dir.
  fs::path project_dir = fs::path(s.root()) / "advdeck" / "projects" / "alpha";
  fs::path tasks_json = project_dir / "tasks.json";
  EXPECT_TRUE(fs::is_regular_file(tasks_json));
  // It must NOT have left a .tmp behind.
  EXPECT_TRUE(!fs::exists(project_dir / "tasks.json.tmp"));
}

void add_task_assigns_unique_id() {
  auto s = make_storage_with_unique_root();
  auto store = make_store(s, "idtest");
  std::string err;
  advdeck::Task a, b, c;
  EXPECT_EQ(std::string(""), store->add_task("A", &a, &err));
  EXPECT_EQ(std::string(""), store->add_task("B", &b, &err));
  EXPECT_EQ(std::string(""), store->add_task("C", &c, &err));

  // Format check: "tsk-" + 8 lowercase hex chars.
  const std::regex id_re("^tsk-[0-9a-f]{8}$");
  EXPECT_TRUE(std::regex_match(a.id, id_re));
  EXPECT_TRUE(std::regex_match(b.id, id_re));
  EXPECT_TRUE(std::regex_match(c.id, id_re));

  // All three ids must be distinct.
  std::set<std::string> ids{a.id, b.id, c.id};
  EXPECT_EQ(std::size_t(3), ids.size());
}

void add_then_load_returns_same_task() {
  auto s = make_storage_with_unique_root();
  auto store = make_store(s, "roundtrip");
  std::string err;
  advdeck::Task added;
  EXPECT_EQ(std::string(""), store->add_task("Persisted", &added, &err));
  EXPECT_EQ(std::string(""), err);

  std::vector<advdeck::Task> tasks;
  EXPECT_EQ(std::string(""), store->load(&tasks, &err));
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::size_t(1), tasks.size());
  EXPECT_EQ(added.id, tasks[0].id);
  EXPECT_EQ(std::string("Persisted"), tasks[0].title);
  EXPECT_EQ(std::string("todo"), tasks[0].status);
  EXPECT_EQ(added.created_at, tasks[0].created_at);
  EXPECT_EQ(added.updated_at, tasks[0].updated_at);
}

void set_status_doing_then_done() {
  auto s = make_storage_with_unique_root();
  auto store = make_store(s, "status");
  std::string err;
  advdeck::Task added;
  EXPECT_EQ(std::string(""), store->add_task("Move it", &added, &err));

  // doing
  EXPECT_EQ(std::string(""), store->set_status(added.id, "doing", &err));
  EXPECT_EQ(std::string(""), err);
  std::vector<advdeck::Task> tasks;
  EXPECT_EQ(std::string(""), store->load(&tasks, &err));
  EXPECT_EQ(std::size_t(1), tasks.size());
  EXPECT_EQ(std::string("doing"), tasks[0].status);
  EXPECT_TRUE(tasks[0].updated_at >= added.updated_at);

  // done
  EXPECT_EQ(std::string(""), store->set_status(added.id, "done", &err));
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::string(""), store->load(&tasks, &err));
  EXPECT_EQ(std::size_t(1), tasks.size());
  EXPECT_EQ(std::string("done"), tasks[0].status);

  // unknown id
  std::string rc = store->set_status("tsk-deadbeef", "doing", &err);
  EXPECT_TRUE(!rc.empty());
  EXPECT_EQ(std::string("task not found"), rc);
}

void delete_task_removes() {
  auto s = make_storage_with_unique_root();
  auto store = make_store(s, "del");
  std::string err;
  advdeck::Task a, b;
  EXPECT_EQ(std::string(""), store->add_task("A", &a, &err));
  EXPECT_EQ(std::string(""), store->add_task("B", &b, &err));

  std::vector<advdeck::Task> tasks;
  EXPECT_EQ(std::string(""), store->load(&tasks, &err));
  EXPECT_EQ(std::size_t(2), tasks.size());

  EXPECT_EQ(std::string(""), store->delete_task(a.id, &err));
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::string(""), store->load(&tasks, &err));
  EXPECT_EQ(std::size_t(1), tasks.size());
  EXPECT_EQ(b.id, tasks[0].id);

  // Deleting again is an error.
  std::string rc = store->delete_task(a.id, &err);
  EXPECT_EQ(std::string("task not found"), rc);
  EXPECT_EQ(std::string("task not found"), err);
}

void malformed_json_recovered_gracefully() {
  auto s = make_storage_with_unique_root();
  auto store = make_store(s, "broken");
  fs::path project_dir = fs::path(s.root()) / "advdeck" / "projects" / "broken";
  fs::path tasks_json = project_dir / "tasks.json";
  // Drop a file that is not valid JSON.
  {
    std::ofstream out(tasks_json);
    out << "{ this is not json ";
  }

  std::vector<advdeck::Task> tasks;
  std::string err;
  std::string rc = store->load(&tasks, &err);
  EXPECT_TRUE(!rc.empty());
  EXPECT_TRUE(!err.empty());

  // The bad file has been moved aside; the original path now points
  // at an empty (recoverable) file. There must be a tasks.json.bad-*
  // sibling we can inspect.
  EXPECT_TRUE(fs::is_regular_file(tasks_json));
  std::error_code ec;
  bool found_aside = false;
  for (const auto& entry : fs::directory_iterator(project_dir, ec)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("tasks.json.bad-", 0) == 0) {
      found_aside = true;
      // The aside should still contain the original malformed text.
      std::ifstream in(entry.path());
      std::stringstream ss;
      ss << in.rdbuf();
      EXPECT_TRUE(ss.str().find("this is not json") != std::string::npos);
      break;
    }
  }
  EXPECT_TRUE(found_aside);

  // A subsequent load sees an empty list (recovered).
  err.clear();
  EXPECT_EQ(std::string(""), store->load(&tasks, &err));
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::size_t(0), tasks.size());
}

void export_markdown_basic() {
  auto s = make_storage_with_unique_root();
  auto store = make_store(s, "exp");
  std::string err;
  advdeck::Task t1, t2, t3;
  EXPECT_EQ(std::string(""), store->add_task("First", &t1, &err));
  EXPECT_EQ(std::string(""), store->add_task("Second", &t2, &err));
  EXPECT_EQ(std::string(""), store->add_task("Third", &t3, &err));
  EXPECT_EQ(std::string(""), store->set_status(t2.id, "doing", &err));
  EXPECT_EQ(std::string(""), store->set_status(t3.id, "done", &err));

  std::vector<advdeck::Task> tasks;
  EXPECT_EQ(std::string(""), store->load(&tasks, &err));
  std::string md = store->export_markdown(tasks);
  EXPECT_EQ(std::string("# Tasks\n\n- [ ] " + t1.id + ": First\n- [ ] " + t2.id +
                            ": Second\n- [x] " + t3.id + ": Third\n"),
            md);
}

void atomic_write_leaves_no_tmp() {
  auto s = make_storage_with_unique_root();
  auto store = make_store(s, "atomic");
  std::string err;
  advdeck::Task added;
  EXPECT_EQ(std::string(""), store->add_task("Atomic", &added, &err));
  fs::path project_dir = fs::path(s.root()) / "advdeck" / "projects" / "atomic";
  EXPECT_TRUE(!fs::exists(project_dir / "tasks.json.tmp"));

  // Several rounds of save to make sure each one is clean.
  for (int i = 0; i < 3; ++i) {
    std::vector<advdeck::Task> tasks;
    EXPECT_EQ(std::string(""), store->load(&tasks, &err));
    EXPECT_EQ(std::string(""), store->save(tasks, &err));
    EXPECT_TRUE(!fs::exists(project_dir / "tasks.json.tmp"));
  }
}

void schema_version_and_tasks_array() {
  auto s = make_storage_with_unique_root();
  auto store = make_store(s, "schema");
  std::string err;
  advdeck::Task added;
  EXPECT_EQ(std::string(""), store->add_task("Hello", &added, &err));

  fs::path tasks_json = fs::path(s.root()) / "advdeck" / "projects" / "schema" /
                        "tasks.json";
  std::ifstream in(tasks_json);
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string body = ss.str();

  // Two cheap invariants that survive pretty-printing: the file
  // contains the version literal and the tasks array opener.
  EXPECT_TRUE(body.find("\"version\": 1") != std::string::npos);
  EXPECT_TRUE(body.find("\"tasks\":") != std::string::npos);
  EXPECT_TRUE(body.find("\"tsk-") != std::string::npos);
}

}  // namespace

ADVDECK_REGISTER_TEST(task_store_empty_load_on_missing_file,
                       empty_load_on_missing_file);
ADVDECK_REGISTER_TEST(task_store_add_task_persists, add_task_persists);
ADVDECK_REGISTER_TEST(task_store_add_task_assigns_unique_id,
                       add_task_assigns_unique_id);
ADVDECK_REGISTER_TEST(task_store_add_then_load_returns_same_task,
                       add_then_load_returns_same_task);
ADVDECK_REGISTER_TEST(task_store_set_status_doing_then_done,
                       set_status_doing_then_done);
ADVDECK_REGISTER_TEST(task_store_delete_task_removes,
                       delete_task_removes);
ADVDECK_REGISTER_TEST(task_store_malformed_json_recovered_gracefully,
                       malformed_json_recovered_gracefully);
ADVDECK_REGISTER_TEST(task_store_export_markdown_basic,
                       export_markdown_basic);
ADVDECK_REGISTER_TEST(task_store_atomic_write_leaves_no_tmp,
                       atomic_write_leaves_no_tmp);
ADVDECK_REGISTER_TEST(task_store_schema_version_and_tasks_array,
                       schema_version_and_tasks_array);
