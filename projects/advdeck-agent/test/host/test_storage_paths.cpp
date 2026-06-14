#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include "advdeck/expect.h"
#include "advdeck/storage.h"

namespace fs = std::filesystem;

namespace {

// Each test gets a fresh temp dir; we keep them inside a parent created
// by the first test (or in the system temp if no parent is available).
fs::path make_temp_root() {
  // std::filesystem::temp_directory_path() returns a known per-process
  // path; under it we create a unique subdir per test.
  static std::atomic<unsigned> counter{0};
  fs::path base = fs::temp_directory_path() /
                  ("advdeck_a02_" + std::to_string(::getpid()) + "_" +
                   std::to_string(counter.fetch_add(1)));
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  return base;
}

advdeck::HostStorage fresh_storage() {
  return advdeck::HostStorage(make_temp_root().string());
}

void mount_creates_root() {
  auto root = make_temp_root();
  std::error_code ec;
  fs::remove_all(root, ec);
  EXPECT_TRUE(!fs::exists(root));
  advdeck::HostStorage s(root.string());
  // Constructor attempts to create the root; calling mount() is idempotent.
  EXPECT_TRUE(s.mount());
  EXPECT_TRUE(s.is_mounted());
  EXPECT_TRUE(s.exists(root.string()));
}

void write_then_read_roundtrips() {
  advdeck::HostStorage s = fresh_storage();
  std::string path = s.join(s.root(), "hello.txt");
  std::string e = s.write_file(path, "hello\nworld\n");
  EXPECT_EQ(std::string(""), e);
  EXPECT_TRUE(s.exists(path));
  EXPECT_EQ(std::string("hello\nworld\n"), s.read_file(path));
  EXPECT_EQ(std::string("hello\nworld\n"),
            s.read_file_or(path, "fallback"));
  EXPECT_EQ(std::string("fallback"),
            s.read_file_or(s.join(s.root(), "missing.txt"), "fallback"));
}

void atomic_rename_does_not_leave_tmp() {
  advdeck::HostStorage s = fresh_storage();
  std::string path = s.join(s.root(), "atomic.txt");
  std::string e = s.write_file(path, "data");
  EXPECT_EQ(std::string(""), e);
  EXPECT_TRUE(s.exists(path));
  EXPECT_TRUE(!s.exists(path + ".tmp"));
  // Overwriting should also be clean.
  e = s.write_file(path, "data2");
  EXPECT_EQ(std::string(""), e);
  EXPECT_TRUE(s.exists(path));
  EXPECT_TRUE(!s.exists(path + ".tmp"));
  EXPECT_EQ(std::string("data2"), s.read_file(path));
}

void list_dir_returns_expected_entries() {
  advdeck::HostStorage s = fresh_storage();
  std::string dir = s.join(s.root(), "d");
  EXPECT_EQ(std::string(""), s.ensure_dir(dir));
  EXPECT_EQ(std::string(""), s.write_file(s.join(dir, "a"), "1"));
  EXPECT_EQ(std::string(""), s.write_file(s.join(dir, "b"), "2"));
  EXPECT_EQ(std::string(""), s.write_file(s.join(dir, "c"), "3"));

  std::vector<std::string> entries = s.list_dir(dir);
  std::set<std::string> as_set(entries.begin(), entries.end());
  EXPECT_EQ(std::size_t(3), as_set.size());
  EXPECT_TRUE(as_set.count("a") == 1);
  EXPECT_TRUE(as_set.count("b") == 1);
  EXPECT_TRUE(as_set.count("c") == 1);

  // Sorted: a, b, c.
  EXPECT_EQ(std::string("a"), entries[0]);
  EXPECT_EQ(std::string("b"), entries[1]);
  EXPECT_EQ(std::string("c"), entries[2]);

  // Missing dir -> empty list, not error.
  EXPECT_EQ(std::size_t(0),
            s.list_dir(s.join(s.root(), "no-such-dir")).size());
}

void join_basics() {
  advdeck::HostStorage s = fresh_storage();
  EXPECT_EQ(std::string("a/b"), s.join("a", "b"));
  EXPECT_EQ(std::string("a/b"), s.join("a/", "b"));
  EXPECT_EQ(std::string("b"), s.join("", "b"));
  EXPECT_EQ(std::string("a"), s.join("a", ""));
}

}  // namespace

ADVDECK_REGISTER_TEST(storage_mount_creates_root, mount_creates_root);
ADVDECK_REGISTER_TEST(storage_write_then_read_roundtrips,
                       write_then_read_roundtrips);
ADVDECK_REGISTER_TEST(storage_atomic_rename_does_not_leave_tmp,
                       atomic_rename_does_not_leave_tmp);
ADVDECK_REGISTER_TEST(storage_list_dir_returns_expected_entries,
                       list_dir_returns_expected_entries);
ADVDECK_REGISTER_TEST(storage_join_basics, join_basics);
