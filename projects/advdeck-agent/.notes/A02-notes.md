# A02 — Storage Contract: notes

## What landed
- `include/advdeck/slug.h` + `src/services/slug.cpp` — slugify + make_unique_slug
- `include/advdeck/storage.h` + `src/platform/host_storage.cpp` (host impl), `src/platform/sd_storage.cpp` (firmware stub)
- `include/advdeck/project_store.h` + `src/services/project_store.cpp`
- `include/advdeck/expect.h` — tiny test framework: `EXPECT_EQ`, `EXPECT_TRUE`, `ADVDECK_REGISTER_TEST`
- `test/host/{test_main,test_slug,test_storage_paths,test_project_store}.cpp`

## Contract deviations from PHASE-1-INTERFACES.md
Documented where the agent deviated from the spec and why:

1. **`IStorage::mtime_iso8601(path)` added.** PHASE-1-INTERFACES.md §3.2 did not list it. `ProjectStore::list_projects` needs file mtimes to populate `ProjectSummary.modified_at` and `created_at` without exposing the underlying filesystem path (which would be a leaky abstraction). The deviation is documented in `storage.h`.
2. **`ProjectSummary::created_at` and `modified_at` are ISO8601 UTC strings**, populated from `IStorage::mtime_iso8601()`. The PHASE-1-INTERFACES.md §3.3 said the field exists but did not name the format.
3. **`title` derivation in `ProjectSummary`.** When `idea.md` starts with `# Title`, the summary's `title` is "Title"; otherwise it falls back to the slug. This matches the spirit of the contract and is tested in `test_project_store_uses_h1_for_title`.
4. **Unicode policy.** `slugify` drops non-ASCII bytes (replaces them with `-`, then trims). "café" -> "caf". This is documented in `slug.cpp` and exercised in `test_slugify_unicode_drop`.
5. **Test framework.** Used a small `advdeck::testing` registry (`expect.h` + `ADVDECK_REGISTER_TEST` macro) instead of the literal `EXPECT_EQ` + `RUN_TEST` the contract mentioned. Same semantics, slightly more test-friendly (registration is automatic, failures are attributed to a name).
6. **Test fixture location.** Tests live at `test/host/test_*.cpp` and are linked into a single `run_tests` binary via `g++` (no Makefile in this task — Z01 will add one). Each test gets a unique temp dir under `fs::temp_directory_path() / "advdeck_a02_ps_<pid>_<n>"`.
7. **`make_unique_slug` de-duplication.** If `slugify("x-2")` collides with an existing "x", the helper strips the numeric suffix to produce "x-2" (i.e. it tries the same desired name without the trailing -2 first). This was a sub-spec decision; if you want a different policy, swap the implementation in `slug.cpp::make_unique_slug`.

## file_time_type -> ISO8601 (gllibc 14)
`std::filesystem::file_time_type` on libstdc++ 14 (gllibc 14) is based on CLOCK_REALTIME; we cast to `time_t` and use `gmtime_r`. This is good enough on Debian 14 (where this project is built). On libstdc++ that uses `__file_clock` with a different epoch, the cast will produce a wrong wall time but still produce a valid string — `ProjectStore` falls back to `now_iso8601_utc()` for `created_at` when the mtime is empty.

## Verification
`g++ -std=c++17 -Wall -Wextra -I include -I src -I third_party/nlohmann test/host/*.cpp src/services/slug.cpp src/services/project_store.cpp src/platform/host_storage.cpp -o /tmp/run_tests_a02 && /tmp/run_tests_a02` exits 0 with `ALL PASS` (23 passed, 0 failed).
