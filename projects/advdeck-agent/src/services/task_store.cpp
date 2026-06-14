// src/services/task_store.cpp
//
// CRUD over <project_dir>/tasks.json. The JSON schema is
//   { "version": 1, "tasks": [ Task, ... ] }
// with `Task` serialized field-by-field. The store is intentionally
// stateless between calls: load() returns a vector, callers mutate it,
// save() writes it back. The add_task / set_status / delete_task
// helpers just do the load+mutate+save dance for the common case.

#include "advdeck/task_store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace advdeck {
namespace {

constexpr const char* kFileName = "tasks.json";
constexpr int kSchemaVersion = 1;

// ISO8601 UTC "now" — second resolution. Same shape as ProjectStore
// uses, kept here to avoid a header dependency.
std::string now_iso8601_utc() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

// ISO8601 UTC timestamp suitable for a "bad file" suffix, with
// filesystem-safe characters. Uses '-' instead of ':' to keep the
// move-aside filename portable across more filesystems.
std::string bad_file_suffix() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm);
  return std::string(buf);
}

// "tsk-" + 8 lowercase hex chars. Seeded from std::random_device so
// the sequence is different per process; mt19937_64 gives us enough
// bits for 8 hex chars (32 bits) without bias.
std::string new_task_id() {
  static thread_local std::mt19937_64 rng([] {
    std::random_device rd;
    // Combine two draws in case the implementation is a thin shim.
    std::seed_seq seq{rd(), rd(), rd(), rd(),
                      static_cast<std::uint32_t>(std::time(nullptr))};
    std::mt19937_64 r(seq);
    return r;
  }());
  std::uint64_t v = rng();
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%08llx",
                static_cast<unsigned long long>(v & 0xFFFFFFFFULL));
  return std::string("tsk-") + buf;
}

// JSON <-> Task field-by-field. We do not store the struct directly;
// using a nlohmann::json gives us resilient parsing (missing or
// extra fields don't blow up load) and stable output ordering on
// save. The nlohmann::json library is single-header and lives in
// third_party/nlohmann/.

nlohmann::json task_to_json(const Task& t) {
  nlohmann::json j;
  j["id"] = t.id;
  j["title"] = t.title;
  j["objective"] = t.objective;
  j["context"] = t.context;
  j["files_or_modules"] = t.files_or_modules;
  j["acceptance_criteria"] = t.acceptance_criteria;
  j["validation"] = t.validation;
  j["dependencies"] = t.dependencies;
  j["risk"] = t.risk;
  j["suggested_agent_role"] = t.suggested_agent_role;
  j["status"] = t.status;
  j["created_at"] = t.created_at;
  j["updated_at"] = t.updated_at;
  return j;
}

Task task_from_json(const nlohmann::json& j) {
  Task t;
  if (j.contains("id") && j["id"].is_string()) t.id = j["id"].get<std::string>();
  if (j.contains("title") && j["title"].is_string())
    t.title = j["title"].get<std::string>();
  if (j.contains("objective") && j["objective"].is_string())
    t.objective = j["objective"].get<std::string>();
  if (j.contains("context") && j["context"].is_string())
    t.context = j["context"].get<std::string>();
  if (j.contains("files_or_modules") && j["files_or_modules"].is_array()) {
    for (const auto& s : j["files_or_modules"]) {
      if (s.is_string()) t.files_or_modules.push_back(s.get<std::string>());
    }
  }
  if (j.contains("acceptance_criteria") &&
      j["acceptance_criteria"].is_array()) {
    for (const auto& s : j["acceptance_criteria"]) {
      if (s.is_string()) t.acceptance_criteria.push_back(s.get<std::string>());
    }
  }
  if (j.contains("validation") && j["validation"].is_array()) {
    for (const auto& s : j["validation"]) {
      if (s.is_string()) t.validation.push_back(s.get<std::string>());
    }
  }
  if (j.contains("dependencies") && j["dependencies"].is_array()) {
    for (const auto& s : j["dependencies"]) {
      if (s.is_string()) t.dependencies.push_back(s.get<std::string>());
    }
  }
  if (j.contains("risk") && j["risk"].is_string())
    t.risk = j["risk"].get<std::string>();
  if (j.contains("suggested_agent_role") &&
      j["suggested_agent_role"].is_string()) {
    t.suggested_agent_role =
        j["suggested_agent_role"].get<std::string>();
  }
  if (j.contains("status") && j["status"].is_string())
    t.status = j["status"].get<std::string>();
  if (j.contains("created_at") && j["created_at"].is_string())
    t.created_at = j["created_at"].get<std::string>();
  if (j.contains("updated_at") && j["updated_at"].is_string())
    t.updated_at = j["updated_at"].get<std::string>();
  return t;
}

// Best-effort move-aside of a malformed tasks.json. Returns "" on
// success, error message on failure. The storage interface has no
// rename primitive, so we use read+write_file (which is itself
// atomic for the new file) and then "delete" the original by
// overwriting it with the empty string when possible. We do the
// rename through a read-then-write fallback: write the new file,
// then overwrite the original with "" (host storage treats missing
// and empty equivalently for our purposes — the next save() will
// just write a fresh file).
//
// We deliberately keep the bad file content rather than deleting it:
// the user can inspect it and recover data by hand.
std::string move_bad_file_aside(IStorage& storage, const std::string& path) {
  std::string content = storage.read_file(path);
  if (content.empty()) {
    // Already empty or unreadable: nothing to preserve, just leave
    // the path in place. The next save() will overwrite.
    return "";
  }
  std::string aside = path + ".bad-" + bad_file_suffix();
  std::string e = storage.write_file(aside, content);
  if (!e.empty()) return "could not write bad-file aside: " + e;
  // Truncate the original so the next load() sees an empty file.
  e = storage.write_file(path, "");
  if (!e.empty()) {
    // Not fatal: the next load() will fail again and re-attempt
    // moving aside, but the new aside path will have a fresh
    // timestamp so the user can still recover.
    return "";
  }
  return "";
}

}  // namespace

TaskStore::TaskStore(IStorage& storage, std::string project_dir)
    : storage_(&storage), project_dir_(std::move(project_dir)) {}

std::string TaskStore::load(std::vector<Task>* out, std::string* err) {
  if (out) out->clear();
  const std::string path = storage_->join(project_dir_, kFileName);
  if (!storage_->exists(path)) {
    if (err) *err = "";
    return "";
  }
  std::string raw = storage_->read_file_or(path, "");
  if (raw.empty()) {
    // File exists but is empty. Treat as no tasks, not as an error:
    // it's a valid (if sparse) state.
    if (err) *err = "";
    return "";
  }
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(raw);
  } catch (const std::exception& ex) {
    // Malformed JSON: move the file aside so the user can inspect it
    // and the next load() can succeed with an empty list.
    std::string aside_err = move_bad_file_aside(*storage_, path);
    std::string msg = std::string("tasks.json: malformed JSON: ") + ex.what();
    if (!aside_err.empty()) {
      msg += " (also: " + aside_err + ")";
    }
    if (err) *err = msg;
    return msg;
  }
  if (!j.is_object() || !j.contains("tasks") || !j["tasks"].is_array()) {
    std::string msg =
        "tasks.json: expected top-level object with a 'tasks' array";
    std::string aside_err = move_bad_file_aside(*storage_, path);
    if (!aside_err.empty()) {
      msg += " (also: " + aside_err + ")";
    }
    if (err) *err = msg;
    return msg;
  }
  if (out) {
    out->reserve(j["tasks"].size());
    for (const auto& item : j["tasks"]) {
      if (item.is_object()) {
        out->push_back(task_from_json(item));
      }
    }
  }
  if (err) *err = "";
  return "";
}

std::string TaskStore::save(const std::vector<Task>& tasks,
                            std::string* err) {
  nlohmann::json j;
  j["version"] = kSchemaVersion;
  j["tasks"] = nlohmann::json::array();
  for (const auto& t : tasks) {
    j["tasks"].push_back(task_to_json(t));
  }
  // Pretty-print with 2 spaces: human-inspectable on the SD card
  // and small enough for the Cardputer's use case.
  std::string body = j.dump(2);
  body.push_back('\n');
  const std::string path = storage_->join(project_dir_, kFileName);
  std::string e = storage_->ensure_dir(project_dir_);
  if (!e.empty()) {
    if (err) *err = "ensure_dir(" + project_dir_ + "): " + e;
    return e;
  }
  e = storage_->write_file(path, body);
  if (!e.empty()) {
    if (err) *err = "write_file(" + path + "): " + e;
    return e;
  }
  if (err) *err = "";
  return "";
}

std::string TaskStore::add_task(const std::string& title, Task* out_added,
                                std::string* err) {
  std::vector<Task> tasks;
  std::string load_err;
  std::string e = load(&tasks, &load_err);
  if (!load_err.empty() && !tasks.empty()) {
    // load failed AND didn't recover to an empty list: surface it.
    if (err) *err = load_err;
    return load_err;
  }
  // If load returned an error but with an empty list (recovery moved
  // the bad file aside), continue — we are starting from a clean
  // slate. If the user wants to keep the old data they can rename
  // the .bad file back.
  Task t;
  t.id = new_task_id();
  t.title = title;
  t.status = "todo";
  t.created_at = now_iso8601_utc();
  t.updated_at = t.created_at;
  tasks.push_back(t);
  std::string save_err;
  e = save(tasks, &save_err);
  if (!save_err.empty()) {
    if (err) *err = save_err;
    return save_err;
  }
  if (out_added) *out_added = t;
  if (err) *err = "";
  return "";
}

std::string TaskStore::set_status(const std::string& id,
                                  const std::string& status,
                                  std::string* err) {
  std::vector<Task> tasks;
  std::string load_err;
  std::string e = load(&tasks, &load_err);
  if (!load_err.empty() && !tasks.empty()) {
    if (err) *err = load_err;
    return load_err;
  }
  bool found = false;
  for (auto& t : tasks) {
    if (t.id == id) {
      t.status = status;
      t.updated_at = now_iso8601_utc();
      found = true;
      break;
    }
  }
  if (!found) {
    if (err) *err = "task not found";
    return "task not found";
  }
  std::string save_err;
  e = save(tasks, &save_err);
  if (!save_err.empty()) {
    if (err) *err = save_err;
    return save_err;
  }
  if (err) *err = "";
  return "";
}

std::string TaskStore::delete_task(const std::string& id, std::string* err) {
  std::vector<Task> tasks;
  std::string load_err;
  std::string e = load(&tasks, &load_err);
  if (!load_err.empty() && !tasks.empty()) {
    if (err) *err = load_err;
    return load_err;
  }
  std::size_t before = tasks.size();
  tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                              [&](const Task& t) { return t.id == id; }),
              tasks.end());
  if (tasks.size() == before) {
    if (err) *err = "task not found";
    return "task not found";
  }
  std::string save_err;
  e = save(tasks, &save_err);
  if (!save_err.empty()) {
    if (err) *err = save_err;
    return save_err;
  }
  if (err) *err = "";
  return "";
}

std::string TaskStore::export_markdown(const std::vector<Task>& tasks) {
  std::ostringstream os;
  os << "# Tasks\n\n";
  for (const auto& t : tasks) {
    const char* box = (t.status == "done") ? "x" : " ";
    os << "- [" << box << "] " << t.id << ": " << t.title << "\n";
  }
  return os.str();
}

}  // namespace advdeck
