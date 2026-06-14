# AdvDeck Agent

Pocket agent front-end for the M5Stack Cardputer-Adv.

## Current Status

Phase 1 — **Offline Project Capture** — implemented.

- Text idea capture (multiline editor)
- Project folders on SD with slug-based unique names
- Local task lists (per project)
- Local calendar events (global, with `project` tag for filtering later)
- Power-cycle-safe (everything is plain files on SD)
- No AI / cloud / Wi-Fi / radio at boot

A12 will polish the 240x135 UX; Phase 2 adds the bridge protocol and fixture import; Phase 3 replaces fixtures with a real planner.

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

## Target Alpha

AdvDeck Agent Alpha 0.1 supports:

- text idea capture
- project folders on SD
- local tasks
- local calendar events
- bridge fixture import (Phase 2)
- generated artifact review UI (Phase 3)
- agent pack export (Phase 6)

Voice capture and real speech-to-text come after the text-to-plan loop works.

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

The bridge service lives separately under `../../bridge/advdeck-agent-bridge/` and will be wired in during Phase 2.

## First Implementation

`A01 Firmware Skeleton` (done) → `A02 Storage Contract` (done) → `A04 Local Tasks` (done) → `A05 Calendar` (done) → `A03 Capture + Browser` (in flight) → `Z01 Verification Harness` (done).

See `../../PHASE-1-INTERFACES.md` for the shared contract used by the swarm, and `../../roadmap/advdeck-agent-swarm-tasks.md` for the full task list.
