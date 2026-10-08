#include "switches.h"

void Switches::begin() {
  for (uint8_t i = 0; i < pins::kSwitchCount; ++i) {
    inputs_[i].begin(pins::kSwitches[i]);
  }
}

void Switches::update() {
  for (uint8_t i = 0; i < pins::kSwitchCount; ++i) {
    const DebouncedInput::Event event = inputs_[i].update();
    if (event == DebouncedInput::Event::kNone || !callback_) {
      continue;
    }
    callback_(i, event == DebouncedInput::Event::kPressed);
  }
}

bool Switches::isPressed(uint8_t index) const {
  if (index >= pins::kSwitchCount) {
    return false;
  }
  return inputs_[index].isPressed();
}

void Switches::setFast(uint8_t index, bool fast) {
  if (index < pins::kSwitchCount) {
    inputs_[index].setFast(fast);
  }
}

uint8_t Switches::pressedCount() const {
  uint8_t pressed = 0;
  for (uint8_t i = 0; i < pins::kSwitchCount; ++i) {
    if (inputs_[i].isPressed()) {
      ++pressed;
    }
  }
  return pressed;
}
