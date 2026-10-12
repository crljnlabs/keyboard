// What the PC sends to the display: report 0x20.
//
// The PC draws, the device shows. Every packet is one report 0x20 of 63 bytes,
// and its first byte says what it is:
//
//   0x01 rectangle   display (1), x, y, width, height (2 each, little endian),
//                    encoding (1: 0 raw RGB565, 1 PackBits), data length (4)
//   0x02 data        number within the rectangle (2), then up to 60 bytes
//   0x03 show        everything since the last show appears at once
//   0x04 brightness  display (1), 0..255
//   0x05 own screen  the device goes back to drawing its own status screen
//
// Pixels are RGB565, little endian. PackBits works on whole pixels: a header
// byte n of 0..127 is followed by n + 1 literal pixels, 128..255 by one pixel
// that repeats n - 126 times. A data packet whose number is not the next one
// drops the rectangle; whatever arrived of it stays in the buffer but is not
// shown until a later rectangle covers it.
//
// Packets arrive on the USB task. They are only decoded into a frame buffer in
// PSRAM there - drawing over SPI would hold up USB - and the main loop pushes
// what changed to the panel when a "show" comes in.

#pragma once

#include <Arduino.h>

#include "display.h"

class DisplayFeed {
 public:
  explicit DisplayFeed(Display& display) : display_(display) {}

  // Allocates the frame buffer. False if there is no memory for it; the feed
  // then ignores everything and the status screen stays.
  bool begin();

  // From the USB task: one report 0x20 without its id.
  void receive(const uint8_t* packet, uint16_t length);

  // From the main loop: pushes a shown frame to the panel, applies the
  // brightness. True while the PC's picture is on the panel - the status screen
  // must not draw then.
  bool update();

  // From the main loop: the PC asked for the device's own screen back, once.
  bool takeOwnScreenRequest();

 private:
  enum class Encoding : uint8_t { kRaw = 0, kPackBits = 1 };

  void beginRectangle(const uint8_t* payload, uint16_t length);
  void data(const uint8_t* payload, uint16_t length);
  void decode(uint8_t byte);
  void putPixel(uint16_t pixel);
  void show();

  Display& display_;
  uint16_t* frame_ = nullptr;

  // --- the rectangle being received (USB task only) ------------------------
  bool receiving_ = false;
  uint16_t rectX_ = 0, rectY_ = 0, rectW_ = 0, rectH_ = 0;
  Encoding encoding_ = Encoding::kRaw;
  uint32_t bytesLeft_ = 0;
  uint32_t pixelsWritten_ = 0;
  uint16_t nextNumber_ = 0;

  // Decoder state, so a pixel or a PackBits run may span two packets.
  uint8_t lowByte_ = 0;
  bool haveLowByte_ = false;
  uint8_t literalsLeft_ = 0;  // pixels still to come as literals
  uint8_t repeatCount_ = 0;   // > 0: the next pixel repeats this often
  bool needHeader_ = true;

  // --- what changed since the last show (USB task writes, loop reads) ------
  int16_t dirtyLeft_ = INT16_MAX, dirtyTop_ = INT16_MAX;
  int16_t dirtyRight_ = -1, dirtyBottom_ = -1;  // exclusive
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;

  // Handed to the main loop under lock_.
  bool showPending_ = false;
  int16_t showLeft_ = 0, showTop_ = 0, showRight_ = 0, showBottom_ = 0;
  int16_t brightnessPending_ = -1;
  bool ownScreenPending_ = false;

  bool active_ = false;  // the PC's picture is on the panel (loop only)
};
