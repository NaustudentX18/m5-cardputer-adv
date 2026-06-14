# AdvDeck Agent

Pocket agent front-end for the M5Stack Cardputer-Adv.

## Current Status

**MVP shipped** (v0.5.0, 2026-06-15). All five alpha phases plus β 0.5
polish landed on `main`. Firmware host tests **107/107** pass green.

- Phase 1: text idea capture (multiline editor) ✅
- Phase 1: project folders on SD with slug-based unique names ✅
- Phase 1: local task lists (per project) ✅
- Phase 1: local calendar events (global, with `project` tag) ✅
- Phase 2: bridge outbox queue + importer ✅
- Phase 2: bridge log + staging queue ✅
- Phase 3: review screen + stage-only bridge import ✅
- Phase 3: agent pack export (`export/agent-pack.md` + tasks) ✅
- Phase 4: SD-aware `WavWriter` + `Recorder` + recording UI ✅
- Phase 4: voice-to-plan wired through bridge (`transcribe-and-plan`) ✅
- Phase 5: calendar accept/reject flow + `.ics` export ✅
- Phase 5: app-running reminder alerts ✅

Power-cycle-safe throughout (plain files on SD). No AI / cloud / Wi-Fi /
radio at boot.

Later / out of MVP: BLE HID snippet export, WebUI over SoftAP, local
workspace watcher, companion C5 status panel, LoRa/GPS project metadata,
optional issue tracker integrations, optional secure sync. See the
top-level `ROADMAP.md` for the full list.

## Build & Test

```bash
# Firmware build (PlatformIO + Arduino)
cd projects/advdeck-agent
/home/pi/.platformio/penv/bin/platformio run

# Host tests (no Arduino headers; pure C++17 + nlohmann/json)
make -C test/host test

# End-to-end verification: host tests + firmware build
./test/host/verify.sh
```

The host tests cover slug generation, storage atomic writes, project CRUD, task CRUD, and calendar events. They run in milliseconds on the host and use a temp-dir-backed `HostStorage` so each test is isolated.

## Bridge integration

The `bridge/advdeck-agent-bridge/` CLI is the off-device helper for AI
planning. Firmware side:

- enqueues a `plan_project` request to `outbox/pending.jsonl`,
- the bridge processes it and writes the six artefacts to
  `outbox/results/<id>/`,
- `BridgeImport` validates the result manifest, stages the artefacts
  into `<project>/.staging/`, and waits for the user to **accept** or
  **reject** on the review screen,
- on accept the artefacts land in `<project>/` and the
  `AgentPackExporter` rebuilds `export/agent-pack.md` + tasks on the
  SD card.

The host side has a mirror command — `advdeck-bridge export
--project <slug> --out <dir>` — that rebuilds the same `export/`
folder from a laptop without the device attached. `advdeck-bridge
export --format github-issues` fans the tasks out into
`gh issue create --input`-shaped JSON for tracker fan-out.

Voice input is wired end-to-end: `Recorder` writes a SHA-256-manifest
WAV to SD, the bridge transcribes it with one of `mock`,
`local-whisper`, or `openai` providers, and the transcript is
chained into the planning flow via `transcribe-and-plan`.

## Implementation Constraints

- PlatformIO + Arduino first.
- Use `M5Cardputer`, `M5Unified`, and `M5GFX`.
- Keep AI and secrets off-device in a bridge service.
- Keep all user data as plain Markdown/JSON files on SD.
- Do not require Wi-Fi, cloud, C5 companion hardware, or LoRa/GPS to boot.

## Firmware Layout

```text
platformio.ini
include/advdeck/    # public C++17 headers (no Arduino, no M5)
  slug.h, storage.h, project_store.h, task_store.h,
  calendar_store.h, expect.h
src/
  main.cpp                     # setup + loop
  platform/                    # keyboard, display, host_storage, sd_storage
  services/                    # slug, project_store, task_store, calendar_store
  app/                         # routes dispatcher + per-screen handlers
    routes.{h,cpp}
    capture.{h,cpp}            # A03
    projects.{h,cpp}           # A03
    tasks.{h,cpp}              # A04
    calendar.{h,cpp}           # A05
  ui/
    status_bar.{h,cpp}, menu.{h,cpp}, text_editor.{h,cpp}  # A03
test/host/                     # g++ host tests + Makefile + verify.sh
third_party/nlohmann/          # vendored MIT single-header
boards/                        # local m5stack-cardputer board + pinout
```
The bridge service lives in `../../bridge/advdeck-agent-bridge/` and is
already wired into the firmware's `outbox`/`inbox`/review flow (see
the `Bridge integration` section above).

## Implementation Order

`A01 Firmware Skeleton` → `A02 Storage Contract` → `A04 Local Tasks` →
`A05 Calendar` → `A03 Capture + Browser` → `Z01 Verification Harness` →
`B1.1–B1.3 Bridge Protocol` → `B2.x Bridge Importer` → `B3.x Review
Gate` → `C1.1 Real Providers` → `C1.2 Agent Pack Export` → `C1.3 Sync
UI` → `D1.1 Recorder` → `D1.3 Transcription` → `E1.1–E1.3 Calendar +
Reminders + Polish`. All landed on `main` as of v0.5.0.
See `../../PHASE-5-INTERFACES.md` for the shared contract used by the
swarm across Phases 1–5, and `../../roadmap/advdeck-agent-swarm-tasks.md`
for the historical task list.
