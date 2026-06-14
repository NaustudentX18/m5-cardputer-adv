// test/host/test_main.cpp
//
// Tiny harness. test_slug.cpp, test_storage_paths.cpp and
// test_project_store.cpp each use static initializers (in an anonymous
// namespace) to register their test cases with
// ::advdeck::testing::register_test() before main() runs. main() then
// iterates the registry; on first failure per test it prints details
// and returns 1. On success it prints `ALL PASS` (last line).

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#include "advdeck/expect.h"

namespace advdeck {
namespace testing {

// Global test case registry. Populated by static initializers in the
// test_*.cpp files before main() runs.
std::vector<TestEntry>& test_registry() {
  static std::vector<TestEntry> r;
  return r;
}

// Per-test scratch state.
namespace {
bool g_failed = false;
std::string g_message;
}  // namespace

void reset_current() {
  g_failed = false;
  g_message.clear();
}

void fail_current(const std::string& msg) {
  g_failed = true;
  g_message = msg;
}

bool current_failed() { return g_failed; }
const std::string& current_message() { return g_message; }

}  // namespace testing
}  // namespace advdeck

int main() {
  int passed = 0;
  int failed = 0;
  for (const auto& tc : advdeck::testing::test_registry()) {
    std::printf("[ RUN  ] %s\n", tc.name);
    advdeck::testing::reset_current();
    try {
      tc.fn();
    } catch (const std::exception& e) {
      advdeck::testing::fail_current(std::string("uncaught exception: ") +
                                     e.what());
    } catch (...) {
      advdeck::testing::fail_current("uncaught non-std exception");
    }
    if (advdeck::testing::current_failed()) {
      std::printf("[ FAIL ] %s : %s\n", tc.name,
                  advdeck::testing::current_message().c_str());
      ++failed;
    } else {
      std::printf("[  OK  ] %s\n", tc.name);
      ++passed;
    }
  }
  std::printf("----\n");
  std::printf("%d passed, %d failed\n", passed, failed);
  if (failed == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  return 1;
}
