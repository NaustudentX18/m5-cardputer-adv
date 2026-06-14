# M5Stack Cardputer-Adv Research Index

Generated: 2026-06-13

## Core Findings

- Device: M5Stack `Cardputer-Adv`, SKU `K132-Adv`.
- Core: Stamp-S3A module based on ESP32-S3FN8, dual-core Xtensa LX7 up to 240 MHz, 8 MB flash.
- Display: ST7789V2, 1.14 inch, 240 x 135 px.
- Input: 56-key keyboard, Adv uses TCA8418RTWR I2C keyboard expander.
- Storage: microSD plus 8 MB internal flash.
- Useful Adv upgrades over original Cardputer: 1750 mAh battery, ES8311 audio codec, 3.5 mm audio jack, BMI270 IMU, EXT 2.54-14P expansion bus, optimized antenna, keyboard expander.
- Best initial build lane: Arduino + PlatformIO using `M5Cardputer`, `M5Unified`, and `M5GFX`; graduate selected projects to ESP-IDF only when lower-level control is needed.
- Poseidon inspiration: keyboard-first command UX, static feature modules, theme system, SD-backed assets, web flasher/release workflow, and companion-node architecture.
- Stronger-than-Poseidon first product: a `Command Deck Launcher` with benign built-ins: BLE macro deck, IR remote, Wi-Fi owner diagnostics, offline notes/tasks, and an IMU game/toy.

## Files

- `hardware/cardputer-adv-hardware.md` - specs, pin notes, deltas, and gotchas.
- `hardware/c5-grove-radio-companion.md` - proposed ESP32-C5 Grove companion with CC1101/nRF24 while LoRa/GPS stays on the M5.
- `software/toolchains-and-libraries.md` - build stacks, library choices, starter layout.
- `poseidon-inspo/poseidon-analysis.md` - local repo analysis and reusable ideas.
- `ideas/app-game-opportunity-map.md` - practical app/game concepts ranked for this unit.
- `market-scan/current-projects-and-sentiment-2026-06-13.md` - current successful projects, public likes/dislikes, and gap analysis.
- `code-reviews/architecture-lessons-2026-06-13.md` - code-level patterns and anti-patterns from Bruce, Launcher, Poseidon, Nemo, and related projects.
- `agent-briefs.md` - condensed outputs from the fanned-out research lanes.
- `sources/source-map.md` - URLs and local source snapshots.

## Roadmap Files

- `../roadmap/first-app-roadmap.md` - phased roadmap for the first app build.
- `../roadmap/advdeck-notes-prd.md` - PRD for the first app: Pocket Notes + Snippet Deck.
- `../roadmap/advdeck-agent-prd.md` - PRD for the Notion-AI-style Cardputer agent app.
- `../roadmap/advdeck-agent-plan.md` - architecture and phased plan of attack for AdvDeck Agent.
- `../roadmap/advdeck-agent-swarm-tasks.md` - swarm-ready task pack for breaking the build into agent lanes.

## Local Source Snapshots

- `sources/poseidon/` - cloned from `https://github.com/GeneralDussDuss/poseidon.git`
  - Snapshot commit: `24b518b`
  - Commit date: `2026-06-12 00:52:25 -0400`
  - Subject: `release: v0.6.2 — sub-GHz analyzer modes, Argus 5GHz + lore, Mass Storage, stability`
  - Latest release observed by research lane: `v0.6.2`, published `2026-06-12T04:53:02Z`

## Open Questions For Later Hardware Testing

- Confirm whether the physical Cardputer-Adv unit exposes usable PSRAM. Official M5Stack specs found here list 8 MB flash but do not list PSRAM; Poseidon comments mention PSRAM and then disable it for a particular unit.
- Validate M5Cardputer board auto-detection on the target unit with no hats, with LoRa cap, and with any custom EXT/Grove hardware.
- Measure real battery life per app class: static text utility, animation-heavy game, Wi-Fi scan, BLE scan, audio playback, SD-heavy app.
- Verify audio input/output APIs specifically for ES8311 on Cardputer-Adv before building audio-heavy apps.
