// Firmware entry point.
//
// Reads the six key switches and the rotary encoder, and mirrors their state on
// the TFT status screen. The USB interface that reports these events to the
// anydeck application on the PC is not implemented yet - this firmware is the
// input and display layer it will sit on top of.

#include <Arduino.h>

#include "display.h"
#include "rotary_encoder.h"
#include "status_screen.h"
#include "switches.h"

namespace {

Display display;
StatusScreen screen(display);
RotaryEncoder encoder;
Switches switches;

constexpr uint8_t kBacklightBrightness = 220;
constexpr uint16_t kBacklightFadeMs = 400;

}  // namespace

void setup() {
  Serial.begin(115200);

  display.begin();
  screen.begin();
  display.fadeBrightness(kBacklightBrightness, kBacklightFadeMs);

  encoder.onRotate([](int8_t direction, int32_t position) {
    screen.onEncoderRotate(direction, position);
  });
  encoder.onButton([](bool pressed) { screen.onEncoderButton(pressed); });
  encoder.begin();

  switches.onEvent([](uint8_t index, bool pressed) { screen.onSwitch(index, pressed); });
  switches.begin();

  Serial.printf("keypad ready: %u switches, 1 encoder, %dx%d display\n", Switches::count(),
                display.width(), display.height());
}

void loop() {
  encoder.update();
  switches.update();
  screen.update();
}
