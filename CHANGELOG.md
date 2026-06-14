# Changelog

All notable changes are recorded here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions
follow [SemVer](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.5.1] - 2026-06-15

### Added

 - Host-side `advdeck-bridge export` CLI: rebuilds the C++ `AgentPackExporter`'s `export/` folder from a laptop without the device attached. Reads from the project folder with a fallback to the most recent `outbox/results/<id>/`. Validates `export-info.json` against the `agent-pack-export-info` schema and writes a SHA-256 `sources.json` so a verifier can re-check the bytes.
 - `--format github-issues`: one `gh issue create --input`-shaped JSON per task in dependency order, with role / status / has-deps / risk labels and a clean `INDEX.md` apply block. Useful for fanning the agent pack out into a tracker without copy-paste.
 - 22 new tests in `tests/test_cli_export.py` (161/161 bridge tests pass).

### Documentation

 - Synced `ROADMAP.md` to match the shipped state of `main` (Phases 1–5 + β 0.5 all DONE; MVP tagged v0.5.0).
 - Replaced the "what's next" prose with a concrete, code-commit-anchored phase table.
 - Listed the three remaining operator-side / wish-list items so contributors know what is *not* code.
 - Flipped the README's roadmap table to match (Phase 2 row DONE 100%, β 0.5 row DONE 100%, v0.5.0 row TAGGED 100%).
 - Bumped bridge `pyproject.toml` to `0.5.0`.
## [0.5.0] - 2026-06-15 - MVP

First public release. Pocket AI planner on the M5Stack Cardputer-Adv.

### Added

#### α 0.1 — Offline Project Capture (Phase 1)
- Firmware skeleton (PlatformIO + Arduino + M5Cardputer + M5Unified + M5GFX).
- SD storage contract: `/advdeck/{inbox,projects,calendar,outbox,logs}`.
- Atomic write helper + project slug generator.
- Text capture route with multiline editor + project browser.
- Per-project task list with `tasks.json` schema v1.
- Global calendar events with `events.json` schema v1.
- Host test harness (`make -C test/host test`) — 107/107 green.

#### α 0.2 — Real Planner Bridge Protocol (Phase 2)
- Live OpenAI smoke one-liner (operator side; the provider is fully implemented and stubbed-tested):
  ```bash
  cd bridge/advdeck-agent-bridge
  .venv/bin/pip install -e '.[live]'
  export OPENAI_API_KEY=<your-key>
  .venv/bin/python -m advdeck_bridge plan --project garden-watering --provider openai
  ```
- Firmware outbox queue + bridge importer + bridge log + staging queue.
- Dry-run provider for end-to-end testing without an API key.
- Bridge tests — 161/161 green.

#### α 0.3 — Calendar Intelligence (Phase 3)
- Real providers: `local-file` (testing), opt-in `openai` (live), `dry-run` (default).
- Agent pack exporter (`export/agent-pack.md`, `agent-tasks.json`, `README.md`, `sources.json`, `export-info.json`).
- Sync UI on the Cardputer.
- Review screen + stage-only bridge import.

#### α 0.4 — Voice Capture (Phase 4)
- SD-aware `WavWriter` (mono 16 kHz, multi-chunk, host-verifiable).
- `Recorder` service with SHA-256 manifest.
- Recording UI with elapsed-time display.
- Transcription bridge with `mock` / `local-whisper` / `openai` providers.
- `transcribe-and-plan` CLI for the voice-to-plan loop.

#### β 0.5 — Agent Workflow Polish (Phase 5)
- Calendar accept/reject flow.
- App-running reminder alerts (no claim of powered-off reliability).
- Deterministic `.ics` export.
- Natural-language date parser for calendar suggestions.
