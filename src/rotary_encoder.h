// The rotary encoder (KY-040 style module: CLK, DT, SW).

#pragma once

#include <Arduino.h>
#include <functional>

#include "debounced_input.h"
#include "pins.h"

// Decodes the encoder's quadrature signal in an interrupt handler and reports
// completed detents plus the push button state from the main loop.
//
// The quadrature signal is decoded with a state machine instead of a debounce
// timer: the machine only leaves a detent state through a valid transition
// sequence, so contact bounce is rejected by construction and no rotation is
// lost even when the main loop is busy drawing to the display.
class RotaryEncoder {
 public:
  // direction is +1 for clockwise and -1 for counter-clockwise; position is
  // the accumulated detent count since begin().
  using RotationCallback = std::function<void(int8_t direction, int32_t position)>;
  using ButtonCallback = std::function<void(bool pressed)>;

  void begin();

  // Call from the main loop; invokes the callbacks for everything that has
  // happened since the previous call.
  void update();

  void onRotate(RotationCallback callback) { rotationCallback_ = std::move(callback); }
  void onButton(ButtonCallback callback) { buttonCallback_ = std::move(callback); }

  int32_t position() const { return position_; }
  bool isButtonPressed() const { return button_.isPressed(); }

  // Whether the push button reports its first edge at once - see
  // DebouncedInput. The turning has no such choice: the state machine needs no
  // waiting.
  void setButtonFast(bool fast) { button_.setFast(fast); }

 private:
  // Interrupt entry point. Registered for both quadrature pins, so every edge
  // on either of them advances the state machine.
  static void IRAM_ATTR onQuadratureEdge(void* arg);

  // Reads both quadrature pins into the index the transition table expects.
  static uint8_t IRAM_ATTR sampleLevels();

  // Advances the state machine from the current pin levels and, on a completed
  // detent, adds to pendingSteps_. Runs in interrupt context.
  void IRAM_ATTR decode();

  DebouncedInput button_;

  RotationCallback rotationCallback_;
  ButtonCallback buttonCallback_;

  // Index into the transition table; only touched inside decode().
  volatile uint8_t state_ = 0;
  // Detents decoded but not yet reported, guarded by lock_.
  volatile int32_t pendingSteps_ = 0;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;

  int32_t position_ = 0;
};
