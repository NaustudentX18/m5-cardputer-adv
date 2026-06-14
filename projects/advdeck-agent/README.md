# AdvDeck Agent

Pocket agent front-end for the M5Stack Cardputer-Adv.

## Current Status

Planning-ready. Firmware implementation has not started.

Start here:

1. Read `../../roadmap/advdeck-agent-prd.md`.
2. Read `../../roadmap/advdeck-agent-plan.md`.
3. Use `../../roadmap/advdeck-agent-swarm-tasks.md` as the agent task queue.
4. Start with Task A01, then A02 and A03.

## Target Alpha

AdvDeck Agent Alpha 0.1 should support:

- text idea capture
- project folders on SD
- local tasks
- local calendar events
- bridge fixture import
- generated artifact review UI
- agent pack export

Voice capture and real speech-to-text come after the text-to-plan loop works.

## Implementation Constraints

- PlatformIO + Arduino first.
- Use `M5Cardputer`, `M5Unified`, and `M5GFX`.
- Keep AI and secrets off-device in a bridge service.
- Keep all user data as plain Markdown/JSON files on SD.
- Do not require Wi-Fi, cloud, C5 companion hardware, or LoRa/GPS to boot.

## Planned Firmware Shape

```text
platformio.ini
src/
  main.cpp
  app/
  platform/
  services/
  ui/
test/
  host/
```

The bridge service lives separately under `../../bridge/advdeck-agent-bridge/`.

## First Implementation PR

Start with task `A01 Firmware Skeleton` from `../../roadmap/advdeck-agent-swarm-tasks.md`.
