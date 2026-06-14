# Research Agent Briefs

Generated: 2026-06-13

This file condenses the fanned-out research lanes so future sessions can quickly recover the work.

## Lane 1: Hardware / Platform

Key results:

- Official name/SKU: Cardputer-Adv, `K132-Adv`.
- MCU: Stamp-S3A based on ESP32-S3FN8, dual-core LX7 up to 240 MHz, 8 MB flash.
- Display: ST7789V2, 1.14 inch, 240 x 135.
- Keyboard: 56 keys, TCA8418RTWR I2C expander, G8/G9 plus interrupt G11.
- Audio: ES8311 codec, NS4150B amp, 8 ohm 1 W speaker, 65 dB SNR MEMS mic, 3.5 mm output.
- IMU: BMI270.
- IR: G44.
- microSD SPI: G12 CS, G14 MOSI, G40 CLK, G39 MISO.
- Expansion: Grove on G1/G2 plus EXT 2.54-14P bus.
- Gotchas: crowded G8/G9 I2C, EXT overlaps internal buses, G0 download mode, G46 strapping sensitivity, no official PSRAM listing found.

Primary sources:

- https://docs.m5stack.com/en/core/Cardputer-Adv
- https://docs.m5stack.com/en/core/Stamp-S3A
- https://github.com/m5stack/M5Cardputer
- https://github.com/m5stack/M5Cardputer-UserDemo/tree/CardputerADV
- https://www.espressif.com/en/products/socs/esp32-s3

## Lane 2: Toolchains / Libraries

Key results:

- Best default: PlatformIO or Arduino + `M5Cardputer` + `M5Unified` + `M5GFX`/LovyanGFX.
- M5Stack docs say Cardputer Arduino APIs apply to Cardputer and Cardputer-Adv.
- M5Cardputer repo shows latest release `1.2.0` on 2026-06-08; raw manifest observed as `1.1.1`, so pin exact tags/commits.
- ESP-IDF is viable for lower-level firmware but heavier for app/game work.
- No full Cardputer emulator found; Wokwi, LovyanGFX PC drawing, LVGL PC projects, and ESP-IDF host apps are partial options.

Useful links:

- https://docs.m5stack.com/en/arduino/m5cardputer/program
- https://docs.m5stack.com/en/arduino/m5cardputer/keyboard
- https://docs.m5stack.com/en/arduino/m5cardputer/sdcard
- https://github.com/m5stack/M5Unified
- https://github.com/m5stack/M5GFX
- https://github.com/lovyan03/LovyanGFX
- https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/index.html

## Lane 3: Poseidon Inspiration

Key results:

- Poseidon is keyboard-first pentesting firmware for Cardputer-Adv.
- Repo metadata observed: created 2026-04-14, pushed 2026-06-12, default branch `master`.
- Latest commit: `24b518b4080cbc0b58d917d98fc1e90f1ba1465a`, `2026-06-12T04:52:25Z`.
- Latest release: `v0.6.2 — sub-GHz analyzer, Argus awakens`, published `2026-06-12T04:53:02Z`.
- Best reusable patterns: mnemonic hotkeys, typed parameters, tiny dashboards, theme system, app/feature modules, companion-node architecture, GitHub Pages/web flasher docs.
- Main risks: dual-use/offensive feature set, hardware fragmentation, active stability issues, docs drift, mixed aspirational vs implemented roadmap items.

Useful links:

- https://github.com/GeneralDussDuss/poseidon
- https://github.com/GeneralDussDuss/poseidon/releases/tag/v0.6.2
- https://generaldussduss.github.io/poseidon
- Local clone: `research/sources/poseidon/`

## Lane 4: App/Game Opportunities

Best recommendation:

Build a `Command Deck Launcher` with five serious built-ins:

- BLE macro deck
- IR remote
- Wi-Fi owner scanner/logger
- offline notes/tasks
- IMU game/toy

Why:

- Uses the Adv-specific hardware instead of cloning a generic ESP32 demo.
- Stays feasible in Arduino/PlatformIO.
- Creates a platform for future apps and games.
- Keeps the default product safer and broader than Poseidon.

Useful library leads:

- BLE HID: https://github.com/T-vK/ESP32-BLE-Keyboard
- BLE core: https://github.com/h2zero/NimBLE-Arduino
- IR: https://github.com/crankyoldgit/IRremoteESP8266
- Audio: https://github.com/pschatzmann/arduino-audio-tools
- Wi-Fi/ESP-NOW: Espressif Arduino/ESP-IDF docs.
