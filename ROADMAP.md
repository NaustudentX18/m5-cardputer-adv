# AdvDeck Agent — Roadmap

> Authoritative. Last sync: 2026-06-15. If this disagrees with `README.md`, this file wins — and the README should be the next thing you fix.

## Current state: MVP shipped (v0.5.0)

All five alpha phases plus the beta-0.5 polish phase plus the host-side
`export` CLI have landed on `main`. Firmware host tests **107/107** and
bridge tests **161/161** pass green (combined: **268/268**).

| Phase | Codename                       | Status         | Code commit | README row |
|------:|--------------------------------|----------------|-------------|------------|
| α 0.1 | Offline Project Capture        | ✅ DONE        | `a9e873d` (firmware skeleton) → `ea6b50c` (A03) | 100% |
| α 0.2 | Real Planner Bridge Protocol   | ✅ DONE        | `bc3b998` (B1.1–B1.3 + Z01) → `5b799f5` (README) | 100% |
| α 0.3 | Calendar Intelligence          | ✅ DONE        | `8151a06` (C1.1–C1.3) → `0f48fb0` (C2.1) | 100% |
| α 0.4 | Voice Capture                  | ✅ DONE        | `9481199` (D1.1) → `930dc8c` (D1.3) | 100% |
| β 0.5 | Agent Workflow Polish (MVP)    | ✅ DONE        | `7174b12` (E1.1–E1.3) | 100% |

The README's roadmap table, the CHANGELOG, and this file all reflect the
shipped v0.5.0 state. If any of them disagree, the v0.5.0 commit is the
source of truth.

## What's still open

### Operator-side, not code

- **Live OpenAI smoke** — `bridge/advdeck-agent-bridge/src/advdeck_bridge/providers/openai.py` is fully implemented (ProviderUnavailable / ProviderRetryable / ProviderUnrecoverable taxonomy, raw-output logging, five unit tests with a stubbed SDK) but has never been run against a real `OPENAI_API_KEY`. The five test stubs cover the success path and every documented error class. **This is a one-liner for whoever has a key:**
  ```bash
  cd bridge/advdeck-agent-bridge
  .venv/bin/pip install -e '.[live]'
  export OPENAI_API_KEY=<your-key>
  .venv/bin/python -m advdeck_bridge plan --project garden-watering --provider openai
  ```
- **Cut the GitHub release for v0.5.0** — the tag has been pushed but
  the release notes on the GitHub side have not been written yet.
- **Tailscale-friendly firmware handoff to the Cardputer** — the
  bridge produces everything; the user still has to `cp -r` the SD
  card contents back. A small `advdeck-bridge push --to <ssh-target>`
  is on the wish list but not required for MVP.

### v0.5.0 wrap-up — what changed

The v0.5.0 commit added:

1. **Host-side `export` command** — `advdeck-bridge export --project
   <slug> --out <dir>` rebuilds the C++ `AgentPackExporter`'s
   `export/` folder from a laptop without the device attached, with
   a project-folder / latest-result-dir fallback. 22 new tests in
   `test_cli_export.py`.
2. **GitHub-issues fan-out** — `--format github-issues` writes one
   `gh issue create --input`-shaped JSON per task, in dependency
   order, with role / status / has-deps / risk labels and a clean
   `INDEX.md` apply block.
3. **Stricter task validation surfaced** — the host exporter reads
   `dependencies`, `suggested_agent_role`, `validation`, `risk` from
   `tasks.json` and renders them in the GitHub-issues body and the
   `INDEX.md` table. The firmware's `TaskStore` already has the same
   fields, so the two sides agree.
4. **Doc sync** — this ROADMAP, the top-level `README.md`, the
   firmware `README.md`, and a new `CHANGELOG.md` all reflect v0.5.0.

## Phase-by-phase detail (historical)

### α 0.1 — Offline Project Capture

Goal: prove the core value without voice, bridge, or cloud.

- firmware skeleton (`A01`), storage contract (`A02`), text capture + project browser (`A03`), local tasks (`A04`), local calendar base (`A05`), verification harness (`Z01`)

Exit criteria met: user can type a messy idea, it persists under
`/advdeck/projects/<slug>/`, the host tests cover slugify, atomic
write, project CRUD, task CRUD, and calendar events.

### α 0.2 — Real Planner Bridge Protocol

Goal: a Cardputer can hand files to an off-device helper and get
results back, even if the helper is a static fixture.

- Phase 2 bridge schemas + templates + invalid-output fixtures (`B1.1`)
- firmware outbox queue + importer (`B1.2`)
- dry-run bridge CLI with five subcommands (`B1.3`)
- end-to-end verification harness (`Z01`)

Exit criteria met: `outbox/pending.jsonl` round-trips through the
bridge, the result manifest validates against
`result-manifest.schema.json`, the firmware imports cleanly, error
manifests keep the request queued and retryable.

### α 0.3 — Calendar Intelligence

Goal: make plans actionable over time.

- real providers (`C1.1`: dry-run + local-file + opt-in OpenAI)
- agent pack export (`C1.2`)
- sync UI (`C1.3`)
- review screen + stage-only bridge import (`C2.1`)

Exit criteria met: accepted suggestions become local events; the
firmware ships a `agent-pack.md` a fresh agent can start from.

### α 0.4 — Voice Capture

Goal: support rough spoken idea dumps.

- SD-aware `WavWriter`, real `Recorder`, recording UI (`D1.1`)
- transcription bridge — providers, CLI, tests (`D1.3`)

Exit criteria met: 1-second mono 16 kHz WAV round-trips, recorder
manifest validates against its schema with a SHA-256 of the bytes,
transcript review flow is documented in `PHASE-4-INTERFACES.md`.

### β 0.5 — Agent Workflow Polish (MVP)

Goal: make the output genuinely useful to other agents.

- calendar accept/reject flow + app-running reminders + `.ics` export (`E1.1`/`E1.2`/`E1.3`)

Exit criteria met: a fresh coding agent can start from
`agent-pack.md`; the exported `.ics` opens in normal calendar apps;
the docs are explicit about reminder limits (app-running only).

## Out of MVP — "Later" bucket

These are the things the global constraints (no RF, no C5 companion,
no cloud) deliberately kept off the critical path:

- BLE HID snippet export
- WebUI over SoftAP
- local workspace watcher
- companion C5 status panel
- LoRa/GPS project metadata
- optional issue tracker integrations
- optional secure sync
- second live LLM provider (Anthropic, local llama.cpp, etc.)

None of these is required for a coding agent to pick up an
`agent-pack.md` and start working. Treat each as a separate
mini-project that would deserve its own phase and its own
`PHASE-N-INTERFACES.md`.

## How to contribute

- **Human contributor** → [docs/PRODUCT.md](docs/PRODUCT.md) → [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) → this file
- **Coding agent** → [AGENTS.md](AGENTS.md) → [docs/AGENT_HANDOFF.md](docs/AGENT_HANDOFF.md) → [roadmap/advdeck-agent-swarm-tasks.md](roadmap/advdeck-agent-swarm-tasks.md)

The swarm-tasks doc is the historical backlog. Treat the α 0.1–β 0.5
rows above as authoritative for "what's done"; the swarm-tasks file
is useful as a per-task spec and acceptance-checklist archive.
