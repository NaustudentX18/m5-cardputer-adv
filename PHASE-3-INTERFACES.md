# Phase 3 Internal Interface Contract (for swarm)

> **Audience:** AdvDeck Agent swarm agents working on Phase 3. Read this FIRST.
> **Purpose:** Define the small shared data + code surface that Batch 1 (A08a-b + A11 + B2.1) and Batch 2 (B3.1) all depend on. Get the contract right once; let the swarm move fast.
> **Status:** Authoritative for `phase-3-text-to-plan` branch. If something here looks wrong, do NOT silently change it — flag in your task notes.

## 1. What Phase 3 means

From `roadmap/advdeck-agent-plan.md` §Phase 3: "typed idea dumps become structured plans." Validation:
- messy idea produces `brief.md`, `plan.md`, `tasks.json`, `tasks.md`, and `agent-prompt.md`
- invalid AI JSON is caught and returned as a recoverable error
- user can reject generated output without losing original idea

Phase 3 also closes the loop with **A11 Agent Pack Export** (the consumer of the plan), **B2.1 Sync Status UI** (deferred from Phase 2), and **B3.1 Review Screen** (the missing piece that lets the user accept or reject generated output without losing the original `idea.md`).

The provider is still **local-first**. No live LLM. The new `LocalFileProvider` reads pre-rendered artifacts from a directory. The `OpenAIProvider` is implemented but disabled by default — it only fires if `OPENAI_API_KEY` is set; the bridge tests skip it.

## 2. SD layout changes in Phase 3

```
/advdeck/
  config.json
  inbox/
  projects/<slug>/
    idea.md                       # never overwritten by import
    transcript.md                 # unchanged
    brief.md                      # generated, REVIEWED
    plan.md                       # generated, REVIEWED
    tasks.json                    # generated, REVIEWED — overwrites user's hand-written
    tasks.md                      # generated, REVIEWED
    calendar-suggestions.json     # generated, NEVER auto-acted-on
    agent-prompt.md               # generated, REVIEWED
    export/                       # Phase 3 NEW: A11
      agent-pack.md               # The single document a fresh agent reads
      agent-tasks.json            # tasks.json copy with version+intent metadata
      README.md                   # tells the fresh agent what's in this folder
      sources.json                # machine-readable index of the source files
      export-info.json            # metadata: exported_at, project_slug, planner_provider, planner_version
  calendar/                       # user's accepted calendar events (still hand-curated in Phase 3)
  outbox/
    pending.jsonl
    results/<request_id>/
      result.json                 # OR error-manifest.json
      brief.md
      plan.md
      tasks.json
      tasks.md
      calendar-suggestions.json
      agent-prompt.md
      raw-provider-output/        # Phase 3 NEW: always keep raw output
        raw-response.txt          # whatever the provider returned (HTTP body, file content, ...)
        raw-metadata.json         # provider name, model, prompt version, attempt n
    staging/<request_id>/         # Phase 3 NEW: holds artifacts awaiting user review
      meta.json                   # review metadata: arrived_at, project, status
      brief.md, plan.md, tasks.json, tasks.md, calendar-suggestions.json, agent-prompt.md
    rejected/<request_id>/        # Phase 3 NEW: holds artifacts the user rejected
      meta.json
      brief.md, plan.md, tasks.json, tasks.md, calendar-suggestions.json, agent-prompt.md
  logs/
    bridge-import.log
```

**Critical:** `idea.md` is never modified by the bridge or the import path. The Phase 2 importer wrote to the project folder; Phase 3 writes to `staging/` first, then on user accept moves files to the project folder atomically. On reject, moves to `rejected/`. On either, `idea.md` is untouched.

## 3. Schemas (B1.1 already owns; one new schema for C1.2)

A new schema for the agent pack export index:

### 3.1 `agent-pack-export-info.schema.json` (new in Phase 3)
```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "AgentPackExportInfo",
  "type": "object",
  "required": ["version", "exported_at", "project_slug", "planner_provider", "planner_version"],
  "properties": {
    "version": { "type": "number", "const": 1 },
    "exported_at": { "type": "string", "format": "date-time" },
    "project_slug": { "type": "string", "pattern": "^[a-z0-9][a-z0-9-]{0,63}$" },
    "planner_provider": { "type": "string" },
    "planner_version": { "type": "string" },
    "request_id": { "type": "string" },
    "artifact_hashes": {
      "type": "object",
      "additionalProperties": { "type": "string" }
    }
  },
  "additionalProperties": false
}
```

`artifact_hashes` is `{ "brief.md": "sha256...", "plan.md": "sha256...", ... }`. SHA-256 of the file bytes (UTF-8, no normalization). Computed at export time, stored in `export-info.json`, used by Z03 to verify the export round-trips through a fresh extract.

### 3.2 No new schemas for B3.1
B3.1's review screen does not need a new schema. It reads the result manifest, the artifacts, and `meta.json` (which is a small typed JSON defined in §4.2 below). The `meta.json` is a fixed shape, not a configurable schema.

## 4. Files and formats

### 4.1 `outbox/staging/<id>/meta.json` (B3.1 writes; the review screen reads)
```json
{
  "version": 1,
  "request_id": "req-20260614-001",
  "project": "pocket-agent",
  "arrived_at": "2026-06-14T15:32:11Z",
  "status": "pending"  // "pending" | "accepted" | "rejected"
}
```

### 4.2 `outbox/rejected/<id>/meta.json` (same shape as staging, status: "rejected")

The review screen mutates this file's status field in place (atomic write).

### 4.3 `export/README.md` template
A frozen template at `bridge/advdeck-agent-bridge/templates/export-README.md.j2` (and a literal copy at `bridge/advdeck-agent-bridge/templates/export-README.md`). Variables: `{{ project_slug }}`, `{{ project_title }}`, `{{ exported_at }}`, `{{ planner_provider }}`, `{{ request_id }}`. The README explains: "You are a fresh coding agent. Read `agent-pack.md` top to bottom. The tasks are in `agent-tasks.json`."

### 4.4 `export/sources.json` template
A JSON index, generated at export time:
```json
{
  "version": 1,
  "project_slug": "pocket-agent",
  "files": [
    { "path": "agent-pack.md",  "bytes": 4211, "sha256": "..." },
    { "path": "agent-tasks.json", "bytes": 1922, "sha256": "..." },
    { "path": "README.md", "bytes": 412, "sha256": "..." },
    { "path": "export-info.json", "bytes": 281, "sha256": "..." }
  ]
}
```

## 5. C++ interface surface (firmware additions)

All headers in `projects/advdeck-agent/include/advdeck/`. Namespace `advdeck`. Pure C++17. Host-testable.

### 5.1 `staging_queue.h` (new — B3.1 owns)
```cpp
namespace advdeck {

struct StagingEntry {
  std::string request_id;
  std::string project;
  std::string arrived_at;
  std::string status;        // "pending" | "accepted" | "rejected"
};

class StagingQueue {
 public:
  StagingQueue(IStorage& storage, std::string storage_root = "/advdeck");

  // Stage a result that's already in outbox/results/<id>/. Copies
  // the artifacts into outbox/staging/<id>/, writes meta.json, leaves
  // the source result dir alone (the user can still mark done
  // afterward).
  std::string stage(const std::string& request_id, std::string* err);

  // Accept or reject a pending entry. On accept, moves every file
  // from staging/<id>/ into <project_dir>/, and from results/<id>/
  // into the project folder as well (so the "source of truth" for
  // the project is the project folder). On reject, moves from
  // staging/<id>/ into rejected/<id>/. Either way, writes a new
  // meta.json with the terminal status.
  std::string accept(const std::string& request_id, std::string* err);
  std::string reject(const std::string& request_id, std::string* err);

  // List pending entries (status: pending), sorted by arrived_at
  // ascending.
  std::string list_pending(std::vector<StagingEntry>* out, std::string* err);

  // Read the meta.json for one entry.
  std::string read_meta(const std::string& request_id, StagingEntry* out, std::string* err);

  // Path of the staging dir. <storage_root>/outbox/staging
  std::string staging_dir() const;
  // Path of the rejected dir. <storage_root>/outbox/rejected
  std::string rejected_dir() const;

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck
```

### 5.2 `agent_pack_exporter.h` (new — C1.2 owns)
```cpp
namespace advdeck {

class AgentPackExporter {
 public:
  AgentPackExporter(IStorage& storage, std::string storage_root = "/advdeck");

  // Read brief.md, plan.md, tasks.json, tasks.md, agent-prompt.md
  // from a project folder, plus the calendar-suggestions.json from
  // the most recent result dir. Write them to the project's export/
  // folder along with export-README.md (rendered from the
  // template), sources.json (file index), and export-info.json
  // (metadata).
  //
  // The metadata block in the header comment lists the SHA-256
  // of every artifact at export time so a verifier can re-check.
  //
  // Returns the export root path on success, "" on failure.
  std::string export_project(const std::string& project_slug,
                             const std::string& planner_provider,
                             const std::string& planner_version,
                             const std::string* request_id_or_null,
                             std::string* err);

 private:
  IStorage* storage_;
  std::string storage_root_;
};

}  // namespace advdeck
```

### 5.3 `bridge_import.h` (modified — B3.1 changes the import flow)
The Phase 2 import copied artifacts directly into the project folder. Phase 3 changes that to:
- `BridgeImport::stage_only(request_id, ...)` — copies artifacts to `outbox/staging/<id>/` and writes `meta.json` with `status: "pending"`. Does NOT touch the project folder.
- The old `BridgeImport::import(request_id, ...)` becomes a thin shim that calls `StagingQueue::accept(request_id)` after staging. Marked deprecated in a comment, kept for the verify.sh end-to-end test.

This is a breaking interface change for any code that called `BridgeImport::import`. B3.1 owns the call-site update in `routes.cpp` and the dispatcher in `main.cpp`.

### 5.4 `outbox_queue.h` (modified — B2.1 adds a `retry` method)
New method:
```cpp
// Mark an errored request as pending again so the bridge picks it
// up on the next run-once. No-op if the request is already pending
// or in_flight.
std::string retry(const std::string& id, std::string* err);
```

### 5.5 `Ctx` extension (B2.1 adds)
Add a pointer to a `StagingQueue` so the routes can accept/reject without going through storage directly. Same pattern as the existing `tasks_for` / `calendar` pointers.
```cpp
// In Ctx (routes.h)
StagingQueue* staging = nullptr;
```

## 6. Bridge CLI additions (C1.1 owns)

Add a `--provider` flag to the existing `plan` and `run-once` subcommands. Values: `dry-run` (default), `local-file`, `openai`. Use `click.Choice`. Document that `openai` requires `OPENAI_API_KEY` and is for live testing only.

New subcommand:
- `advdeck-bridge providers` — list the registered providers and their versions.

New subcommand:
- `advdeck-bridge plan-local --project <slug> --artifacts <dir>` — read pre-rendered artifacts from `<dir>` instead of generating. Used for the A11 test and for the Z03 end-to-end test. Internally uses `LocalFileProvider` and writes the same result manifest as `plan`.

New subcommand:
- `advdeck-bridge export --project <slug> --out <dir>` — produce the A11 agent pack. Reads from the project folder, writes to `<dir>/export/`. This is a CLI-only path; the firmware does its own export on-device. The CLI is for desktop / CI use.

## 7. Provider interface (C1.1 owns)

The existing `Provider` protocol in `bridge/advdeck-agent-bridge/src/advdeck_bridge/providers/__init__.py` is fine. Two new implementations land in this batch:

- `providers/local_file.py` — `LocalFileProvider` reads pre-rendered artifacts from `--artifacts <dir>`. Used for testing, dry-run on real machines, and for Z03.
- `providers/openai.py` — `OpenAIProvider` calls the OpenAI Chat Completions API with the `planning-prompt.md.j2` rendered with the project idea. **Disabled unless `OPENAI_API_KEY` is set.** On any HTTP / JSON / schema error, returns a `ProviderArtifacts` with `warnings` populated and `tasks_json` set to a minimal-but-valid placeholder. The bridge's runner turns that into an `error-manifest.json` only if the schema validation fails; otherwise the bridge's runner writes the artifacts and a `warnings` block in the result manifest.

## 8. Bridge artifact writer hardening (C1.1 owns)

`runner.py` gets a full rewrite (keeping the public surface). The new flow:

1. Read the pending request, mark `in_flight`, bump `attempts`.
2. Read `idea.md` from the project folder.
3. Pick the provider from the CLI's `--provider` flag (default: `dry-run`).
4. Call `provider.render(request, idea_text)`. Capture the raw output (string for `LocalFileProvider`, HTTP body for `OpenAIProvider`) and write it to `outbox/results/<id>/raw-provider-output/raw-response.txt` plus `raw-metadata.json` with provider name + attempt n.
5. Validate `tasks_json` and `calendar_suggestions_json` against the JSON Schemas. If either fails, write `error-manifest.json` with `error_code: "invalid_ai_output"` and `retryable: true` (or `false` if the same provider has failed 3+ times in a row — see below).
6. If validation passes, write the 6 artifacts + the result manifest to `outbox/results/<id>/`.
7. Bump `attempts` in `pending.jsonl`. The status stays `in_flight` until the firmware calls `mark_terminal`.

Retry policy:
- An error manifest with `retryable: true` keeps the request in `pending.jsonl` with `status: "error"`. The CLI's `run-once` re-tries it.
- An error with `retryable: false` (after 3 attempts on the same provider, or an unrecoverable schema mismatch) stays in `pending.jsonl` with `status: "error"` but is excluded from `run-once` re-processing. The user can mark it "retry" manually via B2.1's sync UI.

## 9. UI surface (B3.1 owns the review screen; B2.1 owns the sync UI)

### 9.1 New home menu entries (B2.1)
- "Sync" → `route_sync_impl` shows pending/in_flight/done/errored counts, last 5 done rows, and 'r' to retry an errored row.
- "Export" → `route_export_impl` (uses AgentPackExporter) writes the export folder, then shows the path.

### 9.2 Modified project detail (B3.1)
After the user accepts or rejects a staging entry, the project detail screen shows:
- The most recent 5 staging entries (status, arrived_at, project).
- 'r' opens the review screen for the highlighted entry.

### 9.3 Review screen (B3.1)
A new screen, `route_review_impl(Ctx& ctx, const std::string& request_id)`:
- Shows the project slug + title
- Shows the first 5 lines of `brief.md`
- Shows the task count and the first 3 task titles
- Shows the calendar suggestion count
- Shows the agent prompt first line
- 'Enter' accepts (calls `StagingQueue::accept`)
- 'Esc' rejects (calls `StagingQueue::reject`)
- 'e' shows the full `brief.md` (read-only)
- 't' shows the full `tasks.json` (read-only, pretty-printed)
- 'c' shows the full `calendar-suggestions.json` (read-only)
- 'a' shows the full `agent-prompt.md` (read-only)

## 10. Build commands agents must verify

From `projects/advdeck-agent/`:
```bash
/home/pi/.platformio/penv/bin/platformio run
make -C test/host test
```

From `bridge/advdeck-agent-bridge/`:
```bash
.venv/bin/python -m pytest tests/ -v
```

End-to-end (Z03):
```bash
./projects/advdeck-agent/test/host/verify.sh
```

## 11. Coding rules

Same as Phase 1 + 2: C++17, no Arduino headers in services, no exceptions in firmware, Python 3.11+ for the bridge, type hints on all public functions, all tests under the existing test frameworks.

## 12. What's NOT in Phase 3

- Voice capture (Phase 4 = A09)
- Speech-to-text (Phase 4 = A10)
- Calendar `.ics` export (Phase 5)
- Reminder alerts while the app is running (Phase 5)
- Wi-Fi / SoftAP / C5 companion / radio (later)
- The actual `OpenAIProvider` is implemented but not enabled by default. Phase 4+ may add other providers; Phase 3 only adds the local-file one and the placeholder openai stub.

The LLM still does NOT run on the Cardputer. Everything routes through the bridge service on a desktop or CI machine.
