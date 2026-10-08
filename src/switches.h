// The six key switches.

#pragma once

#include <Arduino.h>
#include <functional>

#include "debounced_input.h"
#include "pins.h"

// Polls all switches from pins::kSwitches and reports debounced press/release
// events through a callback. Indices are zero based, i.e. index 0 is the switch
// labelled SW1 in the schematic.
class Switches {
 public:
  // (index, pressed) - pressed is true for a press, false for a release.
  using Callback = std::function<void(uint8_t index, bool pressed)>;

  void begin();

  // Call from the main loop; invokes the callback once per detected edge.
  void update();

  void onEvent(Callback callback) { callback_ = std::move(callback); }

  // Debounced state of a single switch.
  bool isPressed(uint8_t index) const;

  // Whether a switch reports its first edge at once - see DebouncedInput.
  void setFast(uint8_t index, bool fast);

  // Number of switches currently held down.
  uint8_t pressedCount() const;

  static constexpr uint8_t count() { return pins::kSwitchCount; }

 private:
  DebouncedInput inputs_[pins::kSwitchCount];
  Callback callback_;
};
