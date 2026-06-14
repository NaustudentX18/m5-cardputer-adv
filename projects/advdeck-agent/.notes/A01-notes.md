# A01 — Firmware Skeleton: notes

## What landed
- `platformio.ini` — PlatformIO + Arduino + M5Cardputer + M5Unified + M5GFX + nlohmann/json, with a local board file at `boards/m5stack-cardputer.json` (vendored from `/home/pi/Launcher/boards/_jsonfiles/m5stack-cardputer.json`).
- `boards/m5stack-cardputer.json` + `boards/pinouts/m5stack-cardputer.h` — local board definition; the standard espressif32 platform does not ship a `m5stack-cardputer` board.
- `src/main.cpp` — `setup()` boots M5, mounts SD via `SdStorage`, draws boot screen; `loop()` drives the home route.
- `src/platform/{keyboard,display}.h+.cpp` — keyboard wrapper over `M5Cardputer.Keyboard`, display facade over `LGFX_Sprite` (heap-allocated back buffer).
- `src/ui/{status_bar,menu}.h+.cpp` — top status bar with SD indicator; key-driven menu.
- `src/app/{routes.h, routes.cpp}` — dispatcher with stubs for Home, Capture, ProjectList, ProjectDetail, TaskList, Calendar.

## Decisions
1. **Local board file.** `m5stack-cardputer` does not exist in `espressif32` 6.x, 7.x, or the pioarduino fork. The Launcher on this machine works around it with a local `boards/m5stack-cardputer.json` + `boards_dir`; we copied that pattern and used the same pin flags (backlight=38, TFT_*=33-37, SDCARD_*=12/14/39/40, TCA8418_*=8/9/11).
2. **Sprite back buffer.** `display.h` creates a heap `LGFX_Sprite` in `begin()` and pushes it to the panel in `push()`. Direct M5GFX `M5Canvas` does not work because `M5GFX` declares `M5Canvas` as a using alias; the cleanest path is `LGFX_Sprite` from the same header.
3. **`std::clamp` portability.** Replaced with a hand-rolled clamp in `menu.cpp` because the ESP32 Arduino libc++ does not always expose `std::clamp`.
4. **`Ctx` aggregate init.** Added an explicit constructor in `routes.h` so `main.cpp` can brace-initialize `Ctx` even though it has default member initializers (which makes it a non-aggregate in C++14). The C++17 default for the Arduino toolchain is C++14.
5. **Esc key.** The Cardputer-Adv has no physical Esc key. We map `opt` (the row above the letters, combined with fn/shift) to `KeyEvent::escape` for the dispatcher. A12 will tighten this.
6. **`host_storage.cpp` is host-only.** Excluded from the firmware build by `build_src_filter` in `platformio.ini` AND guarded with `#ifndef ADVDECK_FIRMWARE` inside the file as defense in depth. The file uses `<filesystem>`, which the ESP32 libc++ does not have.
7. **`SdStorage` is a stub for Phase 1.** Every method except trivial accessors returns an error or false. The boot screen shows "SD:NONE" and the home menu still renders. A02 owns the real SD impl; this task only verifies that the wiring compiles.
8. **Nlohmann/json lib_deps.** Added even though the firmware doesn't use it directly; future routes (Tasks JSON) will.

## Verification
`/home/pi/.platformio/penv/bin/platformio run` exits 0 with `[SUCCESS] Took 38s`. Output binary `.pio/build/m5stack-cardputer/firmware.bin` is 500 KB (15% of 3.3 MB flash). RAM usage 22 KB (6.9% of 320 KB). Host tests still pass: 23/23.

## Out of scope (left for later tasks)
- Real `SdStorage` (Phase 2+ when the bridge starts using it)
- Capture / Project detail / Task list / Calendar route bodies (A03, A04, A05)
- Larger fonts, multiple text sizes, screens wider than 240 px (A12)
- Wi-Fi / BLE / radio init (Phase 7 — explicitly disabled)
