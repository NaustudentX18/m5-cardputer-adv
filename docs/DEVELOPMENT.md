# Development

## Prerequisites

Firmware:

- PlatformIO
- ESP32 platform support
- M5Stack Cardputer libraries
- M5Stack Cardputer-Adv hardware for final smoke tests

Bridge:

- Python 3.11+ or another selected service runtime
- no provider credentials required for dry-run tests

## Firmware Start

Target folder:

```text
projects/advdeck-agent/
```

Planned commands once `platformio.ini` exists:

```bash
cd projects/advdeck-agent
pio run
```

## Bridge Start

Target folder:

```text
bridge/advdeck-agent-bridge/
```

The bridge should start with:

- dry-run planner
- fixture input/output
- schema validation
- no credential requirement for tests

## Required Checks

Before claiming implementation complete:

- firmware builds, or the exact blocker is documented
- bridge tests pass, if bridge code changed
- schema fixtures validate, if file contracts changed
- docs are updated when behavior changes

## Hardware Smoke Checklist

Record results in the PR or task notes:

- boots to home route
- keyboard events work
- display uses full 240 x 135 px
- SD present path works
- SD absent path degrades cleanly
- battery/status does not crash
- no Wi-Fi/BLE/radio starts at boot
