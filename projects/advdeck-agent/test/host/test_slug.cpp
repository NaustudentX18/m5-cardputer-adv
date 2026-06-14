// test/host/test_slug.cpp

#include <cstdio>
#include <string>
#include <unordered_set>

#include "advdeck/expect.h"
#include "advdeck/slug.h"

using advdeck::slugify;
using advdeck::make_unique_slug;
using advdeck::SlugExistsFn;

namespace {

// ---- test cases ----------------------------------------------------------

void slugify_empty() {
  // Empty -> timestamp fallback "idea-YYYYMMDD-HHMMSS" (20 chars).
  std::string s = slugify("");
  EXPECT_EQ(std::string("idea-"), s.substr(0, 5));
  EXPECT_TRUE(s.size() == 20u);
  // No leading/trailing dash in the fallback.
  EXPECT_TRUE(s.front() != '-');
  EXPECT_TRUE(s.back() != '-');
}

void slugify_basic_title() {
  EXPECT_EQ(std::string("my-cool-idea"), slugify("My Cool Idea"));
}

void slugify_unicode_drop() {
  // "café" -> "caf" (the 'é' is a non-ASCII byte and is dropped,
  // leaving "caf"; the trailing non-alnum becomes '-', which is then
  // trimmed).
  EXPECT_EQ(std::string("caf"), slugify("café"));
}

void slugify_consecutive_separators() {
  EXPECT_EQ(std::string("a-b-c"), slugify("a  --  b ___ c"));
}

void slugify_leading_trailing_dash() {
  EXPECT_EQ(std::string("foo-bar"), slugify("---foo-bar---"));
  EXPECT_EQ(std::string("hello"), slugify("/hello/"));
}

void slugify_length_cap() {
  // 80 'a's -> 64 'a's (capped).
  std::string in(80, 'a');
  std::string s = slugify(in);
  EXPECT_TRUE(s.size() == 64u);
  EXPECT_EQ(std::string(64u, 'a'), s);

  // Trailing dash introduced by truncation is also trimmed.
  std::string in2 = "abc-" + std::string(80, 'x');
  std::string s2 = slugify(in2);
  EXPECT_TRUE(s2.size() == 64u);
  EXPECT_TRUE(s2.back() != '-');
}

void slugify_punctuation_only_uses_fallback() {
  std::string s = slugify("!!!");
  EXPECT_EQ(std::string("idea-"), s.substr(0, 5));
  EXPECT_TRUE(s.size() == 20u);
}

void make_unique_slug_no_collision() {
  auto cb = +[](const std::string&, void*) { return false; };
  EXPECT_EQ(std::string("my-cool-idea"),
            make_unique_slug("My Cool Idea", cb, nullptr));
}

void make_unique_slug_with_collision_appends_two() {
  std::unordered_set<std::string> taken = {"my-cool-idea"};
  auto cb = +[](const std::string& s, void* ctx) {
    return static_cast<std::unordered_set<std::string>*>(ctx)->count(s) > 0;
  };
  EXPECT_EQ(std::string("my-cool-idea-2"),
            make_unique_slug("My Cool Idea", cb, &taken));
  // After inserting the result, a second call should yield -3.
  taken.insert("my-cool-idea-2");
  EXPECT_EQ(std::string("my-cool-idea-3"),
            make_unique_slug("My Cool Idea", cb, &taken));
}

void make_unique_slug_strips_numeric_suffix() {
  // base was "x-2" -> stem "x" -> next is "x-2" (the original).
  std::unordered_set<std::string> taken = {"x"};
  auto cb = +[](const std::string& s, void* ctx) {
    return static_cast<std::unordered_set<std::string>*>(ctx)->count(s) > 0;
  };
  EXPECT_EQ(std::string("x-2"), make_unique_slug("x-2", cb, &taken));
}

}  // namespace

ADVDECK_REGISTER_TEST(slug_slugify_empty, slugify_empty);
ADVDECK_REGISTER_TEST(slug_slugify_basic_title, slugify_basic_title);
ADVDECK_REGISTER_TEST(slug_slugify_unicode_drop, slugify_unicode_drop);
ADVDECK_REGISTER_TEST(slug_slugify_consecutive_separators,
                       slugify_consecutive_separators);
ADVDECK_REGISTER_TEST(slug_slugify_leading_trailing_dash,
                       slugify_leading_trailing_dash);
ADVDECK_REGISTER_TEST(slug_slugify_length_cap, slugify_length_cap);
ADVDECK_REGISTER_TEST(slug_slugify_punctuation_only_uses_fallback,
                       slugify_punctuation_only_uses_fallback);
ADVDECK_REGISTER_TEST(slug_make_unique_slug_no_collision,
                       make_unique_slug_no_collision);
ADVDECK_REGISTER_TEST(slug_make_unique_slug_with_collision_appends_two,
                       make_unique_slug_with_collision_appends_two);
ADVDECK_REGISTER_TEST(slug_make_unique_slug_strips_numeric_suffix,
                       make_unique_slug_strips_numeric_suffix);
