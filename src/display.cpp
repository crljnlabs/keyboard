#include "display.h"

#include "pins.h"

namespace {

// LEDC settings for the backlight. Channel 0 is free; 5 kHz is well above the
// audible range and far below what would make the panel's LED driver unhappy.
constexpr uint8_t kBacklightChannel = 0;
constexpr uint32_t kBacklightFrequency = 5000;
constexpr uint8_t kBacklightResolutionBits = 8;

// Brightness the backlight is parked at while the panel is being brought up.
// It turns the backlight into a three-state progress indicator:
//
//   dark         -> the firmware is not running at all
//   dim glow     -> begin() was reached but the panel setup did not finish
//   full + image -> everything worked
//
// Low enough that the panel's uninitialised content is not distracting, high
// enough to be unmistakably lit in a normally lit room.
constexpr uint8_t kAliveBrightness = 40;

}  // namespace

void Display::begin() {
  // Backlight before the panel, and before anything that could block: see
  // kAliveBrightness for why this is worth a brief glimpse of the panel's
  // uninitialised content.
  ledcSetup(kBacklightChannel, kBacklightFrequency, kBacklightResolutionBits);
  ledcAttachPin(pins::kTftBacklight, kBacklightChannel);
  setBrightness(kAliveBrightness);

  tft_.init();
  tft_.setRotation(Display::kRotation);
  tft_.fillScreen(TFT_BLACK);
}

void Display::setBrightness(uint8_t brightness) {
  brightness_ = brightness;
  ledcWrite(kBacklightChannel, brightness);
}

void Display::fadeBrightness(uint8_t target, uint16_t durationMs) {
  const int16_t start = brightness_;
  const int16_t span = static_cast<int16_t>(target) - start;
  if (span == 0 || durationMs == 0) {
    setBrightness(target);
    return;
  }

  constexpr uint8_t kSteps = 32;
  for (uint8_t step = 1; step <= kSteps; ++step) {
    setBrightness(static_cast<uint8_t>(start + (span * step) / kSteps));
    delay(durationMs / kSteps);
  }
  setBrightness(target);
}
