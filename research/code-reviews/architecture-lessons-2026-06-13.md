# Architecture Lessons From Current Projects

Generated: 2026-06-13

## Bottom Line

Borrow the architecture direction from Bruce, Poseidon, and Launcher, but keep the first app much smaller:

- Bruce: modular menu/app surface and board overlays.
- Poseidon: Cardputer Adv focus, keyboard-first UX, companion firmware split, release checklist discipline.
- Launcher: install/partition/app-registry awareness.
- ESP32Marauder: cooperative subsystem loops and hardware abstraction ideas.
- Evil-M5Project/NEMO: simple approachable experiences, but avoid monolithic sketch style.

## Recommended First-App Shape

```text
src/
  main.cpp
  app/
    App.h
    registry.cpp
  platform/
    BoardProfile.h
    CardputerAdv.cpp
    Display.cpp
    Input.cpp
    Storage.cpp
  ui/
    Menu.cpp
    StatusBar.cpp
    TextEditor.cpp
  services/
    Battery.cpp
    HidOutput.cpp
    CompanionLink.cpp   # phase two, optional
boards/
  m5stack-cardputer-adv.ini
docs/
  release-checklist.md
platformio.ini
```

## Core Patterns To Adopt

### Small Static App Registry

Use one registry as the source of truth:

```cpp
struct AppDescriptor {
  const char* id;
  const char* label;
  char hotkey;
  bool (*isAvailable)();
  void (*run)(AppContext&);
};
```

Do not wire app selection directly into `main.cpp`.

### Platform Abstraction

Cardputer Adv quirks belong in a platform layer:

- keyboard: TCA8418 behavior, printable keys, modifiers, escape/back mapping
- display: 240 x 135 geometry, rotation, brightness
- storage: SD present/absent, config paths, default fallback
- power: battery/status/sleep
- board profile: pins, I2C addresses, feature flags

### Optional SD

Users expect SD, but first boot must not fail without it.

Use:

- `/advdeck/config.json`
- `/advdeck/notes/`
- `/advdeck/snippets/`
- `/advdeck/logs/`
- NVS fallback for settings
- built-in default snippets when SD is missing

### Launcher-Friendly Builds

PlatformIO envs:

- `cardputer-adv`: normal factory firmware.
- `cardputer-adv-launcher`: launcher-compatible binary if needed.
- Later: `c5-companion`: separate companion firmware.

Do not implement partition manipulation inside the first app.

### Companion Devices As Separate Firmware

Follow Poseidon’s split: main Cardputer UI firmware and separate companion-node firmware.

Required protocol concepts:

- version constant
- capability handshake
- message schema
- timeout/error UI
- independent release artifact

## Anti-Patterns To Avoid

- One huge `.ino`.
- Scattered `#ifdef HAS_*` in app code.
- Global display/config objects consumed directly by every feature.
- Huge menu/UI files.
- Megabyte-scale generated data headers for first app assets.
- Build-flag secrets.
- Partition editor in v1.
- Radio transmit features before diagnostics, logging, and region/safety controls exist.

## Code References Inspected

Local snapshots:

- `research/sources/community-projects/bruce/`
- `research/sources/community-projects/launcher/`
- `research/sources/community-projects/nemo/`
- `research/sources/community-projects/esp32-wifi-penetration-tool/`
- `research/sources/poseidon/`

High-signal files:

- Bruce: `src/core/main_menu.cpp`, `src/core/configPins.cpp`, `src/core/mykeyboard.cpp`, `src/core/sd_functions.cpp`, `src/core/menu_items/*`, `platformio.ini`
- Launcher: `src/app_registry.cpp`, `src/mykeyboard.cpp`, `src/partitioner.cpp`, `src/sd_functions.cpp`, `src/webInterface.cpp`, `README.md`
- Poseidon: `src/main.cpp`, `src/input.cpp`, `src/menu.h`, `src/ui.cpp`, `src/sd_helper.cpp`, `c5_node/README.md`
- NEMO: `m5stick-nemo.ino`, `README.md`, `configs/.env.M5Cardputer`

## First Build Architecture Decision

Build a focused app, not a micro-OS:

1. Home menu with hotkeys.
2. Notes list/editor.
3. Snippet deck.
4. HID type-out.
5. Settings/status.
6. Diagnostics stub for SD/keyboard/battery.

This can later become the base for macro deck, IR profiles, LoRa message notes, and C5 companion diagnostics.
