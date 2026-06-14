# Toolchains And Libraries

Generated: 2026-06-13

## Recommended Default Stack

Use PlatformIO + Arduino first:

```ini
[env:m5stack-cardputer]
platform = espressif32@6.7.0
board = esp32-s3-devkitc-1
framework = arduino
upload_speed = 1500000
build_flags =
    -DESP32S3
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Cardputer=https://github.com/m5stack/M5Cardputer
```

This config appears in M5Stack Cardputer and Cardputer-Adv docs. It is a conservative starting point for apps and games.

## Officially Listed Platforms

M5Stack lists these development platforms for Cardputer-Adv:

- Arduino
- UiFlow2
- ESP-IDF
- PlatformIO

Use UiFlow2 for quick visual experiments. Use ESP-IDF for lower-level firmware, advanced power management, custom USB behavior, or when Arduino abstractions get in the way.

## Core Libraries

- `M5Cardputer`: board support wrapper and examples for display, keyboard, SD, IR, and peripherals.
- `M5Unified`: common M5Stack peripheral APIs.
- `M5GFX`: display driver layer, based on LovyanGFX.
- `Arduino-IRremote`: M5Stack's IR NEC example uses it with `IR_TX_PIN 44`.
- ESP32 Arduino Wi-Fi/BLE APIs or NimBLE-Arduino: useful for connectivity apps. Prefer defensive/owner-authorized use cases.

M5Stack's Arduino quick start notes that `M5Cardputer.update()` belongs in the main loop so keyboard state refreshes correctly.

Research lane 2 observed that the M5Cardputer GitHub repo shows release `1.2.0` on 2026-06-08, while the raw manifest fetched during that lane still reported `1.1.1`. For reproducible builds, pin by exact tag/commit after verifying the library release on the target machine.

## Specialized Library Leads

- BLE HID macro deck: `ESP32-BLE-Keyboard`; consider NimBLE-backed alternatives when RAM matters.
- BLE scanning/client/server: `NimBLE-Arduino`.
- IR send/receive ecosystem: M5Stack examples use `Arduino-IRremote`; `IRremoteESP8266` is worth evaluating for broader protocol databases.
- Audio apps: `M5Unified` audio examples first; `arduino-audio-tools` for generators, codecs, DSP, streams, and I2S workflows.
- Widget-heavy utility screens: `LVGL`; direct `M5GFX`/LovyanGFX remains better for small fast games.

## Starter Project Shape

Recommended minimal app/game scaffold:

```text
project-name/
  platformio.ini
  src/
    main.cpp
    app_config.h
    input.cpp
    input.h
    ui.cpp
    ui.h
    scene.cpp
    scene.h
  assets/
    README.md
```

For a multi-app launcher:

```text
src/
  main.cpp
  app_registry.cpp
  app_registry.h
  input.cpp
  input.h
  ui/
    theme.cpp
    tiny_layout.cpp
  apps/
    notes.cpp
    ir_remote.cpp
    imu_game.cpp
    wifi_diag.cpp
    ble_tools.cpp
```

## UI/Game Loop Rules

- Call `M5Cardputer.update()` every loop or through a shared `input_poll()`.
- Avoid long blocking scans in the foreground. Use state machines or background tasks.
- Redraw only dirty areas where possible; full-screen redraws are visible on 240 x 135 when frequent.
- Keep text rows predictable. Design around a status bar, body, and footer.
- Use letter mnemonics and typed input; the keyboard is the device's biggest advantage.
- Store larger sprites/audio/config on microSD, but keep a no-SD fallback screen.

## Simulator / Host Testing Options

No official full Cardputer-Adv emulator was found.

Useful partial approaches:

- Wokwi ESP32-S3: can simulate ESP32-S3 firmware, Wi-Fi, SPI, GPIO, timers, and GDB, but not Bluetooth and not the exact Cardputer board.
- LovyanGFX PC drawing targets: useful for graphics-loop experiments with SDL2/OpenCV-style drawing backends.
- LVGL PC workflow: useful if the app uses LVGL widgets.
- ESP-IDF host apps: useful for logic/component tests, not hardware-accurate Cardputer testing.

Keep game logic and parsers independent from M5 APIs so they can be host-tested even when hardware cannot be emulated.

## Project Template To Build Next

Create a `cardputer-adv-appkit` template under `templates/` with:

- PlatformIO Arduino config.
- Tiny UI primitives: status bar, footer, menu, modal line editor.
- Keyboard event wrapper.
- SD mount helper.
- App registry.
- Example apps: Hello, Keyboard Test, Display Test, IMU Tilt, IR Blink/Remote Shell.
- Build/readme instructions.

Loop shape:

- `setup()`: initialize `M5Cardputer.begin(cfg, true)`, display rotation, sprite/canvas allocation, SD if needed.
- `loop()`: call `M5Cardputer.update()`, read keyboard, update state on a fixed timestep, draw to `M5Canvas`, then `pushSprite()`.
- Keep hardware/radio/audio drivers behind small app services so games can be tested without a device.

## Verification Checklist

- Builds with PlatformIO.
- Upload enters over USB-C with documented download mode fallback.
- Keyboard test shows all key events.
- Display test draws across the full 240 x 135 screen.
- SD mount succeeds/fails gracefully.
- IR pin stays idle unless an IR app is open.
- Wi-Fi/BLE apps do not start radios at boot unless explicitly needed.
