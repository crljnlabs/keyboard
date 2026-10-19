// What the PC sends to the display: report 0x20.
//
// The PC draws, the device puts together. The PC sends pieces - pictures with
// transparency, already drawn - and where each goes; the device keeps them and
// lays them over each other itself. It knows no fonts, no elements, nothing of
// what a piece shows: a moved piece is a new place, a faded one a new opacity,
// a bar's value the share of its fill to show - a few bytes, not its pixels.
// (anydeck: docs/tasks/01-ausgaenge-runde-24.md, "Bildstücke auf dem Gerät".)
//
// Every packet is one report 0x20 of 63 bytes, its first byte says what it is:
//
//   0x02 data        number within the piece (2), then up to 60 bytes
//   0x03 show        what changed since the last show appears at once
//   0x04 brightness  display (1), 0..255
//   0x05 own screen  the device goes back to drawing its own status screen
//   0x06 clear       display (1): every piece gone
//   0x07 piece       display, id (1 each), x, y (2 each, signed), width, height
//                    (2 each), alpha (1: 0 none, 1 one byte per pixel), length
//                    of the colours, length of the alpha (4 each); the data
//                    follows in 0x02 packets, colours then alpha
//   0x08 place       display, id, x, y, opacity (0..255), reveal (1), amount
//                    (2, 0..65535), start, sweep (2 each, degrees, signed),
//                    centre x, centre y (2 each, from the piece's corner)
//   0x09 remove      display, id
//
// All numbers little endian. Colours are RGB565, PackBits on whole pixels: a
// header byte n of 0..127 is followed by n + 1 literal pixels, 128..255 by one
// pixel that repeats n - 126 times. The alpha is PackBits the same way on
// bytes. Pieces are drawn in the order of their ids, 0 at the bottom - the PC
// sends the background as piece 0. A piece sent again under its id keeps its
// opacity and share, and takes the new x and y.
//
// Reveal - which share of a piece shows (amount / 65535 of it):
//   0 all of it    1 from the left    2 from the bottom    3 from the right
//   4 from the top     5 clockwise from `start` over `sweep` degrees, around
//                      the centre (0° points right, angles grow clockwise)
//
// Packets arrive on the USB task. Pieces are only decoded into PSRAM there -
// drawing over SPI would hold up USB - and the main loop puts together what
// changed and pushes it to the panel when a "show" came in.

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

  // From the main loop: puts together and pushes what a "show" asked for,
  // applies the brightness, frees what was replaced. True while the PC's
  // picture is on the panel - the status screen must not draw then.
  bool update();

  // From the main loop: the PC asked for the device's own screen back, once.
  bool takeOwnScreenRequest();

  static constexpr uint8_t kMaxPieces = 64;

 private:
  struct Piece {
    bool used = false;
    int16_t x = 0, y = 0;
    uint16_t w = 0, h = 0;
    uint16_t* colours = nullptr;
    uint8_t* alpha = nullptr;  // nullptr: every pixel covers
    uint8_t opacity = 255;
    uint8_t reveal = 0;
    uint16_t amount = 65535;
    int16_t start = 0, sweep = 360;
    int16_t centreX = 0, centreY = 0;
  };

  void beginPiece(const uint8_t* payload, uint16_t length);
  void data(const uint8_t* payload, uint16_t length);
  void decode(uint8_t byte);
  void put(uint16_t value);
  void finishPiece();
  void dropIncoming();
  void place(const uint8_t* payload, uint16_t length);
  void remove(uint8_t id);
  void clear();
  void show();

  // Under lock_: a piece's area is to be put together again.
  void markDirty(int16_t x, int16_t y, int16_t w, int16_t h);
  // Under lock_: memory the main loop frees once nothing draws with it.
  void discard(void* memory);

  void compose(const Piece* pieces, int16_t left, int16_t top, int16_t right, int16_t bottom);
  static bool revealed(const Piece& piece, int16_t x, int16_t y);

  Display& display_;
  uint16_t* frame_ = nullptr;

  // Written by the USB task under lock_, read by the loop as a copy.
  Piece pieces_[kMaxPieces];

  // --- the piece being received (USB task only) ----------------------------
  bool receiving_ = false;
  Piece incoming_;
  uint8_t incomingId_ = 0;
  bool inAlpha_ = false;       // decoding the alpha plane, after the colours
  uint32_t bytesLeft_ = 0;     // of the current plane
  uint32_t alphaBytes_ = 0;    // of the alpha plane, to come
  uint32_t written_ = 0;       // values of the current plane
  uint16_t nextNumber_ = 0;

  // Decoder state, so a pixel or a PackBits run may span two packets.
  uint8_t lowByte_ = 0;
  bool haveLowByte_ = false;
  uint8_t literalsLeft_ = 0;
  uint8_t repeatCount_ = 0;
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

  static constexpr uint16_t kGarbage = 2 * kMaxPieces + 8;
  void* garbage_[kGarbage] = {};
  uint16_t garbageCount_ = 0;

  bool active_ = false;  // the PC's picture is on the panel (loop only)
};
