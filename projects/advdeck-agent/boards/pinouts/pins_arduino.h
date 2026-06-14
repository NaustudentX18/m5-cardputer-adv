// boards/pinouts/pins_arduino.h
//
// The Arduino core expects this file at <variant>/pins_arduino.h. Our
// variants_dir is `boards` and the variant name is `pinouts`, so the
// core looks for `boards/pinouts/pins_arduino.h`.
//
// We only support one board (m5stack-cardputer), so this header is
// a one-line include of the per-board pinout. If we ever add another
// board, copy the Launcher's #if/#elif multiplexer pattern.
//
// The launcher / Bruce firmware keeps a single multiplexer
// pins_arduino.h in this directory and includes the right header per
// -DARDUINO_<BOARD> flag. We keep ours simple because we control the
// platformio.ini and know exactly one board is being built.

#ifndef ADVDECK_BOARDS_PINOUTS_PINS_ARDUINO_H_
#define ADVDECK_BOARDS_PINOUTS_PINS_ARDUINO_H_

#include "m5stack-cardputer.h"

#endif  // ADVDECK_BOARDS_PINOUTS_PINS_ARDUINO_H_
