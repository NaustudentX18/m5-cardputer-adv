// boards/pinouts/m5stack-cardputer.h
//
// Minimal variant for the Cardputer-Adv. The M5Cardputer library
// supplies its own keyboard mapping and most of the GPIO defines we
// need; this header is just enough to satisfy the Arduino core's
// pins_arduino.h include. We intentionally do NOT pull in the
// Launcher's full pinouts/m5stack-cardputer.h because that file
// redeclares _kb_asciimap, which now lives in M5Cardputer's
// Keyboard_def.h.

#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// I2C (TCA8418 keyboard expander on the Cardputer-Adv).
static const uint8_t SDA = 8;
static const uint8_t SCL = 9;
static const uint8_t TCA8418_INT_PIN = 11;
static const uint8_t TCA8418_I2C_ADDR = 0x34;

// SPI bus shared by the SD card and the ST7789 panel. Pin numbers
// match the Launcher's per-board ini and M5Stack's reference design.
static const uint8_t SS    = 12;
static const uint8_t MOSI  = 14;
static const uint8_t MISO  = 39;
static const uint8_t SCK   = 40;

// ST7789 panel control pins.
static const uint8_t TFT_BL   = 38;
static const uint8_t TFT_RST  = 33;
static const uint8_t TFT_DC   = 34;
static const uint8_t TFT_CS   = 37;
static const uint8_t TFT_WIDTH  = 135;
static const uint8_t TFT_HEIGHT = 240;

// Misc board pins referenced by the Arduino core / M5Unified.
static const uint8_t LED       = 21;
static const uint8_t BACKLIGHT = 38;

// No battery ADC, no IR, no buzzer pin macros needed for Phase 1.

#endif  // Pins_Arduino_h
