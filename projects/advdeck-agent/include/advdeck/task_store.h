// advdeck/task_store.h
//
// CRUD over <project_dir>/tasks.json. The store owns no per-task
// state in memory beyond what callers pass in: load() returns a
// vector, callers mutate it, save() writes it back. add_task /
// set_status / delete_task are convenience helpers that do the
// load+mutate+save dance for the common case.

#ifndef ADVDECK_INCLUDE_ADVDECK_TASK_STORE_H_
#define ADVDECK_INCLUDE_ADVDECK_TASK_STORE_H_

#include <string>
#include <vector>

#include "advdeck/storage.h"

namespace advdeck {

struct Task {
  std::string id;                       // "tsk-<8hex>"
  std::string title;
  std::string objective;
  std::string context;
  std::vector<std::string> files_or_modules;
  std::vector<std::string> acceptance_criteria;
  std::vector<std::string> validation;
  std::vector<std::string> dependencies;
  std::string risk;
  std::string suggested_agent_role;
  std::string status;                   // "todo" | "doing" | "done"
  std::string created_at;               // ISO8601 UTC
  std::string updated_at;               // ISO8601 UTC
};

class TaskStore {
 public:
  TaskStore(IStorage& storage, std::string project_dir);

  // File: <project_dir>/tasks.json
  // Schema: top-level { "version": 1, "tasks": [ Task, ... ] }
  // Atomic write. Malformed JSON returns a recoverable error and the
  // bad file is moved aside to <project_dir>/tasks.json.bad-<ts>.

  std::string load(std::vector<Task>* out, std::string* err);
  std::string save(const std::vector<Task>& tasks, std::string* err);

  // CRUD helpers
  std::string add_task(const std::string& title, Task* out_added,
                       std::string* err);
  std::string set_status(const std::string& id, const std::string& status,
                         std::string* err);
  std::string delete_task(const std::string& id, std::string* err);

  // Export
  std::string export_markdown(const std::vector<Task>& tasks);

 private:
  IStorage* storage_;
  std::string project_dir_;
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_TASK_STORE_H_
