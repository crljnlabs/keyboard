// Debouncing for a single active-low GPIO.
//
// Shared by the key switches and the rotary encoder's push button, both of
// which short their pin to GND through a mechanical contact.

#pragma once

#include <Arduino.h>

// Turns a bouncing contact into one press and one release, in one of two ways.
// Normally a level change counts once it has been stable for `debounceMs`: a
// glitch shorter than that is never seen, and every press arrives that much
// later. Fast, the first edge counts at once and the pin is not looked at for
// `debounceMs` after it: the bounce goes unseen just the same, the press comes
// sooner, and a glitch counts as a press. Because the pin is only ever sampled -
// never blocked on - update() is cheap enough to be called from the main loop on
// every pass.
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

  // Fast or normal, see above. Takes effect with the next update().
  void setFast(bool fast) { fast_ = fast; }

  // Samples the pin once and reports a debounced edge, if any.
  Event update() {
    const int level = digitalRead(pin_);
    const uint32_t now = millis();

    if (fast_) {
      // Within `debounceMs` of the last edge, whatever the contact does is its
      // bounce.
      if (level == stableLevel_ || now - lastChangeMs_ < debounceMs_) {
        return Event::kNone;
      }
      return accept(level, now);
    }

    if (level != lastLevel_) {
      lastLevel_ = level;
      lastChangeMs_ = now;
      return Event::kNone;
    }
    if (level == stableLevel_ || now - lastChangeMs_ < debounceMs_) {
      return Event::kNone;
    }
    return accept(level, now);
  }

  // Debounced state, i.e. the level update() last accepted.
  bool isPressed() const { return stableLevel_ == LOW; }

 private:
  // Takes `level` as the new state. The time is the edge a fast input waits
  // out; a normal one restarts its wait with the next change anyway.
  Event accept(int level, uint32_t now) {
    stableLevel_ = level;
    lastLevel_ = level;
    lastChangeMs_ = now;
    // Active low: LOW is the pressed state.
    return level == LOW ? Event::kPressed : Event::kReleased;
  }

  uint8_t pin_ = 0;
  uint16_t debounceMs_ = 15;
  int stableLevel_ = HIGH;
  int lastLevel_ = HIGH;
  uint32_t lastChangeMs_ = 0;
  bool fast_ = false;
};
