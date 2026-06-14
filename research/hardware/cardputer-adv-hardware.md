# Cardputer-Adv Hardware Notes

Generated: 2026-06-13

## Identity

- Official product name: `Cardputer-Adv`.
- Store/SKU naming: `M5Stack Cardputer Adv Version (ESP32-S3)`, SKU `K132-Adv`.
- M5Stack positions it as a programmable card-sized computer powered by Stamp-S3A.

Sources: M5Stack docs and shop page.

## Core Specs

| Area | Detail |
| --- | --- |
| Core module | Stamp-S3A |
| MCU | ESP32-S3FN8, dual-core Xtensa LX7, up to 240 MHz |
| Flash | 8 MB |
| External storage | microSD |
| Display | ST7789V2, 1.14 inch, 240 x 135 px |
| Keyboard | 56 keys, 4 x 14, 160 gf |
| Keyboard expander | TCA8418RTWR on Cardputer-Adv |
| Audio codec | ES8311 |
| Speaker | NS4150B amplifier + 8 ohm / 1 W speaker |
| Microphone | MEMS microphone, 65 dB SNR |
| Audio jack | 3.5 mm output; inserted headphone disables speaker amp |
| IMU | BMI270 6-axis |
| IR | 1 IR emitter |
| Battery | 1750 mAh Li-ion |
| Expansion | HY2.0-4P Grove + EXT 2.54-14P bus |
| Size | 84.0 x 54.0 x 19.6 mm |
| Weight | 81.0 g |

## Adv vs Original Cardputer

Official comparison highlights:

- Core: Cardputer-Adv uses Stamp-S3A; original Cardputer used Stamp-S3.
- Battery: Adv uses one 1750 mAh battery; original/v1.1 list 120 mAh + 1400 mAh.
- Audio: Adv uses ES8311 + NS4150B and adds 3.5 mm audio output.
- IMU: Adv adds BMI270.
- Keyboard scan: Adv uses TCA8418RTWR; original/v1.1 used 74HC138.
- Expansion: Adv has HY2.0-4P plus EXT 2.54-14P; earlier versions only list HY2.0-4P.
- Antenna: Adv and v1.1 use optimized antenna design.
- Mechanical: Adv adds lanyard hole.

## Pin / Bus Notes

These are the pins to treat as constrained first when designing apps and expansion hardware:

- Display: backlight G38, reset G33, DC/RS G34, data G35, SCK G36, CS G37.
- Keyboard: TCA8418 over I2C on G8 SDA, G9 SCL, interrupt G11.
- Audio: ES8311 control shares I2C G8/G9; I2S uses G41 SCLK, G46 ASDOUT, G43 LRCK, G42 DSDIN.
- IMU: BMI270 shares I2C G8/G9.
- IR TX: G44.
- microSD SPI: CS G12, MOSI G14, CLK G40, MISO G39.
- Grove / PORT.CUSTOM: GND, 5V, G2, G1.
- EXT 2.54-14P: G3 reset, G4 INT, G6 BUSY, G40 SCK, G14 MOSI, G39 MISO, G5 CS, 5VIN, GND, 5VOUT, G8 SDA, G9 SCL, G13 UART_RX, G15 UART_TX.
- Boot/download: hold G0 while applying power with side switch OFF to enter download mode.
- Charging: M5Stack says switch power ON while charging.

## Development Gotchas

- G8/G9 are busy. Keyboard, audio control, IMU, and external I2C all share them. Avoid heavy blocking I2C polling in game loops.
- EXT exposes pins already used internally. G40/G14/G39 overlap microSD SPI; G8/G9 overlap internal I2C; design hats carefully.
- G0 is part of boot/download behavior. Do not treat it like a normal in-game button without understanding reset/download implications.
- ESP32-S3 strapping-sensitive pins can cause boot surprises. Hardware notes from the research lane specifically flagged G46: do not pull it high before boot without checking the Stamp-S3A documentation.
- No PSRAM is listed in the official Cardputer-Adv specs found during this pass. Code should fit 8 MB flash and normal ESP32-S3 RAM unless hardware testing proves otherwise.
- The screen is tiny but wide: 240 x 135. Design for 7-10 rows of dense text, coarse sprites, and strong contrast.

## Best Hardware-First App Ideas

- IR remote / macro remote.
- Tilt/IMU games.
- Pocket terminal / serial monitor over EXT UART.
- BLE tracker finder for owned devices.
- Wi-Fi diagnostics for owned networks.
- SD-backed notes, decks, cheatsheets, and save games.
- Audio toys: sampler, tuner, metronome, tiny synth, voice memo if ES8311 capture is verified.
