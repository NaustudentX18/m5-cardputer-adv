// test/host/test_project_store.cpp

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "advdeck/expect.h"
#include "advdeck/project_store.h"
#include "advdeck/storage.h"

namespace fs = std::filesystem;

namespace {

advdeck::HostStorage make_storage_with_unique_root() {
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_a02_ps_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return advdeck::HostStorage(base.string());
}

void create_project_writes_idea_and_creates_dir() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  std::string err;
  std::string slug = ps.create_project("My Cool Idea", "# Title\n\nbody\n",
                                       &err);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::string("my-cool-idea"), slug);
  EXPECT_TRUE(ps.project_exists(slug));
  // Verify the on-disk layout directly via std::filesystem.
  fs::path dir = fs::path(s.root()) / "advdeck" / "projects" / slug;
  EXPECT_TRUE(fs::is_directory(dir));
  fs::path idea = dir / "idea.md";
  EXPECT_TRUE(fs::is_regular_file(idea));
  // And read back via the storage API using a logical SD-style path.
  std::string logical = "/advdeck/projects/" + slug + "/idea.md";
  std::string contents = s.read_file(logical);
  EXPECT_EQ(std::string("# Title\n\nbody\n"), contents);
}

void list_projects_returns_created() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  std::string err;
  ps.create_project("Alpha", "alpha body", &err);
  EXPECT_EQ(std::string(""), err);
  ps.create_project("Beta Project", "beta body", &err);
  EXPECT_EQ(std::string(""), err);
  auto list = ps.list_projects(&err);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::size_t(2), list.size());
  std::set<std::string> slugs;
  for (const auto& p : list) slugs.insert(p.slug);
  EXPECT_TRUE(slugs.count("alpha") == 1);
  EXPECT_TRUE(slugs.count("beta-project") == 1);
  // Title derived from first H1 of idea.md, else slug. Both projects
  // have no H1, so title == slug for each.
  for (const auto& p : list) {
    EXPECT_EQ(p.slug, p.title);
  }
}

void list_projects_uses_h1_for_title() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  std::string err;
  ps.create_project("gamma", "# Gamma Project\n\nbody", &err);
  EXPECT_EQ(std::string(""), err);
  auto list = ps.list_projects(&err);
  EXPECT_EQ(std::size_t(1), list.size());
  EXPECT_EQ(std::string("gamma"), list[0].slug);
  EXPECT_EQ(std::string("Gamma Project"), list[0].title);
  // idea_path / dir_path match the logical SD layout from the contract.
  EXPECT_EQ(std::string("/advdeck/projects/gamma/idea.md"),
            list[0].idea_path);
  EXPECT_EQ(std::string("/advdeck/projects/gamma"), list[0].dir_path);
  EXPECT_TRUE(!list[0].created_at.empty());
  EXPECT_TRUE(!list[0].modified_at.empty());
}

void slug_collision_adds_dash_two() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  std::string err;
  std::string a = ps.create_project("My Cool Idea", "one", &err);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::string("my-cool-idea"), a);
  std::string b = ps.create_project("My Cool Idea", "two", &err);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::string("my-cool-idea-2"), b);
  std::string c = ps.create_project("My Cool Idea", "three", &err);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::string("my-cool-idea-3"), c);
  // All three exist.
  EXPECT_TRUE(ps.project_exists("my-cool-idea"));
  EXPECT_TRUE(ps.project_exists("my-cool-idea-2"));
  EXPECT_TRUE(ps.project_exists("my-cool-idea-3"));
  // Original idea.md untouched.
  EXPECT_EQ(std::string("one"),
            s.read_file("/advdeck/projects/my-cool-idea/idea.md"));
  EXPECT_EQ(std::string("two"),
            s.read_file("/advdeck/projects/my-cool-idea-2/idea.md"));
}

void write_idea_overwrites_idea_md_without_touching_others() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  std::string err;
  std::string slug = ps.create_project("Test", "first", &err);
  EXPECT_EQ(std::string(""), err);
  // Drop a sibling file in the project dir to make sure write_idea
  // doesn't clobber it.
  std::string sibling = "/advdeck/projects/" + slug + "/note.md";
  EXPECT_EQ(std::string(""), s.write_file(sibling, "keep me"));
  std::string out = ps.write_idea(slug, "second", &err);
  EXPECT_EQ(std::string(""), out);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::string("second"), ps.read_idea(slug, &err));
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::string("keep me"), s.read_file(sibling));
}

void read_idea_returns_original_text() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  std::string err;
  ps.create_project("readme-test", "line1\nline2\n", &err);
  EXPECT_EQ(std::string(""), err);
  std::string text = ps.read_idea("readme-test", &err);
  EXPECT_EQ(std::string(""), err);
  EXPECT_EQ(std::string("line1\nline2\n"), text);
}

void project_exists_correct() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  EXPECT_TRUE(!ps.project_exists("nope"));
  std::string err;
  ps.create_project("exists", "x", &err);
  EXPECT_TRUE(ps.project_exists("exists"));
  EXPECT_TRUE(!ps.project_exists(""));
}

void storage_root_layout() {
  auto s = make_storage_with_unique_root();
  advdeck::ProjectStore ps(s, "/advdeck");
  EXPECT_EQ(std::string("/advdeck/projects"), ps.storage_root());
  EXPECT_EQ(std::string("/advdeck/projects/foo"), ps.project_dir("foo"));
}

}  // namespace

ADVDECK_REGISTER_TEST(project_store_create_project_writes_idea_and_creates_dir,
                       create_project_writes_idea_and_creates_dir);
ADVDECK_REGISTER_TEST(project_store_list_projects_returns_created,
                       list_projects_returns_created);
ADVDECK_REGISTER_TEST(project_store_list_projects_uses_h1_for_title,
                       list_projects_uses_h1_for_title);
ADVDECK_REGISTER_TEST(project_store_slug_collision_adds_dash_two,
                       slug_collision_adds_dash_two);
ADVDECK_REGISTER_TEST(
    project_store_write_idea_overwrites_idea_md_without_touching_others,
    write_idea_overwrites_idea_md_without_touching_others);
ADVDECK_REGISTER_TEST(project_store_read_idea_returns_original_text,
                       read_idea_returns_original_text);
ADVDECK_REGISTER_TEST(project_store_project_exists_correct,
                       project_exists_correct);
ADVDECK_REGISTER_TEST(project_store_storage_root_layout,
                       storage_root_layout);
