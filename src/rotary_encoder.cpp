#include "rotary_encoder.h"

namespace {

// Half-step quadrature transition table (Ben Buxton, "Rotary encoder, state
// machine implementation", the half-step variant). Row = current state, column
// = the new pin levels as (B << 1) | A. A cell holds the follow-up state, with
// kDirCw / kDirCcw set in the upper bits when a detent was completed.
//
// Half-step because the encoder on this board clicks twice per quadrature
// cycle: it rests with both lines high, and again with both lines low. A
// full-step table only completes at one of the two, so it counted every second
// click. This one completes at both - one step per click.
//
// A step is still only emitted after the lines passed through the level in
// between, so a contact that rattles between two levels keeps falling back to
// the rest it came from.
constexpr uint8_t kRestHigh = 0x0;     // both lines high
constexpr uint8_t kCcwBegin = 0x1;     // out of the high rest, counter-clockwise
constexpr uint8_t kCwBegin = 0x2;      // out of the high rest, clockwise
constexpr uint8_t kRestLow = 0x3;      // both lines low
constexpr uint8_t kCwBeginLow = 0x4;   // out of the low rest, clockwise
constexpr uint8_t kCcwBeginLow = 0x5;  // out of the low rest, counter-clockwise

constexpr uint8_t kDirCw = 0x10;
constexpr uint8_t kDirCcw = 0x20;
constexpr uint8_t kStateMask = 0x0f;
constexpr uint8_t kDirMask = 0x30;

const uint8_t kTransitions[6][4] = {
    // kRestHigh
    {kRestLow, kCwBegin, kCcwBegin, kRestHigh},
    // kCcwBegin
    {static_cast<uint8_t>(kRestLow | kDirCcw), kRestHigh, kCcwBegin, kRestHigh},
    // kCwBegin
    {static_cast<uint8_t>(kRestLow | kDirCw), kCwBegin, kRestHigh, kRestHigh},
    // kRestLow
    {kRestLow, kCcwBeginLow, kCwBeginLow, kRestHigh},
    // kCwBeginLow
    {kRestLow, kRestLow, kCwBeginLow, static_cast<uint8_t>(kRestHigh | kDirCw)},
    // kCcwBeginLow
    {kRestLow, kCcwBeginLow, kRestLow, static_cast<uint8_t>(kRestHigh | kDirCcw)},
};

}  // namespace

void RotaryEncoder::begin() {
  // The module carries its own pull-ups, but enabling the internal ones keeps
  // the lines defined even when a bare encoder without the breakout is wired
  // up.
  pinMode(pins::kEncoderA, INPUT_PULLUP);
  pinMode(pins::kEncoderB, INPUT_PULLUP);
  button_.begin(pins::kEncoderButton);

  // Start at the rest the knob is actually in. Either is a detent, and the
  // table only leaves a rest through the level in between, so starting at the
  // other one would cost the first click.
  state_ = sampleLevels() == 0 ? kRestLow : kRestHigh;

  attachInterruptArg(digitalPinToInterrupt(pins::kEncoderA), onQuadratureEdge, this, CHANGE);
  attachInterruptArg(digitalPinToInterrupt(pins::kEncoderB), onQuadratureEdge, this, CHANGE);
}

void IRAM_ATTR RotaryEncoder::onQuadratureEdge(void* arg) {
  static_cast<RotaryEncoder*>(arg)->decode();
}

uint8_t IRAM_ATTR RotaryEncoder::sampleLevels() {
  // The transition table is written for (B << 1) | A. On this board CLK and DT
  // reach the MCU the other way round, which made a clockwise turn count down.
  // Sampling the two swapped fixes the direction at its source, so both the
  // table and the reported sign stay canonical - negating the result afterwards
  // would work too, but then every reader has to know about the correction.
  return (digitalRead(pins::kEncoderA) << 1) | digitalRead(pins::kEncoderB);
}

void IRAM_ATTR RotaryEncoder::decode() {
  const uint8_t levels = sampleLevels();
  const uint8_t next = kTransitions[state_ & kStateMask][levels];
  state_ = next & kStateMask;

  const uint8_t direction = next & kDirMask;
  if (direction == 0) {
    return;
  }

  portENTER_CRITICAL_ISR(&lock_);
  pendingSteps_ += (direction == kDirCw) ? 1 : -1;
  portEXIT_CRITICAL_ISR(&lock_);
}

void RotaryEncoder::update() {
  // Take the whole batch at once so no step decoded while reporting is lost.
  portENTER_CRITICAL(&lock_);
  const int32_t steps = pendingSteps_;
  pendingSteps_ = 0;
  portEXIT_CRITICAL(&lock_);

  // Report every detent separately, so a consumer that maps detents to key
  // repeats sees the same count regardless of how long the loop was busy.
  if (steps != 0) {
    const int8_t direction = steps > 0 ? 1 : -1;
    const int32_t count = steps > 0 ? steps : -steps;
    for (int32_t i = 0; i < count; ++i) {
      position_ += direction;
      if (rotationCallback_) {
        rotationCallback_(direction, position_);
      }
    }
  }

  const DebouncedInput::Event event = button_.update();
  if (event != DebouncedInput::Event::kNone && buttonCallback_) {
    buttonCallback_(event == DebouncedInput::Event::kPressed);
  }
}
