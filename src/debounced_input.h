// Debouncing for a single active-low GPIO.
//
// Shared by the key switches and the rotary encoder's push button, both of
// which short their pin to GND through a mechanical contact.

#pragma once

#include <Arduino.h>

// Rejects a level change until it has been stable for `debounceMs`. Because the
// pin is only ever sampled - never blocked on - update() is cheap enough to be
// called from the main loop on every pass.
class DebouncedInput {
 public:
  // How a level change is reported by update().
  enum class Event : uint8_t {
    kNone,     // level unchanged (or still bouncing)
    kPressed,  // contact closed: pin pulled low
    kReleased  // contact opened: pin back high
  };

  DebouncedInput() = default;

  // Configures the pin as an input with the internal pull-up and takes the
  // current level as the starting state, so a switch that is already held at
  // boot does not produce a spurious press.
  void begin(uint8_t pin, uint16_t debounceMs = 15) {
    pin_ = pin;
    debounceMs_ = debounceMs;
    pinMode(pin_, INPUT_PULLUP);
    stableLevel_ = digitalRead(pin_);
    lastLevel_ = stableLevel_;
    lastChangeMs_ = millis();
  }

  // Samples the pin once and reports a debounced edge, if any.
  Event update() {
    const int level = digitalRead(pin_);
    const uint32_t now = millis();

    if (level != lastLevel_) {
      lastLevel_ = level;
      lastChangeMs_ = now;
      return Event::kNone;
    }
    if (level == stableLevel_ || now - lastChangeMs_ < debounceMs_) {
      return Event::kNone;
    }

    stableLevel_ = level;
    // Active low: LOW is the pressed state.
    return level == LOW ? Event::kPressed : Event::kReleased;
  }

  // Debounced state, i.e. the level update() last accepted.
  bool isPressed() const { return stableLevel_ == LOW; }

 private:
  uint8_t pin_ = 0;
  uint16_t debounceMs_ = 15;
  int stableLevel_ = HIGH;
  int lastLevel_ = HIGH;
  uint32_t lastChangeMs_ = 0;
};
