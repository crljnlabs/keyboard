#include "status_screen.h"

namespace {

// --- palette ---------------------------------------------------------------
constexpr uint16_t kColorBackground = 0x0861;  // near black, slightly blue
constexpr uint16_t kColorPanel = 0x18E3;       // tile and panel fill
constexpr uint16_t kColorAccent = 0x05FF;      // cyan
constexpr uint16_t kColorAccentWarm = 0xFD20;  // amber
constexpr uint16_t kColorMuted = 0x8410;

// --- layout ----------------------------------------------------------------
// All of it is derived from the display's safe area, because the panel's glass
// has rounded corners: nothing readable may sit in the outer few pixels. The
// numbers below are the heights of the three horizontal bands and the gaps
// between them; the widths follow from the safe area at runtime.
constexpr int16_t kHeaderHeight = 30;
constexpr int16_t kBandGap = 8;
// Tall enough to contain font 7, which is 48 px, without clipping it.
constexpr int16_t kEncoderHeight = 66;
constexpr int16_t kTileGap = 8;
constexpr uint8_t kTileColumns = 3;
constexpr uint8_t kTileRows = 2;

// Zeroes the counter when the encoder button is released again within this long.
constexpr uint16_t kResetPressMs = 600;

// --- corner gauge ----------------------------------------------------------
// Arcs to check the measured corner radius (43) by eye: the glass hides any arc
// smaller than its own rounding. Expected: red (35) gone, green (43) running
// along the glass edge, cyan (51) whole.
struct GaugeArc {
  int16_t radius;
  uint16_t color;
};
constexpr GaugeArc kGaugeArcs[] = {
    {35, 0xF800},  // red
    {43, 0x07E0},  // green
    {51, 0x07FF},  // cyan
};

}  // namespace

void StatusScreen::begin() {
  drawChrome();
  drawnKeyMask_ = 0xff;            // force a full tile draw
  drawnEncoderPosition_ = INT32_MIN;
  update();
}

void StatusScreen::drawChrome() {
  TFT_eSPI& tft = display_.tft();
  tft.fillScreen(kColorBackground);

  const int16_t left = display_.safeLeft();
  const int16_t right = display_.safeRight();
  const int16_t top = display_.safeTop();

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(kColorAccent, kColorBackground);
  tft.drawString("ANYDECK", left, top + kHeaderHeight / 2, 4);

  drawCornerGauge();

  // Hairline under the header, inset like everything else.
  tft.drawFastHLine(left, top + kHeaderHeight, right - left, kColorPanel);
}

void StatusScreen::drawCornerGauge() {
  TFT_eSPI& tft = display_.tft();
  const int16_t edge = display_.width() - 1;

  // Outside the safe area on purpose: the corner is what is being checked.
  for (const GaugeArc& arc : kGaugeArcs) {
    tft.drawCircleHelper(edge - arc.radius, arc.radius, arc.radius, 0x2, arc.color);  // 0x2: top right
  }

  // The radii in the header, in their arcs' colours, ending clear of the largest arc.
  constexpr int16_t kLegendGap = 6;
  constexpr int kArcs = sizeof(kGaugeArcs) / sizeof(kGaugeArcs[0]);
  int16_t x = edge - kGaugeArcs[kArcs - 1].radius - kLegendGap;
  const int16_t y = display_.safeTop() + kHeaderHeight / 2;
  tft.setTextDatum(MR_DATUM);
  for (int i = kArcs - 1; i >= 0; --i) {
    tft.setTextColor(kGaugeArcs[i].color, kColorBackground);
    char label[4];
    snprintf(label, sizeof(label), "%d", kGaugeArcs[i].radius);
    x -= tft.drawString(label, x, y, 2) + kLegendGap;
  }
}

void StatusScreen::drawEncoder() {
  TFT_eSPI& tft = display_.tft();

  const int16_t left = display_.safeLeft();
  const int16_t width = display_.safeWidth();
  const int16_t top = display_.safeTop() + kHeaderHeight + kBandGap;
  const int16_t middle = top + kEncoderHeight / 2;

  const uint16_t frame = encoderHeld_ ? kColorAccent : kColorPanel;
  tft.drawRoundRect(left, top, width, kEncoderHeight, 6, frame);

  // Clear the inside only, one pixel inside the border, so the frame survives.
  tft.fillRect(left + 2, top + 2, width - 4, kEncoderHeight - 4, kColorBackground);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(kColorMuted, kColorBackground);
  tft.drawString("ENCODER", left + 12, top + 7, 2);

  // Font 7 is the seven-segment face: digits and '-' only, which is all a
  // signed counter needs.
  char buffer[12];
  snprintf(buffer, sizeof(buffer), "%ld", static_cast<long>(encoderPosition_));
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(kColorAccent, kColorBackground);
  tft.drawString(buffer, left + width - 14, middle, 7);

  // Direction marker: a triangle pointing the way the encoder last moved.
  const int16_t markerX = left + 26;
  const int16_t markerY = middle + 12;
  if (encoderDirection_ > 0) {
    tft.fillTriangle(markerX - 8, markerY + 6, markerX + 8, markerY + 6, markerX, markerY - 8,
                     kColorAccentWarm);
  } else if (encoderDirection_ < 0) {
    tft.fillTriangle(markerX - 8, markerY - 8, markerX + 8, markerY - 8, markerX, markerY + 6,
                     kColorAccentWarm);
  }

  drawnEncoderPosition_ = encoderPosition_;
  drawnEncoderDirection_ = encoderDirection_;
  drawnEncoderHeld_ = encoderHeld_;
}

void StatusScreen::drawKeyTiles() {
  TFT_eSPI& tft = display_.tft();

  const int16_t left = display_.safeLeft();
  const int16_t width = display_.safeWidth();
  const int16_t top = display_.safeTop() + kHeaderHeight + kBandGap + kEncoderHeight + kBandGap;
  const int16_t height = display_.safeBottom() - top;

  const int16_t tileWidth = (width - (kTileColumns - 1) * kTileGap) / kTileColumns;
  const int16_t tileHeight = (height - (kTileRows - 1) * kTileGap) / kTileRows;

  for (uint8_t index = 0; index < Switches::count(); ++index) {
    const bool pressed = (keyMask_ >> index) & 1;
    if (pressed == static_cast<bool>((drawnKeyMask_ >> index) & 1)) {
      continue;
    }

    // SW1..SW3 on the top row, SW4..SW6 below, matching the board.
    const uint8_t row = index / kTileColumns;
    const uint8_t column = index % kTileColumns;
    const int16_t x = left + column * (tileWidth + kTileGap);
    const int16_t y = top + row * (tileHeight + kTileGap);

    const uint16_t fill = pressed ? kColorAccent : kColorPanel;
    tft.fillRoundRect(x, y, tileWidth, tileHeight, 6, fill);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(pressed ? kColorBackground : kColorMuted, fill);
    tft.drawNumber(index + 1, x + tileWidth / 2, y + tileHeight / 2, 4);
  }

  drawnKeyMask_ = keyMask_;
}

void StatusScreen::onEncoderRotate(int8_t direction, int32_t position) {
  encoderPosition_ = position;
  encoderDirection_ = direction;
}

void StatusScreen::onEncoderButton(bool pressed) {
  encoderHeld_ = pressed;
  if (pressed) {
    encoderPressedAtMs_ = millis();
    return;
  }
  // A short press zeroes the counter; holding it longer is just a highlight, so
  // the reset cannot happen by accident while the knob is being gripped.
  if (millis() - encoderPressedAtMs_ <= kResetPressMs) {
    encoderPosition_ = 0;
    encoderDirection_ = 0;
  }
}

void StatusScreen::onSwitch(uint8_t index, bool pressed) {
  if (index >= Switches::count()) {
    return;
  }
  if (pressed) {
    keyMask_ |= static_cast<uint8_t>(1u << index);
  } else {
    keyMask_ &= static_cast<uint8_t>(~(1u << index));
  }
}

void StatusScreen::update() {
  if (encoderPosition_ != drawnEncoderPosition_ || encoderDirection_ != drawnEncoderDirection_ ||
      encoderHeld_ != drawnEncoderHeld_) {
    drawEncoder();
  }
  if (keyMask_ != drawnKeyMask_) {
    drawKeyTiles();
  }
}
