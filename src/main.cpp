// The crljn keypad: six keys, a rotary encoder with a push button and a 1.69"
// display, as an anydeck device. Everything between the keypad and the PC is
// the anydeck library's (../anydeck-library); this file only says what the
// keypad is made of. The display's driver and SPI pins are TFT_eSPI build
// flags in platformio.ini.

#include <Arduino.h>
#include <Anydeck.h>

#include "pins.h"

namespace {

// SW1..SW3 on the top row, SW4..SW6 below, as they sit on the board. Their
// order here is the order anydeck numbers them in.
anydeck::Key sw1(pins::kSwitches[0]);
anydeck::Key sw2(pins::kSwitches[1]);
anydeck::Key sw3(pins::kSwitches[2]);
anydeck::Key sw4(pins::kSwitches[3]);
anydeck::Key sw5(pins::kSwitches[4]);
anydeck::Key sw6(pins::kSwitches[5]);

// CLK and DT reach the processor the other way round on this board, so the
// encoder's A is DT and its B is CLK: that way clockwise counts up.
anydeck::RotaryEncoder knob(pins::kEncoderB, pins::kEncoderA, pins::kEncoderButton);

// Landscape, pin header on the right of the image: the module plugs into a
// vertical header at the board's right edge and its panel extends leftwards,
// so the 280 pixel axis runs horizontally. 1 is the same landscape mirrored,
// 0 and 2 are portrait.
//
// The glass hides the corners: their rounding has a radius of 43 px,
// measured from a photo of the panel (all four corners 42-44 px). The visible
// area is 32.6 by 27.9 mm in this orientation.
anydeck::Display screen({
    .rotation = 3,
    .backlightPin = pins::kTftBacklight,
    .cornerRadius = 43,
    .physicalWidthTenthMm = 326,
    .physicalHeightTenthMm = 279,
    .brightness = 220,
    .fadeInMs = 400,
    // Only while the keypad shows its own status screen: once anydeck draws,
    // anydeck decides when the display sleeps.
    .sleepAfterMs = 5 * 60 * 1000,
});

}  // namespace

// The product is the name the keypad gives itself over USB, and so its name in
// anydeck until somebody renames it. The manufacturer is who made the keypad,
// not its chip: crljn.labs, as the LICENSE has it. The ids stay Espressif's,
// the serial number the chip's MAC address. While the Mac sleeps, a key or a
// turn wakes it, if the Mac allows that.
Anydeck keypad({.manufacturer = "crljn.labs", .product = "crljn-board", .canWakeComputer = true},  //
               sw1, sw2, sw3, sw4, sw5, sw6, knob, screen);

void setup() { keypad.begin(); }

void loop() { keypad.loop(); }
