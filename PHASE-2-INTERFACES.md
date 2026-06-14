# Phase 2 Internal Interface Contract (for swarm)

> **Audience:** AdvDeck Agent swarm agents working on Phase 2. Read this FIRST.
> **Purpose:** Define the small shared data + code surface that B1.1 / B1.2 / B1.3 / B2.1 / B2.2 all depend on. Get this contract right once; let the rest of the swarm move fast against it.
> **Status:** Authoritative for `phase-2-bridge-protocol` branch. If something here looks wrong, do NOT silently change it — flag in your task notes.

## 1. What Phase 2 means

From `roadmap/advdeck-agent-plan.md` §Phase 2: "Cardputer can hand files to an off-device helper and receive results." Validation: send `idea.md` to bridge → bridge returns static fixture files → firmware imports → failures leave queued files intact.

No real AI yet (Phase 3 = A08, "Text-To-Plan Bridge"). No voice yet (Phase 4 = A09). The bridge is a **dry-run fixture provider** that always returns canned outputs. The contract is what matters — the provider is interchangeable.

## 2. The SD layout changes in Phase 2

```
/advdeck/
  config.json                       # unchanged from Phase 1
  inbox/                            # unchanged
  projects/<slug>/
    idea.md
    transcript.md
    brief.md                        # Phase 2 NEW: generated, accepted
    plan.md                         # Phase 2 NEW: generated, accepted
    tasks.json                      # Phase 2 NEW: generated, accepted (overwrites user tasks)
    tasks.md                        # Phase 2 NEW: generated, accepted
    calendar-suggestions.json       # Phase 2 NEW: generated, not yet acted on
    agent-prompt.md                 # Phase 2 NEW: generated, accepted
    export/                         # Phase 6 (out of scope)
  calendar/                         # unchanged
  outbox/                           # Phase 2 NEW
    pending.jsonl                   # append-only queue of pending bridge requests
    results/<request_id>/           # per-request staging directory
      result.json                   # result manifest OR error manifest
      brief.md                      # artifact copies
      plan.md
      tasks.json
      tasks.md
      calendar-suggestions.json
      agent-prompt.md
      raw-provider-output/          # raw LLM output (Phase 3+)
        ...
  logs/                             # Phase 2 NEW
    bridge-import.log               # newline-delimited JSON of every import attempt
```

**Important:** `tasks.json` and `agent-prompt.md` exist in the per-project folder from Phase 1 / 2. The bridge generates them; the user can also write them by hand. The bridge output goes through a **review gate** — see §6.

## 3. Schemas (B1.1 owns these)

All schemas live in `bridge/advdeck-agent-bridge/schemas/`. A copy of the **two schemas the firmware cares about** (`pending-request.schema.json` and `result-manifest.schema.json`) is also vendored into `projects/advdeck-agent/schemas/` so the firmware build doesn't need a Python toolchain.

Draft 2020-12, no `$ref`, no `oneOf`/`anyOf` (so the C++ JSON-schema validator we use stays small). If validation requires a richer feature, document it in the .notes file and pick a validator that supports it.

### 3.1 `pending-request.schema.json`
JSON Schema for one line in `/advdeck/outbox/pending.jsonl`:
```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "PendingBridgeRequest",
  "type": "object",
  "required": ["id", "project", "type", "inputs", "created_at", "status"],
  "properties": {
    "id":         { "type": "string", "pattern": "^req-[0-9]{8}-[0-9]{3,6}$" },
    "project":    { "type": "string", "pattern": "^[a-z0-9][a-z0-9-]{0,63}$" },
    "type":       { "type": "string", "enum": ["plan_project"] },
    "inputs":     { "type": "array", "items": { "type": "string" }, "minItems": 1 },
    "created_at": { "type": "string", "format": "date-time" },
    "status":     { "type": "string", "enum": ["pending", "in_flight", "done", "error"] },
    "attempts":   { "type": "number", "minimum": 0 }
  },
  "additionalProperties": false
}
```

### 3.2 `result-manifest.schema.json`
JSON Schema for `outbox/results/<id>/result.json` (success case):
```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "BridgeResultManifest",
  "type": "object",
  "required": ["request_id", "status", "artifacts", "warnings"],
  "properties": {
    "request_id":   { "type": "string", "pattern": "^req-[0-9]{8}-[0-9]{3,6}$" },
    "status":       { "type": "string", "enum": ["ok"] },
    "artifacts":    { "type": "array", "items": { "type": "string" }, "minItems": 1 },
    "warnings":     { "type": "array", "items": { "type": "string" } }
  },
  "additionalProperties": false
}
```

### 3.3 `error-manifest.schema.json`
```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "BridgeErrorManifest",
  "type": "object",
  "required": ["request_id", "status", "error_code", "message", "retryable"],
  "properties": {
    "request_id": { "type": "string", "pattern": "^req-[0-9]{8}-[0-9]{3,6}$" },
    "status":     { "type": "string", "enum": ["error"] },
    "error_code": { "type": "string", "enum": ["invalid_ai_output", "bridge_timeout", "storage_error", "unknown"] },
    "message":    { "type": "string", "minLength": 1 },
    "retryable":  { "type": "boolean" }
  },
  "additionalProperties": false
}
```

### 3.4 `tasks.schema.json`
JSON Schema for `tasks.json` in either the result or the project folder. Mirrors the `Task` struct in `projects/advdeck-agent/include/advdeck/task_store.h` (Phase 1) but with strict validation:
```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "TaskList",
  "type": "object",
  "required": ["version", "tasks"],
  "properties": {
    "version": { "type": "number", "const": 1 },
    "tasks": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["id", "title", "status"],
        "properties": {
          "id":                   { "type": "string", "minLength": 1 },
          "title":                { "type": "string", "minLength": 1 },
          "objective":            { "type": "string" },
          "context":              { "type": "string" },
          "files_or_modules":     { "type": "array", "items": { "type": "string" } },
          "acceptance_criteria":  { "type": "array", "items": { "type": "string" } },
          "validation":           { "type": "array", "items": { "type": "string" } },
          "dependencies":         { "type": "array", "items": { "type": "string" } },
          "risk":                 { "type": "string" },
          "suggested_agent_role": { "type": "string", "enum": ["executor", "planner", "architect", "writer", "reviewer", "debugger", "tester", "oracle", "designer"] },
          "status":               { "type": "string", "enum": ["todo", "doing", "done"] },
          "created_at":           { "type": "string", "format": "date-time" },
          "updated_at":           { "type": "string", "format": "date-time" }
        },
        "additionalProperties": false
      }
    }
  },
  "additionalProperties": false
}
```

### 3.5 `calendar-suggestions.schema.json`
```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "CalendarSuggestions",
  "type": "object",
  "required": ["version", "suggestions"],
  "properties": {
    "version": { "type": "number", "const": 1 },
    "suggestions": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["title", "starts_at"],
        "properties": {
          "title":     { "type": "string", "minLength": 1 },
          "starts_at": { "type": "string", "format": "date-time" },
          "ends_at":   { "type": "string", "format": "date-time" },
          "remind_at": { "type": "string", "format": "date-time" },
          "project":   { "type": "string" }
        },
        "additionalProperties": false
      }
    }
  },
  "additionalProperties": false
}
```

### 3.6 `agent-pack.md` template
A Jinja2-style template at `bridge/advdeck-agent-bridge/templates/agent-pack.md.j2` and a frozen copy at `bridge/advdeck-agent-bridge/templates/agent-pack.md` for the dry-run provider. Variables: `{{ project_slug }}`, `{{ project_title }}`, `{{ idea }}`, `{{ brief }}`, `{{ plan }}`, `{{ tasks }}`, `{{ calendar_suggestions }}`. The frozen copy is what the dry-run provider actually substitutes.

### 3.7 Planning prompt template
`bridge/advdeck-agent-bridge/templates/planning-prompt.md.j2`. Variables: `{{ project_slug }}`, `{{ idea_text }}`, `{{ style }}` (e.g. "executor", "architect", "reviewer"). Used in Phase 3 by A08; the dry-run provider in Phase 2 ignores it. Document this in the header comment.

### 3.8 Invalid-output examples
`bridge/advdeck-agent-bridge/fixtures/invalid/`:
- `tasks-missing-status.json` — task missing the required `status` field
- `tasks-unknown-status.json` — task with `status: "in-progress"` (not in the enum)
- `calendar-bad-datetime.json` — `starts_at: "tomorrow at 9am"`
- `result-manifest-missing-artifacts.json` — manifest missing the required `artifacts` array

Each file is used by the bridge E2E test to verify the validator actually rejects malformed input.

## 4. Bridge protocol on disk (B1.3 owns the writer; B1.2 owns the reader)

### 4.1 Pending request lifecycle
1. Firmware calls `OutboxQueue::enqueue("plan_project", "pocket-agent", {"idea.md"})`. The queue generates `id = "req-" + YYYYMMDD + "-" + NNN` (sequence resets daily), appends a JSON line to `outbox/pending.jsonl`, and writes `status: "pending"`.
2. Bridge CLI reads `outbox/pending.jsonl`, picks the first `status: "pending"` request, and processes it.
3. Before processing, the bridge marks the request `status: "in_flight"` (in-place edit of the JSONL; the queue will compact later).
4. The bridge writes the result to `outbox/results/<id>/` (creates the directory, writes `result.json` plus the artifacts).
5. Firmware's `BridgeImport` reads `outbox/results/<id>/result.json`, validates against `result-manifest.schema.json`, copies the artifacts into the project's folder (subject to the review gate in §6), then marks the request `status: "done"` in `pending.jsonl`. On validation failure or import error, marks `status: "error"`.
6. Failed requests with `retryable: true` (or `error: "storage_error"`) remain in `pending.jsonl` with `status: "error"`; the UI shows them and the user can "retry" which re-queues them.

### 4.2 `outbox/pending.jsonl` format
JSON Lines (one JSON object per line, `\n` separator, no trailing comma, file ends with `\n` if non-empty). Each line is a `PendingBridgeRequest` per §3.1. To update status in place, the firmware rewrites the line at the matching `id`. To compact, the firmware can rewrite the file with only `done` rows trimmed if their `result.json` has been fully imported and the user has accepted.

**Concurrency rule for Phase 2:** single-writer. The firmware writes, the bridge reads+marks in-flight, the firmware writes again. No concurrent bridge processes. Document this in the bridge CLI's `--help` text.

## 5. C++ interface surface (B1.2 owns this; the firmware uses it)

All headers in `projects/advdeck-agent/include/advdeck/`. Namespace `advdeck`. Pure C++17. No Arduino headers. Host tests in `test/host/` use the existing `IStorage` so we can drive the whole flow with a temp dir.

### 5.1 `pending_request.h` (struct + free helpers)
```cpp
namespace advdeck {

struct PendingRequest {
  std::string id;          // "req-YYYYMMDD-NNN"
  std::string project;     // slug
  std::string type;        // "plan_project" only for now
  std::vector<std::string> inputs;
  std::string created_at;  // ISO8601 with TZ
  std::string status;      // "pending" | "in_flight" | "done" | "error"
  int attempts = 0;
};

// Returns the request id used. On failure returns "" and sets *err.
std::string generate_request_id(IStorage& storage,
                                const std::string& outbox_pending_path,
                                std::string* err);

}  // namespace advdeck
```

### 5.2 `outbox_queue.h` (B1.2)
```cpp
namespace advdeck {

class OutboxQueue {
 public:
  OutboxQueue(IStorage& storage, std::string storage_root = "/advdeck");

  // Append a new pending request for `project` with the given input
  // filenames (resolved relative to the project folder). Returns the
  // generated request id; "" on failure.
  std::string enqueue(const std::string& project_slug,
                      const std::string& request_type,
                      const std::vector<std::string>& inputs,
                      std::string* err);

  // Read all pending/in_flight/done/error rows. *out is cleared.
  std::string load_all(std::vector<PendingRequest>* out, std::string* err);

  // Mark an existing request as in_flight (the bridge is working on it).
  std::string mark_in_flight(const std::string& id, std::string* err);

  // Mark an existing request as done or error (the firmware finished
  // processing the result). The outbox JSONL is rewritten atomically.
  std::string mark_terminal(const std::string& id,
                            const std::string& final_status,  // "done" | "error"
                            std::string* err);

  // Path of the JSONL file. <storage_root>/outbox/pending.jsonl
  std::string pending_path() const;
  // Path of the per-request results folder. <storage_root>/outbox/results
  std::string results_dir() const;

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck
```

### 5.3 `bridge_import.h` (B1.2)
```cpp
namespace advdeck {

struct ImportResult {
  bool ok = false;
  std::string request_id;
  std::vector<std::string> imported_files;  // absolute paths
  std::vector<std::string> warnings;
  std::string error_message;
  bool retryable = false;
};

class BridgeImport {
 public:
  BridgeImport(IStorage& storage,
               std::string storage_root = "/advdeck");

  // Look at outbox/results/<id>/result.json. If it's a result
  // manifest, copy the artifacts into the project folder, validate
  // tasks.json / calendar-suggestions.json against the schemas, and
  // return a populated ImportResult. If it's an error manifest,
  // return ok=false with the error message. The caller is then
  // expected to call OutboxQueue::mark_terminal accordingly.
  std::string import(const std::string& request_id,
                     ImportResult* out, std::string* err);

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck
```

### 5.4 ProjectStore extension (B1.2)
Add these two methods to `ProjectStore` so the import can put artifacts in the right place:
```cpp
// Returns the absolute on-storage path of the project folder.
std::string project_dir(const std::string& slug) const;  // already in Phase 1
```

`ProjectStore::project_dir` already exists from Phase 1. **No changes to ProjectStore needed for Phase 2** — `BridgeImport` will use the existing `project_dir` and `read_idea` / `write_idea`. If you find you need a new method, document it in the .notes file.

### 5.5 `bridge_log.h` (B1.2)
```cpp
namespace advdeck {

class BridgeLog {
 public:
  BridgeLog(IStorage& storage,
            std::string storage_root = "/advdeck");

  // Append one event to logs/bridge-import.log. Atomic write.
  // `event` is a JSON object with at least {"ts","request_id","event","details"}.
  std::string log_event(const std::string& request_id,
                       const std::string& event_name,
                       const std::string& details_json,
                       std::string* err);

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck
```

## 6. Review gate (B1.2 implements, B1.3 / B2.2 respect)

Per the architecture's "Generated AI output must be reviewable before acceptance" rule:

- **Phase 2** (no real LLM yet): the dry-run fixture is deterministic and known. The firmware imports the result **directly** into the project folder (no user prompt), but writes a `bridge-import.log` entry with `event: "import"`, `status: "auto"`. A future phase will add the review screen.
- **Phase 3+** (real LLM): the firmware imports into a **staging** folder under `outbox/staging/<id>/`. The user reviews on-device, then either accepts (artifacts move to project folder) or rejects (artifacts move to `outbox/rejected/<id>/`).
- **The dry-run provider in Phase 2 DOES NOT need the staging dance.** It just writes artifacts directly. The bridge CLI is responsible for putting them in `outbox/results/<id>/` and the firmware reads from there.

This keeps Phase 2 small. B1.2 implements the **direct import** path; B1.2's `BridgeImport` writes a `bridge-import.log` entry; B1.2 leaves a `// TODO(phase-3): staging + review gate` comment in the import body.

## 7. UI surface (B2.1 owns this)

A new `route_sync` entry in the home menu. Shows:
- Count of pending requests (from `pending.jsonl` rows with `status: pending` or `in_flight`)
- Count of errored retryable requests
- Count of done requests (last 5, with timestamps)
- A button/key to retry an errored request
- A button/key to compact `pending.jsonl` (drop done rows older than 7 days)

The screen is read-only for the `brief.md` / `plan.md` / `agent-prompt.md` / `tasks.json` files — viewing those is A12 polish.

## 8. Bridge CLI surface (B1.3 owns this)

Python package at `bridge/advdeck-agent-bridge/`, installable with `pip install -e .` (or just runnable as `python -m advdeck_bridge`). Click-based. Subcommands:

```
advdeck-bridge plan --project <slug> [--storage-root <path>]
advdeck-bridge list [--storage-root <path>]
advdeck-bridge show <request_id> [--storage-root <path>]
advdeck-bridge run-once [--storage-root <path>] [--dry-run]
advdeck-bridge validate <fixture.json> <schema.json>  # for tests
```

- `plan` is the main entry point: reads `idea.md` from the project folder, runs the dry-run provider, writes a result to `outbox/results/<id>/`.
- `run-once` reads `pending.jsonl` and processes the first pending request. The fixture provider is the only provider Phase 2 ships with; `run-once` uses it.
- `validate` is exposed for the E2E test in Z02 to check the schemas reject the bad fixtures from §3.8.

The CLI logs to stderr; the manifest + artifacts on disk are the only thing the firmware reads.

## 9. Build commands agents must verify

From `projects/advdeck-agent/`:
```bash
/home/pi/.platformio/penv/bin/platformio run           # firmware
make -C test/host test                                  # host C++ tests
```

From `bridge/advdeck-agent-bridge/`:
```bash
pip install -e .[test]                                 # install with test deps
pytest tests/ -v                                        # bridge tests
```

End-to-end:
```bash
./projects/advdeck-agent/test/host/verify.sh            # firmware + C++ host tests
./bridge/advdeck-agent-bridge/verify-bridge.sh          # bridge E2E
```

`verify-bridge.sh` is what Z02 writes. It runs a full pipeline: create a project, queue a planning request, run `advdeck-bridge run-once`, simulate the firmware import, verify the artifacts landed.

## 10. Coding rules

Same as Phase 1: C++17, no Arduino headers in services, no exceptions in firmware, Python 3.11+ for the bridge, type hints on all public functions.

## 11. What's NOT in Phase 2

- Real LLM provider (Phase 3 = A08)
- Voice recording / transcription (Phase 4 = A09 / A10)
- `.ics` calendar export (Phase 5)
- Agent pack export (Phase 6)
- Review/accept/reject screen for generated outputs (Phase 3; the dry-run is auto-imported)
- WebUI / SoftAP / Wi-Fi (later)
- C5 companion / radio (later)

The dry-run provider is enough. If you find yourself reaching for HTTP, requests, or any LLM SDK, stop — that's Phase 3.
