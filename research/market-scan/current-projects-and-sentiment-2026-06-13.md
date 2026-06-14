# Current Project Landscape And Sentiment

Generated: 2026-06-13

## Executive Read

The Cardputer ecosystem is active, but crowded in two places:

- attack/pentest mega-firmware
- firmware launchers / firmware switching

The underserved gap is a polished daily-driver app that treats the Cardputer Adv as a real pocket computer: notes, snippets, macros, file transfer, safe diagnostics, and later companion hardware.

## Ranked Signals

| Rank | Project | What People Like | Friction / Complaints | Lesson |
| --- | --- | --- | --- | --- |
| 1 | ESP32Marauder | Mature Wi-Fi/BLE toolkit, broad hardware support, Cardputer/ADV binaries | Many hardware variants, nightly/pre-release caveats, large issue backlog | Serious tools need exact build targeting and docs |
| 2 | Bruce | Huge all-in-one feature breadth, WebUI, SD manager, scripting, Cardputer/ADV support | Rapid churn, keyboard/UI/crash fixes, issue backlog, hardware pin complexity | Users love breadth, but stability and setup cost are high |
| 3 | Evil-M5Project | Approachable M5/Cardputer-focused Wi-Fi exploration | Weaker packaging, no releases, monolithic sketch style | Good ideas need release discipline |
| 4 | M5Launcher / Launcher | OTA, SD installs, WebUI, partition manager, firmware sanity checks, multi-firmware installation | Storage/partition/device quirks keep recurring | Any serious app should be launcher-friendly from day one |
| 5 | NEMO | Playful defensive/prank framing, TV-B-Gone, attack detection, BadUSB Hunter | Blocking scans, occasional unresponsive buttons, USB-C power caveats | Defensive detection tools are loved when simple |
| 6 | Picoware / tiny OS style projects | Code editor, REPL, file manager, app store, OTA, media viewer/player, games | Cross-device scope can dilute Cardputer polish | Daily-driver pocket-computer direction is attractive |
| 7 | Plai / LoRa terminal style | Standalone Cardputer Adv messaging with LoRa cap and keyboard | Narrower accessory dependency | Field communicator is a strong phase-two lane |
| 8 | M5Cardputer library | Canonical Cardputer + ADV support | Library, not a product | Use it as compatibility baseline |
| 9 | Doom / emulators / games | Fun proof that the unit can run serious ports | RAM/PSRAM constraints, SD/WAD setup, input bugs | Games are great demos, not the first foundation |
| 10 | AdvanceOS / utility OS projects | Music, file explorer, painter, IR editor, games, Wi-Fi spectrum, partition viewer | Rough and aspirational | People want “actual pocket computer,” but polish is missing |
| 11 | Poseidon | Keyboard-first hotkeys, typed parameters, themes, Cardputer-Adv focus, companion node idea | Smaller community, offensive/security-heavy identity | Copy interaction model, not the whole product category |

## Public Pain Signals

- Users like being able to switch `.bin` apps on-device with Launcher, and specifically call out switching between Nemo, Bruce, Marauder, and others.
- Users report Cardputer Adv noob friction: Doom stuck at home screen, keyboard input not working, UIFlow/Cardputer-Adv demo oddness, serial/device configuration errors.
- Users ask for word processor, text editor, notepad, and PDA-like OS flows.
- Launcher release notes repeatedly fix keyboard, SD, partition, rotation, firmware-name, app-size, and WebUI issues. That tells us the pain is not just “more features”; it is reliability and compatibility.
- Bruce and NEMO docs both surface real hardware caveats: blocking scans, shared SPI, power/current limits, SD availability, keyboard variants, and optional external modules.

## Things People Love

- One-click / no-USB install paths: M5Burner, web flashers, OTA, SD install.
- App switching and app catalogs.
- Keyboard-first navigation.
- Big “Swiss army knife” firmware bundles.
- Defensive tools that explain what is happening.
- SD-backed files, captures, logs, and assets.
- WebUI for configuration and file management.
- Small games and novelty ports.

## Things People Dislike

- Reflashing friction.
- Wrong binary for the exact hardware variant.
- Keyboard mismatches on Cardputer Adv.
- SD card formatting and mount failures.
- Partition/app size incompatibility.
- Monolithic firmware that is hard to update safely.
- Hardware pin conflict confusion, especially RF modules.
- Blocking scans that freeze input.
- Docs drifting behind releases.

## Market Gap

Build a polished **Pocket Notes + Snippet Deck** first:

- A daily-use app, not another attack firmware.
- Uses the keyboard and SD card immediately.
- Can later grow into macro deck, IR profile manager, ESP-NOW/LoRa notes, and companion diagnostics.
- Fits Launcher/M5Burner expectations.
- Lower legal and hardware risk than RF-heavy first releases.

## Sources

- ESP32Marauder: https://github.com/justcallmekoko/ESP32Marauder
- Bruce: https://github.com/BruceDevices/firmware
- Evil-M5Project: https://github.com/7h30th3r0n3/Evil-M5Project
- Launcher: https://github.com/bmorcelli/Launcher
- NEMO: https://github.com/n0xa/m5stick-nemo
- Awesome Cardputer list: https://github.com/terremoth/awesome-m5stack-cardputer
- Poseidon: https://github.com/GeneralDussDuss/poseidon
- M5Stack Cardputer Adv noob thread: https://community.m5stack.com/topic/7991/cardputer-adv-noobie
- Reddit app launcher thread: https://www.reddit.com/r/CardPuter/comments/1aq4zuq/bin_app_launcher/
- Reddit word processor/text editor demand references gathered by research lane:
  - https://www.reddit.com/r/CardPuter/comments/1actvi2/word_processor/
  - https://www.reddit.com/r/CardPuter/comments/1gnfh74/texteditor_firmware_complete/
  - https://www.reddit.com/r/CardPuter/comments/1eso7p0/os_for_the_cardputer/
