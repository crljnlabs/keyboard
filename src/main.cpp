// Firmware entry point.
//
// Reads the six key switches and the rotary encoder, mirrors their state on the
// TFT status screen, and reports them to the PC over a vendor-defined USB HID
// interface. Receiving display content from the PC is not implemented yet.

#include <Arduino.h>

#include "display.h"
#include "keypad_hid.h"
#include "rotary_encoder.h"
#include "status_screen.h"
#include "switches.h"

namespace {

Display display;
StatusScreen screen(display);
RotaryEncoder encoder;
Switches switches;

// Global on purpose, not a local of setup(): its constructor registers the HID
// interface, and the Arduino core starts USB in app_main - before setup() runs.
// A global constructor is the last moment at which the report descriptor can
// still be added.
KeypadHid keypadHid;

constexpr uint8_t kBacklightBrightness = 220;
constexpr uint16_t kBacklightFadeMs = 400;

}  // namespace

void setup() {
  Serial.begin(115200);

  display.begin();
  screen.begin();
  display.fadeBrightness(kBacklightBrightness, kBacklightFadeMs);

  keypadHid.begin();

  encoder.onRotate([](int8_t direction, int32_t position) {
    screen.onEncoderRotate(direction, position);
    keypadHid.addRotation(direction);
  });
  encoder.onButton([](bool pressed) {
    screen.onEncoderButton(pressed);
    keypadHid.setEncoderButton(pressed);
  });
  encoder.begin();

  switches.onEvent([](uint8_t index, bool pressed) {
    screen.onSwitch(index, pressed);
    keypadHid.setKey(index, pressed);
  });
  switches.begin();

  Serial.printf("keypad ready: %u switches, 1 encoder, %dx%d display\n", Switches::count(),
                display.width(), display.height());
}

void loop() {
  encoder.update();
  switches.update();
  screen.update();
  keypadHid.update();
}
