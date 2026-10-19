// Firmware entry point.
//
// Reads the six key switches and the rotary encoder, mirrors their state on the
// TFT status screen, and reports them to the PC over a vendor-defined USB HID
// interface. The PC can set single buttons to report at once; the keypad keeps
// that in flash. What the PC sends for the display (report 0x20) replaces the
// status screen until the PC hands it back.

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <Preferences.h>

#include "display.h"
#include "display_feed.h"
#include "keypad_hid.h"
#include "rotary_encoder.h"
#include "status_screen.h"
#include "switches.h"

namespace {

Display display;
StatusScreen screen(display);
DisplayFeed feed(display);
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

// Why the chip started last, for the serial report: a crash or a watchdog
// shows here, a plain power-on too.
esp_reset_reason_t startedBecause = ESP_RST_UNKNOWN;

const char* reasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "restart by software";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "other watchdog";
    case ESP_RST_BROWNOUT: return "brownout (too little power)";
    case ESP_RST_DEEPSLEEP: return "wake from deep sleep";
    default: return "unknown";
  }
}

// Once a serial monitor is open, and every 10 s while it is: why the chip
// started, how much memory is free, what the display feed refused. Nothing
// goes out while no one listens - CDC needs the host to open the port.
void reportOverSerial() {
  static bool listening = false;
  static uint32_t lastMs = 0;
  const bool now = static_cast<bool>(Serial);
  if (!now) {
    listening = false;
    return;
  }
  if (listening && millis() - lastMs < 10000) {
    return;
  }
  listening = true;
  lastMs = millis();
  Serial.printf("started because: %s (%d), up %lu s\n", reasonName(startedBecause),
                static_cast<int>(startedBecause), static_cast<unsigned long>(millis() / 1000));
  Serial.printf("free: internal %u B (largest %u), PSRAM %u B (largest %u)\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  Serial.printf("display feed: %lu refused for memory, %lu dropped\n",
                static_cast<unsigned long>(feed.refusedForMemory()), static_cast<unsigned long>(feed.dropped()));
}

}  // namespace

void setup() {
  startedBecause = esp_reset_reason();
  Serial.begin(115200);

  display.begin();
  screen.begin();
  display.fadeBrightness(kBacklightBrightness, kBacklightFadeMs);

  if (!feed.begin()) {
    Serial.println("no memory for the display feed: the PC cannot draw");
  }
  keypadHid.attachDisplayFeed(&feed);
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
  if (feed.takeOwnScreenRequest()) {
    screen.begin();
  }
  if (!feed.update()) {
    screen.update();
  }
  keypadHid.update();
  reportOverSerial();

  uint8_t fast = 0;
  if (keypadHid.takeFastButtons(fast)) {
    applyFastButtons(fast);
    settings.putUChar(kFastButtonsKey, fast);
  }
}
