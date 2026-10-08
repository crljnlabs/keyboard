// Firmware entry point.
//
// Reads the six key switches and the rotary encoder, mirrors their state on the
// TFT status screen, and reports them to the PC over a vendor-defined USB HID
// interface. The PC can set single buttons to report at once; the keypad keeps
// that in flash. Receiving display content from the PC is not implemented yet.

#include <Arduino.h>
#include <Preferences.h>

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

// Which buttons report at once, kept in flash: the setting outlives a power cut
// and needs no PC to come back.
Preferences settings;
constexpr const char* kSettingsNamespace = "keypad";
constexpr const char* kFastButtonsKey = "fast-buttons";

// Bit n is button n + 1 of the report descriptor: SW1..SW6, then the encoder's
// click.
void applyFastButtons(uint8_t mask) {
  for (uint8_t i = 0; i < Switches::count(); ++i) {
    switches.setFast(i, mask & (1u << i));
  }
  encoder.setButtonFast(mask & (1u << Switches::count()));
}

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

  settings.begin(kSettingsNamespace, false);
  const uint8_t fast = settings.getUChar(kFastButtonsKey, 0);
  applyFastButtons(fast);
  keypadHid.setFastButtons(fast);

  Serial.printf("keypad ready: %u switches, 1 encoder, %dx%d display\n", Switches::count(),
                display.width(), display.height());
}

void loop() {
  encoder.update();
  switches.update();
  screen.update();
  keypadHid.update();

  uint8_t fast = 0;
  if (keypadHid.takeFastButtons(fast)) {
    applyFastButtons(fast);
    settings.putUChar(kFastButtonsKey, fast);
  }
}
