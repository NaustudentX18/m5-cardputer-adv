// advdeck/expect.h
//
// Minimal test helpers shared by host tests. NOT used by firmware.
//
// EXPECT_EQ(a, b) records a failure with a human-readable diff and
// returns from the current test function. On success it is a no-op.
//
// Test cases register themselves via ADVDECK_REGISTER_TEST(name, fn)
// from a static initializer in their TU; the harness discovers them
// when it iterates the global registry.

#ifndef ADVDECK_INCLUDE_ADVDECK_EXPECT_H_
#define ADVDECK_INCLUDE_ADVDECK_EXPECT_H_

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace advdeck {
namespace testing {

struct TestEntry {
  const char* name;
  void (*fn)();
};

std::vector<TestEntry>& test_registry();

// Per-test state (set up by the harness before each test runs).
void reset_current();
void fail_current(const std::string& msg);
bool current_failed();
const std::string& current_message();

// Register a test case. test_*.cpp files invoke this from a static
// initializer so the harness can find them by the time main() runs.
inline void register_test(const char* name, void (*fn)()) {
  test_registry().push_back({name, fn});
}

}  // namespace testing
}  // namespace advdeck

// Auto-register a test case from an anonymous-namespace static
// initializer. Use inside an anonymous namespace block at file scope.
#define ADVDECK_REGISTER_TEST(test_name, fn)                         \
  namespace {                                                        \
  struct _AdvdeckReg_##test_name {                                   \
    _AdvdeckReg_##test_name() {                                      \
      ::advdeck::testing::register_test(#test_name, &fn);            \
    }                                                                \
  };                                                                 \
  static _AdvdeckReg_##test_name _advdeck_reg_instance_##test_name;  \
  }

namespace advdeck {
namespace testing {
namespace detail {

struct SourceLoc {
  const char* file;
  int line;
  const char* expr;
};

inline std::string format_value(const std::string& v) { return "\"" + v + "\""; }
inline std::string format_value(const char* v) {
  return v ? std::string("\"") + v + "\"" : std::string("(null)");
}
inline std::string format_value(bool v) { return v ? "true" : "false"; }
template <typename T>
inline std::string format_value(const T& v) {
  std::ostringstream os;
  os << v;
  return os.str();
}

// EXPECT_EQ(expected, actual) convention. The first arg is the
// expected value (typically a literal in the test source); the
// second is the actual computed value.
template <typename E, typename A>
inline std::string eq_message(const SourceLoc& loc, const E& expected,
                              const A& actual) {
  std::ostringstream os;
  os << loc.file << ":" << loc.line << ": EXPECT_EQ(" << loc.expr
     << ") failed\n    actual:   " << format_value(actual)
     << "\n    expected: " << format_value(expected);
  return os.str();
}

}  // namespace detail
}  // namespace testing
}  // namespace advdeck

#define ADVDECK_FAIL_AT(file, line, msg)                            \
  do {                                                              \
    ::advdeck::testing::fail_current(                               \
        (std::ostringstream() << file << ":" << line << ": " << msg) \
            .str());                                                \
    return;                                                         \
  } while (0)

#define EXPECT_EQ(expected, actual)                                  \
  do {                                                               \
    auto&& _advdeck_expected = (expected);                           \
    auto&& _advdeck_actual = (actual);                               \
    if (!(_advdeck_expected == _advdeck_actual)) {                   \
      ::advdeck::testing::detail::SourceLoc _loc{__FILE__, __LINE__, \
                                                 #expected " == " #actual}; \
      ::advdeck::testing::fail_current(                              \
          ::advdeck::testing::detail::eq_message(_loc, _advdeck_expected, \
                                                  _advdeck_actual)); \
      return;                                                        \
    }                                                                \
  } while (0)

#define EXPECT_TRUE(cond)                                  \
  do {                                                     \
    if (!(cond)) {                                         \
      ADVDECK_FAIL_AT(__FILE__, __LINE__,                  \
                      "EXPECT_TRUE(" #cond ") was false"); \
    }                                                      \
  } while (0)

#endif  // ADVDECK_INCLUDE_ADVDECK_EXPECT_H_
