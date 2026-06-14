# AdvDeck Notes PRD

Generated: 2026-06-13

## Summary

AdvDeck Notes is a keyboard-first notes and snippet utility for M5Stack Cardputer Adv. It turns the device into a practical pocket notebook, snippet launcher, and later macro deck.

## Target User

- Cardputer/Cardputer Adv owner who wants a useful app beyond demos and pentest firmware.
- Builder who wants a reliable text/snippet base before adding radios and companion hardware.
- Field user who wants notes, checklists, serial snippets, commands, or quick text output without a laptop.

## User Problems

- Existing ecosystem is fragmented across launchers, attack firmware, demos, and rough OS experiments.
- Text editing / notepad / PDA workflows are repeatedly requested but not polished.
- Reflashing and app switching are painful when an app is not Launcher-friendly.
- Many firmwares break on exact device variant, keyboard layout, SD, or partition assumptions.

## Core Experience

First screen is the app, not a splash-heavy landing page.

Main menu:

- `N` Notes
- `S` Snippets
- `F` Find
- `D` Device
- `H` Help
- `Q` Sleep/quit/reboot options

Notes:

- list notes by filename/title
- create note
- edit note
- rename
- delete with confirmation
- save as plain `.txt`

Snippets:

- create snippet
- edit snippet
- type snippet over USB HID after confirmation
- rate limit / delay option

Device:

- keyboard test
- SD status
- battery/status
- firmware version
- Cardputer Adv detected capabilities

## Storage

Primary:

- `/advdeck/config.json`
- `/advdeck/notes/*.txt`
- `/advdeck/snippets/*.txt`
- `/advdeck/logs/`

Fallback:

- NVS for settings
- built-in demo note/snippet when SD missing

## Technical Stack

- PlatformIO
- Arduino framework
- `M5Cardputer`
- `M5Unified`
- `M5GFX`
- SD / FS APIs
- USB HID library path to be selected during implementation

## Architecture

Use the appkit structure from `roadmap/first-app-roadmap.md`:

- `platform`: board/input/display/storage abstractions
- `ui`: status bar, menu, text editor
- `app`: app registry and app entry points
- `services`: battery, HID, future companion link

## Safety / Trust

- Never type HID output without a visible confirmation.
- Never auto-run snippets.
- Never start radios at boot.
- Never require SD to boot.
- Never hide install/rollback limits.

## MVP Acceptance Tests

- Boots to menu.
- All Cardputer Adv keys used by editor are recognized.
- Creates a note and persists it on SD.
- Opens note after reboot.
- Edits and saves note.
- Deletes note only after confirmation.
- Creates snippet.
- Types snippet to host over USB HID only after confirmation.
- Shows useful degraded mode with no SD.
- Build passes from clean checkout.

## Future Features

- BLE HID.
- WebUI file transfer.
- IR remote profile manager.
- ESP-NOW field notes.
- LoRa/GPS note tagging.
- C5 companion status and receive-only radio diagnostics.
