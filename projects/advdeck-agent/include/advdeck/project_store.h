// advdeck/project_store.h
//
// CRUD over project folders under <root>/projects/<slug>/. idea.md is
// required at create time; everything else (transcript, brief, plan,
// tasks, agent-prompt, export) is generated later by other layers.

#ifndef ADVDECK_INCLUDE_ADVDECK_PROJECT_STORE_H_
#define ADVDECK_INCLUDE_ADVDECK_PROJECT_STORE_H_

#include <string>
#include <vector>

#include "advdeck/storage.h"

namespace advdeck {

struct ProjectSummary {
  std::string slug;
  std::string title;        // derived from first H1 of idea.md, else slug
  std::string idea_path;    // <root>/projects/<slug>/idea.md
  std::string dir_path;     // <root>/projects/<slug>
  std::string created_at;   // ISO8601 (UTC) — derived from dir mtime
  std::string modified_at;  // ISO8601 (UTC) — derived from idea.md mtime
};

class ProjectStore {
 public:
  explicit ProjectStore(IStorage& storage, std::string root = "/advdeck");

  // Create a new project: slug from `title`, ensure the dir, atomic write
  // of `idea_text` to idea.md. Returns the slug used, or "" on failure
  // (and `*err` is set).
  std::string create_project(const std::string& title,
                             const std::string& idea_text,
                             std::string* err);

  // Overwrite idea.md atomically. Returns "" on success.
  std::string write_idea(const std::string& slug,
                         const std::string& idea_text,
                         std::string* err);

  // Read idea.md. Returns the text and "" in `*err` on success; on
  // failure returns "" and sets `*err`.
  std::string read_idea(const std::string& slug, std::string* err);

  // List all projects under <root>/projects/. Returns summaries in
  // unspecified order. Returns an empty vector on success, or sets `*err`
  // on failure (e.g. missing root, permission denied).
  std::vector<ProjectSummary> list_projects(std::string* err);

  // True if a project with `slug` exists.
  bool project_exists(const std::string& slug);

  // Absolute path of a project directory.
  std::string project_dir(const std::string& slug) const;

  // Root directory of all projects (<root>/projects).
  std::string storage_root() const { return storage_->join(root_, "projects"); }

 private:
  IStorage* storage_;
  std::string root_;
};

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_PROJECT_STORE_H_
