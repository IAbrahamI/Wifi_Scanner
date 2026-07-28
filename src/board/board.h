#pragma once

#include <Arduino.h>
#include "display.h"
#include "pins.h"

namespace board {

// Powers the peripheral rail, brings up the LCD + touch, and starts the escape
// hatch watchdog. Every app must call this first -- see the "Hardware
// Initialization Rule" in the project spec.
//
// `rotation` is passed straight to LovyanGFX: 0/2 are portrait 170x320,
// 1/3 are landscape 320x170.
void begin(uint8_t rotation = 1);

// Battery voltage in millivolts, read through the 1:2 divider on GPIO 4.
// Reads ~4200 on a full LiPo. On USB-OTG power with no cell fitted this
// floats and the value is meaningless -- treat < 3000 as "no battery".
uint32_t batteryMilliVolts();

}  // namespace board
