# App And Game Opportunity Map

Generated: 2026-06-13

## Product Thesis

The Cardputer-Adv is strongest when the app is built around three things normal ESP32 boards lack:

- a real 56-key keyboard
- a tiny but usable color display
- pocket hardware features: IR, IMU, audio, microSD, Wi-Fi/BLE, Grove/EXT

The best direction is a personal pocket computer and app/game deck, not a clone of a single security firmware.

## Highest-Value Projects

| Idea | Why It Fits | MVP | Stretch |
| --- | --- | --- | --- |
| Cardputer Adv AppKit | Creates reusable base for every future project | PlatformIO template, input/UI/SD helpers, 5 sample apps | Web flasher, app pack system |
| Pocket Launcher OS | Keyboard-first home screen for many tools | Categories, hotkeys, settings, SD config | Install apps from SD, themes, recents |
| Command Deck Launcher | Best synthesis of the research lanes: app hub plus practical built-ins | Launcher, BLE macro deck, IR remote, Wi-Fi owner scanner, notes/tasks, IMU toy | Signed app manifests, SoftAP config, app permissions, crash reports |
| IMU Arcade Pack | Adv has BMI270; original Cardputer did not | Tilt maze, balance ball, reaction game | Physics engine, high scores, haptics via audio |
| IR Remote Studio | Adv has IR and keyboard for naming buttons | NEC sender, profile editor, SD profiles | Learn/capture via external receiver, TV/device database |
| Tiny Notes / Cheatsheets | Keyboard + SD are perfect for offline text | Markdown-ish notes, search, bookmarks | Sync via Wi-Fi, encrypted notes |
| Serial Field Terminal | EXT exposes UART pins | 115200 terminal, line input, log to SD | baud scan, macros, protocol decoders |
| Wi-Fi Owner Diagnostics | Useful and legal when scoped to owned networks | scan, RSSI history, channel view | roaming logger, captive portal tester |
| BLE Owner Toolbox | BLE 5 LE plus keyboard UI | scan, filter, known device labels | tracker finder, GATT browser |
| BLE HID Macro Deck | 56-key keyboard makes the unit genuinely useful as a pocket keyboard/controller | pair as BLE keyboard, macro profiles, media keys | per-app profiles, phone/web config, mouse/gamepad modes |
| Audio Toys | ES8311 + speaker/headphone jack | metronome, tone synth, sample playback | voice memo, tiny tracker/sequencer |
| Sensor Deck | Grove + I2C makes plug-in sensors easy | I2C scan, sensor dashboard | graphing, logging, alert rules |

## Game Ideas

- `Tilt Labyrinth`: use BMI270 to roll a pixel ball through mazes.
- `Packet Runner`: arcade dodger themed around signal lanes, no real wireless needed.
- `Micro Rogue`: keyboard-driven roguelike designed for 240 x 135.
- `Word Deck`: typing speed, spelling, anagrams, terminal-style puzzles.
- `IR Tag`: local game using IR send/receive only if external receiver hardware is added; otherwise TX-only training mode.
- `Space Courier`: simple sprite game with SD-loaded levels and chiptune audio.

## Utility Ideas

- `Owner Network Notebook`: save Wi-Fi notes, router location, channel history.
- `Battery Lab`: per-app battery drain estimates and logs.
- `Pinout Pocket Reference`: searchable built-in Cardputer-Adv pinout/gotchas.
- `M5 Unit Tester`: Grove sensor detector and display.
- `SD Deck`: browse `.txt`, `.md`, small bitmap sprites, and audio assets.
- `Macro Pad`: USB HID keyboard macros for the user's own computer.

## Better-Than-Poseidon Product Ideas

- Make the UI framework reusable and documented instead of only embedded inside one giant firmware.
- Build benign defaults: games, utilities, diagnostics, learning tools.
- Put any security tooling behind explicit owner-authorized labels and keep it diagnostic-first.
- Make assets and user data portable on SD.
- Support separate project firmware images, not just one huge binary.
- Treat hardware self-test as a first-class app.
- Provide a clean template for new apps with a fixed visual system.

## First Three Projects To Build

1. `cardputer-adv-appkit` template.
2. `command-deck-launcher` using the template, with BLE macro deck, IR remote, Wi-Fi owner scanner, notes/tasks, and IMU toy.
3. `imu-arcade-pack` as the first fun demo that proves the Adv-specific IMU path.
