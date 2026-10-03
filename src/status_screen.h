// The device's status screen.
//
// Shows what the keypad is currently doing: the encoder's position and last
// direction, and which keys are held. The key tiles are arranged in two rows of
// three, the same way the switches sit on the board, so a tile lighting up maps
// straight onto the key under the user's finger.

#pragma once

#include <Arduino.h>

#include "display.h"
#include "switches.h"

class StatusScreen {
 public:
  explicit StatusScreen(Display& display) : display_(display) {}

  // Draws the parts that never change. Call after Display::begin().
  void begin();

  // Redraws whatever changed. Cheap to call on every pass of the main loop; it
  // only touches the panel when there is something new to show.
  void update();

  // --- input events, fed in from the main loop ------------------------------
  void onEncoderRotate(int8_t direction, int32_t position);
  void onEncoderButton(bool pressed);
  void onSwitch(uint8_t index, bool pressed);

 private:
  void drawChrome();
  // Arcs in the top right corner to read the glass's corner radius off.
  void drawCornerGauge();
  void drawEncoder();
  void drawKeyTiles();

  Display& display_;

  int32_t encoderPosition_ = 0;
  int32_t drawnEncoderPosition_ = INT32_MIN;
  int8_t encoderDirection_ = 0;
  int8_t drawnEncoderDirection_ = 0;

  // The encoder button is a modifier here, not an event to display on its own:
  // holding it highlights the encoder panel, releasing it after a short press
  // zeroes the counter.
  bool encoderHeld_ = false;
  bool drawnEncoderHeld_ = false;
  uint32_t encoderPressedAtMs_ = 0;

  // One bit per switch, bit 0 = SW1.
  uint8_t keyMask_ = 0;
  uint8_t drawnKeyMask_ = 0xff;
};
