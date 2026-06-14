# Poseidon Inspiration Analysis

Generated: 2026-06-13

Source repo: https://github.com/GeneralDussDuss/poseidon

Local clone: `research/sources/poseidon/`

Snapshot:

- Commit: `24b518b`
- Date: `2026-06-12 00:52:25 -0400`
- Subject: `release: v0.6.2 — sub-GHz analyzer modes, Argus 5GHz + lore, Mass Storage, stability`
- Latest release observed: `v0.6.2 — sub-GHz analyzer, Argus awakens`, published `2026-06-12T04:53:02Z`
- Repo metadata observed by research lane: created `2026-04-14`, pushed `2026-06-12`, default branch `master`.

## What Poseidon Is

Poseidon describes itself as keyboard-first pentesting firmware for the M5Stack Cardputer-Advance. It targets the Cardputer-Adv with PlatformIO/Arduino and a large feature set across Wi-Fi, BLE, sub-GHz, LoRa, IR, LAN/network tools, USB/HID, themes, screensavers, and a companion ESP32-C5 node.

The useful inspiration is not the offensive feature list. The useful engineering/product ideas are:

- keyboard-first UX
- dense tiny-screen UI
- typed parameters instead of D-pad scrolling
- feature modules split by capability
- static menu tree with mnemonic hotkeys
- SD-backed assets, state, and captures
- theme engine and screensavers
- companion-node architecture for hardware the S3 cannot do itself
- web flasher / M5Burner-style install flow

## Architecture Observed

Important files:

- `platformio.ini` - primary Cardputer env plus launcher/OTA variants, pinned Arduino/IDF stack, library pins.
- `src/main.cpp` - boot sequencing, hardware safety setup, SD/theme/sound init, menu entry.
- `src/app.h` - shared display geometry and color constants.
- `src/menu.h` / `src/menu.cpp` - static hierarchical menu with one-letter hotkeys.
- `src/input.cpp` / `src/input.h` - keyboard polling and modal line editor.
- `src/ui.cpp`, `src/theme.cpp`, `src/ui_ambient.cpp`, `src/screensaver.cpp` - UI and style layer.
- `src/features/*.cpp` - large feature collection, generally one capability per file.
- `c5_node/` - companion ESP32-C5 firmware.
- `docs/` - static web docs and web-flasher manifests.
- `TESTERS.md`, `RELEASE_CHECKLIST.md`, and roadmap docs - field-test/release workflow references, though some docs appeared stale relative to v0.6.2.

## Dependency Shape Observed

From `platformio.ini`, Poseidon uses a non-trivial pinned stack:

- pioarduino `platform-espressif32` 55.03.38.
- Arduino 3.3.8 / IDF 5.5.4 family.
- C++17.
- `M5Cardputer`, `M5Unified`, `NimBLE-Arduino`, `ESP32Ping`, `RadioLib`, SmartRC-CC1101 fork, `rc-switch`, and `RF24`.
- Custom/pinned Wi-Fi libraries and linker behavior for raw Wi-Fi management-frame behavior.

For a general app/game hub, do not copy this stack wholesale. Start with official M5Stack/PlatformIO settings and only adopt Poseidon's pins when a specific feature requires them.

## Patterns Worth Reusing

- Static `menu_node_t` tree: deterministic, memory-light, easy to navigate by letter.
- `input_poll()` wrapper: centralizes keyboard update, special keys, injected events, and idle tracking.
- Modal line editor: critical for typed parameters on a real keyboard.
- Shared screen geometry constants: prevents every app from inventing layout.
- Feature modules as independent leaf actions: good mental model for an app launcher.
- Themes as data plus functions: better than hard-coded colors in every app.
- SD helper layer: essential for assets and save data.
- Hardware safety parking during boot: especially IR and expansion pins.
- Lazy radio startup: do not start Wi-Fi/BLE/GPS at boot unless the user opens a feature.

## Patterns To Avoid Or Improve

- Avoid making the core product identity offensive/security-only. Build a general app/game platform with owner-authorized diagnostics as one category.
- Avoid a single huge firmware with every idea compiled in. Prefer an appkit plus separate project folders, or a launcher with selectively built apps.
- Avoid hard-pinned custom wireless stack patches unless a project explicitly needs them and they are verified on the target unit.
- Avoid assuming PSRAM. Official Cardputer-Adv docs found in this research pass list 8 MB flash but no PSRAM.
- Avoid boot-time radio/background tasks unless they are part of the current foreground app.
- Keep legal/safety boundaries visible in project docs for any Wi-Fi/BLE/IR/RF tools.
- Avoid stale docs drift. Generate README, website, app registry, and flasher manifests from shared metadata where possible.
- Avoid hidden hardware assumptions. Every app should declare required hardware, pins, radio state, SD needs, and risk level.

## Better Version Direction

Build `Cardputer Adv AppKit`: a polished app/game launcher and SDK-like template for the unit.

Core apps:

- Launcher with letter hotkeys, typed search, categories, recents, and settings.
- Device diagnostics: keyboard, display, SD, battery estimate, I2C scan, pins reference.
- IR remote builder for owned devices.
- IMU games and toys.
- Notes/decks/cheatsheets stored on SD.
- BLE/Wi-Fi owner diagnostics.
- Sprite/audio playground.
- Serial terminal over EXT UART.

Platform improvements over Poseidon:

- App manifest system: id, label, hotkey, category, storage path, required hardware, permissions, radios, risk level.
- Hardware capability badges: base Cardputer, LoRa, Hydra/RF, C5, W5500, nRF52, Grove sensors.
- Safer defaults: games/utilities/diagnostics first; lab/authorized-use gates for dual-use modules.
- Sandboxed storage: `/apps/<id>/` per app, exportable by USB mass storage or SD card.
- On-device self-test and docs generated from the app registry.

Stretch ideas:

- Web flasher with multiple firmware profiles.
- SD app packs and asset packs.
- Companion node support for safe/benign sensors first.
- Save-game and settings schema.
- Tiny UI component library.

## Immediate Next Build

Create `templates/cardputer-adv-appkit/` with:

- PlatformIO config.
- `input_poll()`.
- status/body/footer UI.
- static app registry.
- sample apps: keyboard test, display test, SD browser stub, IMU tilt ball, IR sender test.

This gives every future app/game a clean base instead of copying Poseidon's whole firmware.
