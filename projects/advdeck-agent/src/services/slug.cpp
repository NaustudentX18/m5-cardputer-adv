// src/services/slug.cpp
//
// Slug generation. Pure C++17, no platform deps.
//
// Unicode policy: non-ASCII bytes are dropped (each byte that does not
// match [a-zA-Z0-9] when lowercased is replaced with '-'). "café" -> "caf".
// This is the simplest deterministic behaviour and is documented in the
// header; it is the choice Z01 should verify against the slug regex
// ^[a-z0-9][a-z0-9-]{0,63}$.
//
// Timestamp fallback: if a title sanitises to an empty string, slugify()
// returns "idea-YYYYMMDD-HHMMSS" using local time. The format is fixed
// at 20 chars ("idea-" + YYYYMMDD + "-" + HHMMSS), well under the
// 64-char cap.

#include "advdeck/slug.h"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <string>

namespace advdeck {
namespace {

constexpr std::size_t kMaxSlugLen = 64;

// Build the timestamp fallback "idea-YYYYMMDD-HHMMSS" from local time.
std::string timestamp_slug() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  // "%04d%02d%02d-%02d%02d%02d" can in theory produce more than 16 chars
  // if int is 64-bit; size the buffer accordingly.
  char buf[64];
  std::snprintf(buf, sizeof(buf), "idea-%04d%02d%02d-%02d%02d%02d",
                static_cast<int>(tm.tm_year + 1900),
                static_cast<int>(tm.tm_mon + 1),
                static_cast<int>(tm.tm_mday),
                static_cast<int>(tm.tm_hour),
                static_cast<int>(tm.tm_min),
                static_cast<int>(tm.tm_sec));
  return std::string(buf);
}

}  // namespace

std::string slugify(const std::string& title) {
  std::string out;
  out.reserve(title.size());

  for (unsigned char c : title) {
    if (std::isalnum(c)) {
      // Lowercase ASCII letters. std::tolower on unsigned char is well-
      // defined for the values we get here (no negative chars).
      out.push_back(static_cast<char>(std::tolower(c)));
    } else {
      // Non-alnum (spaces, punctuation, non-ASCII bytes) -> '-'.
      out.push_back('-');
    }
  }

  // Collapse repeated '-' and trim leading/trailing '-'.
  std::string collapsed;
  collapsed.reserve(out.size());
  bool prev_dash = true;  // treat start as dash to drop leading dashes
  for (char c : out) {
    if (c == '-') {
      if (!prev_dash) {
        collapsed.push_back('-');
        prev_dash = true;
      }
    } else {
      collapsed.push_back(c);
      prev_dash = false;
    }
  }
  while (!collapsed.empty() && collapsed.back() == '-') {
    collapsed.pop_back();
  }

  // Cap at 64 chars; trim a trailing '-' produced by the cap.
  if (collapsed.size() > kMaxSlugLen) {
    collapsed.resize(kMaxSlugLen);
    while (!collapsed.empty() && collapsed.back() == '-') {
      collapsed.pop_back();
    }
  }

  // The first char must be [a-z0-9] per the regex; if we ended up with
  // an empty string here, fall back to the timestamp slug.
  if (collapsed.empty()) {
    return timestamp_slug();
  }
  return collapsed;
}

std::string make_unique_slug(const std::string& desired,
                             SlugExistsFn exists, void* ctx) {
  std::string base = slugify(desired);
  if (!exists || !exists(base, ctx)) {
    return base;
  }
  // Strip a trailing -N if any (so the user gets "x-2", "x-3" rather
  // than "x-2-2" when retrying a project they just renamed).
  std::string stem = base;
  while (!stem.empty()) {
    std::size_t dash = stem.rfind('-');
    if (dash == std::string::npos || dash + 1 >= stem.size()) break;
    bool numeric = true;
    for (std::size_t i = dash + 1; i < stem.size(); ++i) {
      if (stem[i] < '0' || stem[i] > '9') { numeric = false; break; }
    }
    if (!numeric) break;
    stem.resize(dash);
  }
  // If the stem became empty (e.g. base was just a number), keep base.
  if (stem.empty()) stem = base;

  for (int n = 2; n < 10000; ++n) {
    std::string candidate = stem + "-" + std::to_string(n);
    if (candidate.size() > kMaxSlugLen) {
      // Try to trim stem to fit "-NNN" suffix.
      std::size_t need = candidate.size() - kMaxSlugLen;
      if (need < stem.size()) {
        candidate = stem.substr(0, stem.size() - need) + "-" +
                    std::to_string(n);
      } else {
        // Cannot fit; bail out with the timestamp slug to be safe.
        return timestamp_slug();
      }
    }
    if (!exists(candidate, ctx)) {
      return candidate;
    }
  }
  return timestamp_slug();
}

}  // namespace advdeck
