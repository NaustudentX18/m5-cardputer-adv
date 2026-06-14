# ESP32-C5 Grove Radio Companion Concept

Generated: 2026-06-13

## Concept

Use the Cardputer-Adv as the UI, keyboard, display, storage, LoRa/GPS host, and app launcher. Add a small ESP32-C5 companion board plugged into the Cardputer-Adv Grove connector as a radio coprocessor.

The M5Stack LoRa/GPS cap stays on the Cardputer-Adv EXT/header path. The C5 companion gets its own attached radios, such as CC1101 and nRF24L01+/2.4 GHz modules, plus its own antenna connectors.

## Recommended Split

Cardputer-Adv:

- UI, keyboard, display, menus, SD storage, config, logs.
- LoRa/GPS cap control.
- High-level command orchestration.
- Safe owner-authorized app flows.

ESP32-C5 companion:

- 5 GHz / 2.4 GHz Wi-Fi and 802.15.4 work native to ESP32-C5.
- CC1101 sub-GHz radio over the C5 SPI bus.
- nRF24L01+ / 2.4 GHz module over the C5 SPI bus.
- Radio timing, packet capture, scan loops, and buffering.
- Compact result batches back to the Cardputer.

## Grove Link Recommendation

Use Grove as power plus a command/data link, not as the shared SPI radio bus.

Preferred Grove mapping:

- Grove red: 5V to C5 devboard VIN/5V input only.
- Grove black: GND.
- Grove yellow G2: Cardputer TX to C5 RX.
- Grove white G1: Cardputer RX from C5 TX.

Use UART first. It is simpler and avoids the Cardputer-Adv internal I2C crowding on G8/G9. The Grove pins G1/G2 are separate from the Adv keyboard/audio/IMU I2C bus.

Alternative:

- I2C over Grove can work for simple sensor-style command/register traffic, but UART is better for logs, scan results, firmware debug text, and streaming batches.
- ESP-NOW can also be used for wireless Cardputer-to-C5 control, but a plugged Grove companion should use wired UART for reliability and power.

## Radio Wiring On The C5

Put CC1101 and nRF24 on the C5 side, not the Cardputer side.

Suggested pattern:

- Shared C5 SPI: SCK, MOSI, MISO.
- CC1101: separate CS, GDO0/IRQ, optional GDO2.
- nRF24: separate CSN, CE, IRQ.
- Separate 3.3V regulator with enough peak current for radio modules, especially PA/LNA nRF24 boards.
- Keep RF modules close to antenna connectors and away from noisy display/power wiring.

Do not feed 5V into radio modules that expect 3.3V. Use level-safe ESP32-C5 GPIO only.

## Why This Is Better Than Hanging Radios Directly Off The Cardputer

- Keeps the Cardputer EXT pins free for the LoRa/GPS cap.
- Avoids piling CC1101/nRF24 traffic onto the Cardputer microSD SPI bus.
- Lets the C5 handle real-time radio work while the Cardputer keeps UI responsive.
- Gives a clean hardware capability model: Cardputer base + LoRa/GPS cap + C5 radio companion.
- Makes future companion firmware reusable across multiple Cardputer apps.

## Firmware Protocol Shape

Use a small binary packet protocol over UART:

```text
magic      2 bytes
version    1 byte
type       1 byte
seq        2 bytes
length     2 bytes
payload    length bytes
crc16      2 bytes
```

Command classes:

- `HELLO`: board id, firmware version, supported capabilities, heap, radio status.
- `SCAN_START`: radio id, channel/frequency range, duration.
- `SCAN_STOP`: stop current task.
- `TX_REQUEST`: bounded transmit request for owner-authorized tools.
- `RX_BATCH`: compact scan/capture results from C5 to Cardputer.
- `STATUS`: busy/idle/error/overheat/low-power.
- `LOG`: debug/event text.

## Key Risks

- Power budget: Grove 5V may not be enough for a C5 devboard plus PA/LNA radio modules under transmit peaks. Test current draw.
- Voltage mistakes: devboards may accept 5V on VIN/VBUS, but bare C5/radio modules require 3.3V.
- RF layout: random long wires to antennas will perform badly. Prefer modules with SMA/u.FL and short coax.
- Bus contention: avoid sharing the Cardputer LoRa/GPS cap SPI path with the C5 radios.
- Legal/safety: radio tools should default to diagnostics, receive/scan, and owner-authorized operation.

## Build Order

1. Grove UART link only: HELLO/PING/status.
2. C5 firmware reports its native Wi-Fi/802.15.4 capabilities.
3. Add CC1101 wiring and a receive-only scan test.
4. Add nRF24 wiring and a receive-only scan/noise test.
5. Add Cardputer UI app: `C5 Companion`.
6. Add bounded, explicitly authorized transmit features only after RX/status are stable.
