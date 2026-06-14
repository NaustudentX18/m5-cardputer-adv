# Phase 1 Internal Interface Contract (for swarm)

> **Audience:** AdvDeck Agent swarm agents working on Phase 1. Read this FIRST.
> **Purpose:** Define the small shared C++ surface that A01–A05 all depend on. Get this contract right once; let the rest of the swarm move fast against it.
> **Status:** Authoritative for `phase-1-offline-capture` branch. If something here looks wrong, do NOT silently change it — flag in your task notes.

## 1. Repo layout we're creating

```
projects/advdeck-agent/
  platformio.ini                  # PIO project, M5Stack/Cardputer board, Arduino
  src/
    main.cpp                      # A01: setup() + loop() + boot to home
    platform/
      keyboard.h / .cpp           # A01: thin wrapper over M5Cardputer.keyboard
      display.h / .cpp            # A01: tiny canvas helpers
      storage.h / .cpp            # A02: SD mount + atomic write + path joins
    services/
      slug.h / .cpp               # A02: "My Cool Idea" -> "my-cool-idea"
      project_store.h / .cpp      # A02: list/create/read/update project folders
      task_store.h / .cpp         # A04: tasks.json CRUD
      calendar_store.h / .cpp     # A05: events.json CRUD
    app/
      routes.h / .cpp             # A01: minimal home/capture/projects/tasks/calendar routes
      home.h / .cpp               # A01
      capture.h / .cpp            # A03
      projects.h / .cpp           # A03
      tasks.h / .cpp              # A04
      calendar.h / .cpp           # A05
    ui/
      status_bar.h / .cpp         # A01: top strip with SD status
      text_editor.h / .cpp        # A03: multiline editor for idea.md
      menu.h / .cpp               # A01: simple key-driven menu
  test/
    host/                         # Plain C++ that compiles with g++ (no PIO)
      test_slug.cpp
      test_storage_paths.cpp
      test_task_store.cpp
      test_calendar_store.cpp
      test_project_store.cpp
      test_main.cpp               # main() that runs all and prints PASS/FAIL
  include/                        # Public interface headers mirror src/services/*.h
  README.md                       # updated with build/test commands
tests/                            # repo-level integration tests (optional this phase)

bridge/advdeck-agent-bridge/      # untouched in this phase (Phase 2 work)
```

## 2. The data contract on disk (do not deviate)

SD root: `/advdeck/`. Plain files. UTF-8. LF line endings. Atomic write: write to `<file>.tmp`, then `rename()`.

```
/advdeck/
  config.json
  inbox/
  projects/
    <project-slug>/
      idea.md                      # required at create time
      transcript.md                # optional
      brief.md                     # optional, generated
      plan.md                      # optional, generated
      tasks.json                   # optional, generated
      tasks.md                     # optional, generated
      calendar-suggestions.json    # optional, generated
      agent-prompt.md              # optional, generated
      export/
        agent-pack.md
        agent-tasks.json
  calendar/
    events.json
  outbox/
  logs/
```

Project slugs: `^[a-z0-9][a-z0-9-]{0,63}$`. Generator rules:

- Lowercase, ASCII only
- Spaces and non-alnum → `-`
- Collapse repeated `-`, trim leading/trailing `-`
- Truncate to 64 chars
- If the slug is empty after sanitizing, use `idea-<timestamp>` (YYYYMMDD-HHMMSS)
- If a project with the generated slug already exists, append `-2`, `-3`, …

## 3. C++ interface surface (these are the public headers in `include/` and `src/services/*.h`)

All headers in `include/advdeck/`. Namespace `advdeck`. **Pure C++17. No Arduino headers. No M5 headers.** All UI/M5 code in `src/` includes these. Host tests in `test/host/` also include these. This is the seam that lets us test without hardware.

### 3.1 `slug.h`

```cpp
namespace advdeck {
// Sanitize a free-text title into a valid project slug.
std::string slugify(const std::string& title);

// Compute a unique slug given the desired base and an "exists" predicate.
using SlugExistsFn = bool (*)(const std::string& slug, void* ctx);
std::string make_unique_slug(const std::string& desired,
                             SlugExistsFn exists, void* ctx);
}
```

### 3.2 `storage.h`

```cpp
namespace advdeck {

// Abstract storage interface. SD impl lives in src/platform/storage.cpp
// Host tests use an in-memory impl that targets a temp dir.
class IStorage {
public:
  virtual ~IStorage() = default;

  virtual bool mount() = 0;
  virtual bool is_mounted() const = 0;
  virtual bool exists(const std::string& path) const = 0;

  // Returns "" on success, error message on failure.
  virtual std::string ensure_dir(const std::string& path) = 0;

  // Atomic write: write to <path>.tmp, fsync if supported, then rename.
  virtual std::string write_file(const std::string& path, const std::string& data) = 0;

  virtual std::string read_file(const std::string& path) = 0;  // "" if missing
  virtual std::string read_file_or(const std::string& path, const std::string& fallback) = 0;

  virtual std::vector<std::string> list_dir(const std::string& path) = 0;

  virtual std::string join(const std::string& a, const std::string& b) = 0;
  virtual std::string root() const = 0;
};

// In-memory implementation for host tests. Backed by a temp directory.
class HostStorage : public IStorage {
public:
  explicit HostStorage(std::string root_path);
  // ... implements all of the above using std::filesystem on root_path
};

}  // namespace advdeck
```

### 3.3 `project_store.h`

```cpp
namespace advdeck {

struct ProjectSummary {
  std::string slug;
  std::string title;       // derived from first H1 of idea.md, else slug
  std::string idea_path;   // /advdeck/projects/<slug>/idea.md
  std::string dir_path;
  std::string created_at;  // ISO8601
  std::string modified_at; // ISO8601
};

class ProjectStore {
public:
  explicit ProjectStore(IStorage& storage, std::string root = "/advdeck");

  // Returns the slug used.
  // On failure, returns "" and sets *err.
  std::string create_project(const std::string& title,
                             const std::string& idea_text,
                             std::string* err);

  std::string write_idea(const std::string& slug,
                         const std::string& idea_text,
                         std::string* err);

  std::string read_idea(const std::string& slug, std::string* err);

  std::vector<ProjectSummary> list_projects(std::string* err);

  bool project_exists(const std::string& slug);

  std::string project_dir(const std::string& slug) const;
  std::string storage_root() const { return storage_->join(root_, "projects"); }

private:
  IStorage* storage_;
  std::string root_;
};

}  // namespace advdeck
```

### 3.4 `task_store.h`

```cpp
namespace advdeck {

struct Task {
  std::string id;          // "A01" style or "tsk-<8hex>"
  std::string title;
  std::string objective;
  std::string context;
  std::vector<std::string> files_or_modules;
  std::vector<std::string> acceptance_criteria;
  std::vector<std::string> validation;
  std::vector<std::string> dependencies;
  std::string risk;
  std::string suggested_agent_role;
  std::string status;       // "todo" | "doing" | "done"
  std::string created_at;
  std::string updated_at;
};

class TaskStore {
public:
  TaskStore(IStorage& storage, std::string project_dir);

  // File: <project_dir>/tasks.json
  // Schema: top-level { "version": 1, "tasks": [ Task, ... ] }
  // Atomic write. Malformed JSON returns a recoverable error.

  std::string load(std::vector<Task>* out, std::string* err);
  std::string save(const std::vector<Task>& tasks, std::string* err);

  // CRUD helpers
  std::string add_task(const std::string& title, Task* out_added, std::string* err);
  std::string set_status(const std::string& id, const std::string& status, std::string* err);
  std::string delete_task(const std::string& id, std::string* err);

  // Export
  std::string export_markdown(const std::vector<Task>& tasks);
};

}  // namespace advdeck
```

### 3.5 `calendar_store.h`

```cpp
namespace advdeck {

struct Event {
  std::string id;          // "evt-YYYYMMDD-NNN"
  std::string title;
  std::string starts_at;   // ISO8601 with TZ
  std::string ends_at;     // may be empty
  std::string remind_at;   // may be empty
  std::string source;      // "manual" | "bridge"
  std::string project;     // may be empty
  std::string status;      // "accepted" | "suggested" | "rejected"
};

class CalendarStore {
public:
  CalendarStore(IStorage& storage, std::string root = "/advdeck");

  // File: <root>/calendar/events.json
  std::string load(std::vector<Event>* out, std::string* err);
  std::string save(const std::vector<Event>& events, std::string* err);

  std::string add_event(const Event& e, Event* out_added, std::string* err);
  std::string delete_event(const std::string& id, std::string* err);

  // Sorted view, soonest first
  std::string upcoming(const std::string& now_iso,
                       std::vector<Event>* out, std::string* err);
};

}  // namespace advdeck
```

## 4. JSON in Phase 1

Use **nlohmann/json** (single-header). It is in the Arduino library registry (`bblanchon/ArduinoJson` and `nlohmann/json` both work; pick **nlohmann/json** because it has zero Arduino dependency and works on host). Add it to `platformio.ini` under `lib_deps`.

Host tests: use the same header via a CMake-less g++ build that includes the single header directly (download or vendor it). Preferred: vendor a copy at `projects/advdeck-agent/third_party/nlohmann/json.hpp` (already a single header). Add `third_party/` to `.gitignore`? **NO** — the file is MIT and small; vendor it.

## 5. UI / route surface (A01 owns the dispatcher, others plug in)

```cpp
// In src/app/routes.h
namespace advdeck::app {

struct Ctx {
  IStorage& storage;
  ProjectStore& projects;
  TaskStore* tasks_for(const std::string& project_slug) const;  // lazy
  CalendarStore& calendar;
};

enum class Route { Home, Capture, ProjectList, ProjectDetail, TaskList, Calendar };

void route_home(Ctx& ctx);
void route_capture(Ctx& ctx);
void route_project_list(Ctx& ctx);
void route_project_detail(Ctx& ctx, const std::string& slug);
void route_task_list(Ctx& ctx, const std::string& slug);
void route_calendar(Ctx& ctx);

}  // namespace advdeck::app
```

Each route is a stateful function: it draws once when entered, then is driven by a `Route::tick(ctx, key_event)` call from the main loop. The first phase can keep this very small — keyboard input → mutate state → redraw. No fancy widget framework.

**Acceptance for Phase 1 (firmware side):** builds, reaches Home, key presses move between routes, and routes for Capture / ProjectList / TaskList / Calendar at least show their content. Tasks and Calendar can be empty stubs that compile. A03 fills in real UX.

## 6. Host tests contract (Z01)

```
projects/advdeck-agent/
  test/
    host/
      test_main.cpp
      test_slug.cpp
      test_storage_paths.cpp
      test_project_store.cpp
      test_task_store.cpp
      test_calendar_store.cpp
      Makefile            # `make` -> builds a binary `run_tests`
```

`make` should work with system g++ (Debian 13 has g++ 13+). No external deps beyond the vendored `nlohmann/json.hpp`. The binary should exit 0 on success and print `ALL PASS` last line. Each test file uses a tiny `EXPECT_EQ` macro.

## 7. Build commands agents must verify

From `projects/advdeck-agent/`:

```bash
# Firmware build
/home/pi/.platformio/penv/bin/platformio run

# Host tests
cd test/host && make && ./run_tests
```

## 8. Coding rules

- C++17. No exceptions in firmware. Use `std::string` err returns instead of exceptions.
- No `std::cout` in firmware. Use `Serial.printf`.
- All public headers include what they use.
- Keep the .cpp files under 500 lines. Split if larger.
- Comments: explain WHY, not what.
- Use `// NOLINT` only if a static analysis tool is in play; otherwise skip.

## 9. What's NOT in Phase 1

- Bridge service code (Phase 2)
- Real AI / LLM calls (Phase 3)
- Audio recording (Phase 4)
- Cloud calendar export (Phase 5)
- Agent pack export content (Phase 6)
- WebUI / SoftAP / Wi-Fi (Later)
- C5 companion / radio (Later)
- LVGL (Not needed for Phase 1 — direct M5GFX is sufficient)

A05 calendar UI does NOT need to do background reminders or .ics export. Just CRUD + a list. Phase 5 will add the rest.

A03 does NOT need a 240x135-pixel-perfect mockup. Keep it readable. Phase 1.5 (A12) is for the UX polish pass.
