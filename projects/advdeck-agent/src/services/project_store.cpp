// src/services/project_store.cpp
//
// CRUD over <root>/projects/<slug>/idea.md. Slug is taken from the title
// and made unique against the on-disk project list. idea.md is the only
// required file at create time.

#include "advdeck/project_store.h"
#include "advdeck/slug.h"

#include <ctime>
#include <sstream>
#include <string>

namespace advdeck {
namespace {

constexpr const char* kIdeaFile = "idea.md";
constexpr const char* kH1Prefix = "# ";

// ISO8601 UTC "now" used as the created_at fallback when the
// directory mtime is unavailable.
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

// Predicate for make_unique_slug: does a project with this slug exist?
bool project_exists_predicate(const std::string& slug, void* ctx) {
  auto* store = static_cast<ProjectStore*>(ctx);
  return store->project_exists(slug);
}

// Extract the first markdown H1 ("# Title") from idea text. Returns
// "" if no H1 is found.
std::string extract_h1(const std::string& text) {
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.compare(0, 2, kH1Prefix) == 0) {
      std::string title = line.substr(2);
      std::size_t i = 0;
      while (i < title.size() &&
             (title[i] == ' ' || title[i] == '\t')) {
        ++i;
      }
      return title.substr(i);
    }
  }
  return "";
}

}  // namespace

ProjectStore::ProjectStore(IStorage& storage, std::string root)
    : storage_(&storage), root_(std::move(root)) {}

std::string ProjectStore::create_project(const std::string& title,
                                         const std::string& idea_text,
                                         std::string* err) {
  std::string slug = make_unique_slug(title, &project_exists_predicate, this);
  if (slug.empty()) {
    if (err) *err = "slugify produced empty slug";
    return "";
  }

  std::string dir = project_dir(slug);
  std::string e = storage_->ensure_dir(dir);
  if (!e.empty()) {
    if (err) *err = "ensure_dir(" + dir + "): " + e;
    return "";
  }

  std::string idea_path = storage_->join(dir, kIdeaFile);
  e = storage_->write_file(idea_path, idea_text);
  if (!e.empty()) {
    if (err) *err = "write_file(" + idea_path + "): " + e;
    return "";
  }
  return slug;
}

std::string ProjectStore::write_idea(const std::string& slug,
                                    const std::string& idea_text,
                                    std::string* err) {
  if (!project_exists(slug)) {
    if (err) *err = "project not found: " + slug;
    return "project not found";
  }
  std::string idea_path = storage_->join(project_dir(slug), kIdeaFile);
  return storage_->write_file(idea_path, idea_text);
}

std::string ProjectStore::read_idea(const std::string& slug,
                                   std::string* err) {
  if (!project_exists(slug)) {
    if (err) *err = "project not found: " + slug;
    return "";
  }
  std::string idea_path = storage_->join(project_dir(slug), kIdeaFile);
  std::string text = storage_->read_file(idea_path);
  if (text.empty() && !storage_->exists(idea_path)) {
    if (err) *err = "idea.md missing in project: " + slug;
    return "";
  }
  return text;
}

std::vector<ProjectSummary> ProjectStore::list_projects(std::string* err) {
  std::vector<ProjectSummary> out;
  std::vector<std::string> names = storage_->list_dir(storage_root());
  for (const auto& name : names) {
    ProjectSummary s;
    s.slug = name;
    s.dir_path = project_dir(name);
    s.idea_path = storage_->join(s.dir_path, kIdeaFile);

    // idea.md mtime -> modified_at; dir mtime -> created_at fallback.
    s.modified_at = storage_->mtime_iso8601(s.idea_path);
    s.created_at = storage_->mtime_iso8601(s.dir_path);
    if (s.created_at.empty()) s.created_at = now_iso8601_utc();

    // Title: first H1 of idea.md, else slug.
    std::string text = storage_->read_file(s.idea_path);
    std::string title = extract_h1(text);
    s.title = title.empty() ? name : title;

    out.push_back(std::move(s));
  }
  if (err) err->clear();
  return out;
}

bool ProjectStore::project_exists(const std::string& slug) {
  if (slug.empty()) return false;
  std::string dir = project_dir(slug);
  return storage_->exists(dir);
}

std::string ProjectStore::project_dir(const std::string& slug) const {
  return storage_->join(storage_root(), slug);
}

}  // namespace advdeck
