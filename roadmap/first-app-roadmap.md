# First M5 Cardputer Adv App Roadmap

Generated: 2026-06-13

## Product Decision

First app: **Pocket Notes + Snippet Deck**.

Working name: **AdvDeck Notes**.

Why this first:

- People explicitly want text editor / notepad / PDA-like workflows.
- It uses the Cardputer’s strongest hardware: real keyboard, tiny screen, SD card, USB/BLE HID potential.
- It is safer and less crowded than another pentest mega-firmware.
- It becomes the foundation for macro deck, IR profile manager, ESP-NOW/LoRa field notes, and C5 companion status tools.
- It can ship as one clean Launcher/M5Burner-friendly binary.

## MVP Definition

The first build is successful when the user can:

- boot directly into a clean keyboard-first interface
- create, edit, rename, delete notes
- save notes as plain `.txt` files on SD
- search notes by title/content prefix
- open a Snippets view
- send a selected snippet/note as USB HID keyboard output
- use the app with SD absent, with a clear degraded mode
- see battery/status/storage indicators
- install it as a normal firmware binary and, later, a Launcher-compatible binary

## Non-Goals For MVP

- No partition manager.
- No app store.
- No RF transmit features.
- No C5 companion requirement.
- No LoRa/GPS requirement.
- No cloud sync.
- No rich Markdown renderer.
- No encryption in v1 unless time remains after core reliability.

## Roadmap

### Phase 0: Repo And Template

Outcome: compileable skeleton.

- Create `projects/advdeck-notes/`.
- PlatformIO Arduino config for Cardputer Adv.
- Add `src/platform`, `src/ui`, `src/app`, `src/services`.
- Boot screen, status bar, main menu.
- Hardware smoke app: keyboard, display, SD, battery/status.

Validation:

- `pio run -e cardputer-adv` passes.
- Hardware smoke test documented.

### Phase 1: Storage And Notes

Outcome: reliable offline notes.

- Storage facade over SD plus NVS fallback.
- Create `/advdeck/notes/`.
- Note list.
- Text editor with cursor, backspace, enter/newline, save/cancel.
- Rename/delete.
- Prefix search.

Validation:

- SD absent mode works.
- SD present mode persists notes.
- Power-cycle note persistence.
- Long note and empty note behavior tested.

### Phase 2: Snippet Deck And HID

Outcome: daily utility hook.

- `/advdeck/snippets/` file format.
- Snippet list and edit flow.
- USB HID type-out for selected snippet.
- Basic delay/rate setting.
- Safe confirm before type-out.

Validation:

- Types into a local text editor.
- Cancels cleanly.
- Does not type unexpectedly at boot.

### Phase 3: Polish And Launcher Compatibility

Outcome: usable release.

- Theme: high-contrast default plus dim mode.
- Sleep/dim controls.
- Help/about screen.
- Error screens for SD/USB/HID.
- Release checklist.
- Binary naming.
- Launcher-compatible build if needed.

Validation:

- Install from direct flash.
- Install via Launcher if build supports it.
- SDHC FAT32 card tested.
- No-SD path tested.

### Phase 4: Extensions

Candidates after MVP:

- BLE HID macro mode.
- WebUI over SoftAP for file transfer/config.
- IR profile manager.
- ESP-NOW note drop / field chat.
- LoRa/GPS field-note tagging on the M5 LoRa/GPS cap.
- C5 companion status panel and receive-only diagnostics.

## Companion Hardware Track

The C5 Grove radio companion should not block the first app.

Track it in parallel after the notes app skeleton exists:

1. Grove UART HELLO/PING/status.
2. Capability handshake.
3. C5 native radio status: Wi-Fi 5 GHz / 2.4 GHz / 802.15.4.
4. CC1101 receive-only scan.
5. nRF24 receive/noise scan.
6. Cardputer UI panel inside AdvDeck.
7. Region/safety gates before any transmit behavior.

Reference: `research/hardware/c5-grove-radio-companion.md`.

## Success Criteria For First Public Release

- Install instructions are one page and exact for Cardputer Adv.
- App does not require SD to boot.
- With SD, notes and snippets are plain files.
- Keyboard works correctly on Cardputer Adv, including symbols that Launcher recently had to fix.
- No feature starts Wi-Fi/BLE/radio at boot.
- No hidden destructive behavior.
- Release includes known limitations and rollback instructions.

## Build Philosophy

Make the first release boringly useful.

The flashy pieces can come later. The thing people are missing is a reliable daily-driver utility that respects the tiny screen, the keyboard, SD files, and install/rollback reality.
