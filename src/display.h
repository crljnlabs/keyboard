// The TFT panel.

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

// Owns the ST7789 panel, its orientation, its backlight and the usable drawing
// area.
//
// Deliberately thin: it brings the panel up and hands out the TFT_eSPI instance
// for drawing. What is shown lives in the screen that draws it, so a new screen
// never has to touch this class.
//
// The SPI pins, the driver and the panel geometry are compile-time settings of
// TFT_eSPI and live in platformio.ini. Orientation, backlight and safe area are
// runtime concerns and belong here.
class Display {
 public:
  void begin();

  // 0 = off, 255 = full brightness.
  void setBrightness(uint8_t brightness);
  uint8_t brightness() const { return brightness_; }

  // Ramps the backlight to `target` over `durationMs`, blocking. Used for the
  // fade-in at boot, which doubles as a visible "firmware is alive" signal.
  void fadeBrightness(uint8_t target, uint16_t durationMs);

  TFT_eSPI& tft() { return tft_; }

  int16_t width() { return tft_.width(); }
  int16_t height() { return tft_.height(); }

  // --- geometry --------------------------------------------------------------
  // Landscape, pin header on the right hand side of the image: the module plugs
  // into a vertical header at the right edge of the board and its panel extends
  // leftwards, so the 280 pixel axis runs horizontally. Change this single value
  // if the module is ever mounted the other way round - 1 is the same landscape
  // mirrored, 0 and 2 are the two portrait orientations.
  static constexpr uint8_t kRotation = 3;

  // The panel as the firmware addresses it, i.e. after kRotation was applied.
  // TFT_WIDTH and TFT_HEIGHT describe the panel unrotated, so the odd rotations
  // swap them. Kept here rather than written out twice, because the USB
  // capability block reports the same numbers to the PC.
  static constexpr int16_t kWidth = (kRotation % 2 == 1) ? TFT_HEIGHT : TFT_WIDTH;
  static constexpr int16_t kHeight = (kRotation % 2 == 1) ? TFT_WIDTH : TFT_HEIGHT;

  // --- corners and safe area -------------------------------------------------
  // The glass hides the corners: their rounding has a radius of 43 px,
  // measured from a photo of the panel (all four corners 42-44 px).
  static constexpr uint8_t kCornerRadius = 43;

  // Everything readable stays this far from the edges. Enough for the corners:
  // the inset corner point (14, 14) is still 2 px inside the rounding.
  static constexpr int16_t kSafeInset = 14;

  int16_t safeLeft() const { return kSafeInset; }
  int16_t safeTop() const { return kSafeInset; }
  int16_t safeWidth() { return width() - 2 * kSafeInset; }
  int16_t safeHeight() { return height() - 2 * kSafeInset; }
  int16_t safeRight() { return width() - kSafeInset; }
  int16_t safeBottom() { return height() - kSafeInset; }

 private:
  TFT_eSPI tft_;
  uint8_t brightness_ = 0;
};
