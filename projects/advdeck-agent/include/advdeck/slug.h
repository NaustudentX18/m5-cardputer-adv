// advdeck/slug.h
//
// Slug generation for project folders.
//
// Slug rules (matches PHASE-1-INTERFACES.md §2):
//   - Lowercase, ASCII only
//   - Spaces and non-alnum -> '-'
//   - Collapse repeated '-', trim leading/trailing '-'
//   - Truncate to 64 chars (max length of the regex)
//   - If empty after sanitizing, use "idea-<timestamp>" (YYYYMMDD-HHMMSS)
//
// `make_unique_slug(desired, exists, ctx)` calls `exists(candidate, ctx)` and
// appends "-2", "-3", ... until the predicate returns false. The caller
// supplies the predicate (e.g. an IStorage-backed check) so this header does
// not depend on storage.

#ifndef ADVDECK_INCLUDE_ADVDECK_SLUG_H_
#define ADVDECK_INCLUDE_ADVDECK_SLUG_H_

#include <string>

namespace advdeck {

// Sanitize a free-text title into a valid project slug.
std::string slugify(const std::string& title);

// Compute a unique slug given the desired base and an "exists" predicate.
using SlugExistsFn = bool (*)(const std::string& slug, void* ctx);
std::string make_unique_slug(const std::string& desired,
                             SlugExistsFn exists, void* ctx);

}  // namespace advdeck

#endif  // ADVDECK_INCLUDE_ADVDECK_SLUG_H_
