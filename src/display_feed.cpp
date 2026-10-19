#include "display_feed.h"

#include <math.h>

namespace {

constexpr uint8_t kData = 0x02;
constexpr uint8_t kShow = 0x03;
constexpr uint8_t kBrightness = 0x04;
constexpr uint8_t kOwnScreen = 0x05;
constexpr uint8_t kClear = 0x06;
constexpr uint8_t kPiece = 0x07;
constexpr uint8_t kPlace = 0x08;
constexpr uint8_t kRemove = 0x09;

constexpr uint8_t kWhole = 0;
constexpr uint8_t kFromLeft = 1;
constexpr uint8_t kFromBottom = 2;
constexpr uint8_t kFromRight = 3;
constexpr uint8_t kFromTop = 4;
constexpr uint8_t kClockwise = 5;

constexpr int16_t kWidth = Display::kWidth;
constexpr int16_t kHeight = Display::kHeight;

uint16_t le16(const uint8_t* at) { return static_cast<uint16_t>(at[0] | (at[1] << 8)); }
int16_t les16(const uint8_t* at) { return static_cast<int16_t>(le16(at)); }

uint32_t le32(const uint8_t* at) {
  return static_cast<uint32_t>(at[0]) | (static_cast<uint32_t>(at[1]) << 8) |
         (static_cast<uint32_t>(at[2]) << 16) | (static_cast<uint32_t>(at[3]) << 24);
}

void* bigAlloc(size_t bytes) {
  void* memory = ps_malloc(bytes);
  return memory != nullptr ? memory : malloc(bytes);
}

// How many of `size` rows or columns an amount of 0..65535 shows - rounded,
// the same as anydeck's scene.py.
uint32_t share(uint32_t size, uint16_t amount) { return (size * amount + 32767) / 65535; }

// `source` over `below` with an alpha of 0..255, per RGB565 component.
uint16_t blend(uint16_t source, uint16_t below, uint8_t alpha) {
  const uint32_t a = alpha, b = 255 - alpha;
  const uint32_t r = (((source >> 11) & 0x1F) * a + ((below >> 11) & 0x1F) * b + 127) / 255;
  const uint32_t g = (((source >> 5) & 0x3F) * a + ((below >> 5) & 0x3F) * b + 127) / 255;
  const uint32_t bl = ((source & 0x1F) * a + (below & 0x1F) * b + 127) / 255;
  return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

}  // namespace

bool DisplayFeed::begin() {
  const size_t bytes = static_cast<size_t>(kWidth) * kHeight * sizeof(uint16_t);
  frame_ = static_cast<uint16_t*>(bigAlloc(bytes));
  if (frame_ == nullptr) {
    return false;
  }
  memset(frame_, 0, bytes);
  return true;
}

void DisplayFeed::receive(const uint8_t* packet, uint16_t length) {
  if (frame_ == nullptr || length == 0) {
    return;
  }
  const uint8_t* payload = packet + 1;
  const uint16_t payloadLength = length - 1;
  switch (packet[0]) {
    case kPiece:
      beginPiece(payload, payloadLength);
      break;
    case kData:
      data(payload, payloadLength);
      break;
    case kPlace:
      place(payload, payloadLength);
      break;
    case kRemove:
      if (payloadLength >= 2 && payload[0] == 0) {
        remove(payload[1]);
      }
      break;
    case kClear:
      if (payloadLength >= 1 && payload[0] == 0) {
        clear();
      }
      break;
    case kShow:
      show();
      break;
    case kBrightness:
      if (payloadLength >= 2 && payload[0] == 0) {
        portENTER_CRITICAL(&lock_);
        brightnessPending_ = payload[1];
        portEXIT_CRITICAL(&lock_);
      }
      break;
    case kOwnScreen:
      portENTER_CRITICAL(&lock_);
      ownScreenPending_ = true;
      showPending_ = false;
      portEXIT_CRITICAL(&lock_);
      break;
    default:
      break;  // unknown commands are skipped, so a newer PC can try things
  }
}

// --- receiving a piece (USB task) ---------------------------------------------

void DisplayFeed::beginPiece(const uint8_t* payload, uint16_t length) {
  dropIncoming();
  if (length < 19 || payload[0] != 0) {
    return;
  }
  const uint8_t id = payload[1];
  const int16_t x = les16(payload + 2);
  const int16_t y = les16(payload + 4);
  const uint16_t w = le16(payload + 6);
  const uint16_t h = le16(payload + 8);
  const bool alpha = payload[10] != 0;
  const uint32_t colourBytes = le32(payload + 11);
  const uint32_t alphaBytes = le32(payload + 15);
  const uint32_t pixels = static_cast<uint32_t>(w) * h;
  if (id >= kMaxPieces || pixels == 0 || pixels > static_cast<uint32_t>(kWidth) * kHeight ||
      colourBytes == 0 || (alpha && alphaBytes == 0)) {
    return;
  }
  incoming_ = Piece();
  incoming_.colours = static_cast<uint16_t*>(bigAlloc(pixels * sizeof(uint16_t)));
  incoming_.alpha = alpha ? static_cast<uint8_t*>(bigAlloc(pixels)) : nullptr;
  if (incoming_.colours == nullptr || (alpha && incoming_.alpha == nullptr)) {
    dropIncoming();  // no room: the piece is not taken, what was there stays
    return;
  }
  incoming_.x = x;
  incoming_.y = y;
  incoming_.w = w;
  incoming_.h = h;
  incomingId_ = id;
  inAlpha_ = false;
  bytesLeft_ = colourBytes;
  alphaBytes_ = alpha ? alphaBytes : 0;
  written_ = 0;
  nextNumber_ = 0;
  haveLowByte_ = false;
  literalsLeft_ = 0;
  repeatCount_ = 0;
  needHeader_ = true;
  receiving_ = true;
}

void DisplayFeed::data(const uint8_t* payload, uint16_t length) {
  if (!receiving_ || length < 2) {
    return;
  }
  if (le16(payload) != nextNumber_) {
    dropIncoming();  // a packet went missing: the piece is not taken
    return;
  }
  ++nextNumber_;
  const uint32_t total = static_cast<uint32_t>(incoming_.w) * incoming_.h;
  for (uint16_t i = 2; i < length && receiving_; ++i) {
    if (bytesLeft_ == 0) {
      dropIncoming();  // the data ran out before the pixels did
      return;
    }
    --bytesLeft_;
    decode(payload[i]);
    if (written_ < total) {
      continue;
    }
    if (!inAlpha_ && incoming_.alpha != nullptr) {
      // The colours are complete; what follows is the alpha.
      inAlpha_ = true;
      bytesLeft_ = alphaBytes_;
      written_ = 0;
      literalsLeft_ = 0;
      repeatCount_ = 0;
      needHeader_ = true;
      continue;
    }
    finishPiece();
    return;
  }
}

void DisplayFeed::decode(uint8_t byte) {
  if (needHeader_) {
    if (byte < 128) {
      literalsLeft_ = byte + 1;
      repeatCount_ = 0;
    } else {
      repeatCount_ = byte - 126;
      literalsLeft_ = 0;
    }
    needHeader_ = false;
    return;
  }
  uint16_t value = byte;
  if (!inAlpha_) {
    if (!haveLowByte_) {
      lowByte_ = byte;
      haveLowByte_ = true;
      return;
    }
    haveLowByte_ = false;
    value = static_cast<uint16_t>(lowByte_ | (byte << 8));
  }
  if (repeatCount_ > 0) {
    for (uint8_t i = 0; i < repeatCount_; ++i) {
      put(value);
    }
    repeatCount_ = 0;
    needHeader_ = true;
  } else {
    put(value);
    if (--literalsLeft_ == 0) {
      needHeader_ = true;
    }
  }
}

void DisplayFeed::put(uint16_t value) {
  const uint32_t total = static_cast<uint32_t>(incoming_.w) * incoming_.h;
  if (written_ >= total) {
    return;
  }
  if (inAlpha_) {
    incoming_.alpha[written_] = static_cast<uint8_t>(value);
  } else {
    incoming_.colours[written_] = value;
  }
  ++written_;
}

void DisplayFeed::finishPiece() {
  receiving_ = false;
  portENTER_CRITICAL(&lock_);
  if (garbageCount_ + 2 > kGarbage) {
    portEXIT_CRITICAL(&lock_);
    dropIncoming();  // the loop has not caught up: what was there stays
    return;
  }
  Piece& slot = pieces_[incomingId_];
  if (slot.used) {
    // Sent again: it keeps its opacity and share, takes the new place.
    incoming_.opacity = slot.opacity;
    incoming_.reveal = slot.reveal;
    incoming_.amount = slot.amount;
    incoming_.start = slot.start;
    incoming_.sweep = slot.sweep;
    incoming_.centreX = slot.centreX;
    incoming_.centreY = slot.centreY;
    markDirty(slot.x, slot.y, slot.w, slot.h);
    discard(slot.colours);
    discard(slot.alpha);
  }
  incoming_.used = true;
  slot = incoming_;
  markDirty(slot.x, slot.y, slot.w, slot.h);
  portEXIT_CRITICAL(&lock_);
  incoming_.colours = nullptr;  // the table has them now
  incoming_.alpha = nullptr;
}

void DisplayFeed::dropIncoming() {
  receiving_ = false;
  free(incoming_.colours);  // never in the table: the USB task's own
  free(incoming_.alpha);
  incoming_.colours = nullptr;
  incoming_.alpha = nullptr;
}

// --- places and the rest (USB task) -------------------------------------------

void DisplayFeed::place(const uint8_t* payload, uint16_t length) {
  if (length < 18 || payload[0] != 0 || payload[1] >= kMaxPieces) {
    return;
  }
  portENTER_CRITICAL(&lock_);
  Piece& piece = pieces_[payload[1]];
  if (piece.used) {
    markDirty(piece.x, piece.y, piece.w, piece.h);
    piece.x = les16(payload + 2);
    piece.y = les16(payload + 4);
    piece.opacity = payload[6];
    piece.reveal = payload[7];
    piece.amount = le16(payload + 8);
    piece.start = les16(payload + 10);
    piece.sweep = les16(payload + 12);
    piece.centreX = les16(payload + 14);
    piece.centreY = les16(payload + 16);
    markDirty(piece.x, piece.y, piece.w, piece.h);
  }
  portEXIT_CRITICAL(&lock_);
}

void DisplayFeed::remove(uint8_t id) {
  if (id >= kMaxPieces) {
    return;
  }
  portENTER_CRITICAL(&lock_);
  Piece& piece = pieces_[id];
  if (piece.used && garbageCount_ + 2 <= kGarbage) {
    markDirty(piece.x, piece.y, piece.w, piece.h);
    discard(piece.colours);
    discard(piece.alpha);
    piece = Piece();
  }
  portEXIT_CRITICAL(&lock_);
}

void DisplayFeed::clear() {
  portENTER_CRITICAL(&lock_);
  for (Piece& piece : pieces_) {
    if (piece.used && garbageCount_ + 2 <= kGarbage) {
      discard(piece.colours);
      discard(piece.alpha);
      piece = Piece();
    }
  }
  markDirty(0, 0, kWidth, kHeight);
  portEXIT_CRITICAL(&lock_);
}

void DisplayFeed::markDirty(int16_t x, int16_t y, int16_t w, int16_t h) {
  const int16_t left = max<int16_t>(0, x);
  const int16_t top = max<int16_t>(0, y);
  const int16_t right = min<int16_t>(kWidth, x + w);
  const int16_t bottom = min<int16_t>(kHeight, y + h);
  if (right <= left || bottom <= top) {
    return;  // entirely off the display
  }
  dirtyLeft_ = min(dirtyLeft_, left);
  dirtyTop_ = min(dirtyTop_, top);
  dirtyRight_ = max(dirtyRight_, right);
  dirtyBottom_ = max(dirtyBottom_, bottom);
}

void DisplayFeed::discard(void* memory) {
  if (memory != nullptr) {
    garbage_[garbageCount_++] = memory;
  }
}

void DisplayFeed::show() {
  portENTER_CRITICAL(&lock_);
  if (dirtyRight_ > dirtyLeft_ && dirtyBottom_ > dirtyTop_) {
    if (showPending_) {
      showLeft_ = min(showLeft_, dirtyLeft_);
      showTop_ = min(showTop_, dirtyTop_);
      showRight_ = max(showRight_, dirtyRight_);
      showBottom_ = max(showBottom_, dirtyBottom_);
    } else {
      showLeft_ = dirtyLeft_;
      showTop_ = dirtyTop_;
      showRight_ = dirtyRight_;
      showBottom_ = dirtyBottom_;
    }
    showPending_ = true;
  }
  dirtyLeft_ = dirtyTop_ = INT16_MAX;
  dirtyRight_ = dirtyBottom_ = -1;
  portEXIT_CRITICAL(&lock_);
}

// --- putting together (main loop) ---------------------------------------------

bool DisplayFeed::update() {
  static Piece snapshot[kMaxPieces];
  void* freeing[kGarbage];

  portENTER_CRITICAL(&lock_);
  const bool pending = showPending_;
  const int16_t left = showLeft_, top = showTop_, right = showRight_, bottom = showBottom_;
  showPending_ = false;
  const int16_t brightness = brightnessPending_;
  brightnessPending_ = -1;
  // What was replaced before this moment: nothing drawn from now on uses it.
  const uint16_t freeCount = garbageCount_;
  memcpy(freeing, garbage_, freeCount * sizeof(void*));
  garbageCount_ = 0;
  if (pending) {
    memcpy(snapshot, pieces_, sizeof(pieces_));
  }
  portEXIT_CRITICAL(&lock_);

  for (uint16_t i = 0; i < freeCount; ++i) {
    free(freeing[i]);
  }
  if (brightness >= 0) {
    display_.setBrightness(static_cast<uint8_t>(brightness));
  }
  if (pending) {
    compose(snapshot, left, top, right, bottom);
    // Row by row out of the frame buffer: a rectangle inside it is not
    // contiguous. The buffer holds the pixel values themselves; the panel
    // wants the high byte first, which is what swapping does.
    TFT_eSPI& tft = display_.tft();
    const bool swapped = tft.getSwapBytes();
    tft.setSwapBytes(true);
    tft.startWrite();
    for (int16_t y = top; y < bottom; ++y) {
      tft.pushImage(left, y, right - left, 1, frame_ + y * kWidth + left);
    }
    tft.endWrite();
    tft.setSwapBytes(swapped);
    active_ = true;
  }
  return active_;
}

void DisplayFeed::compose(const Piece* pieces, int16_t left, int16_t top, int16_t right, int16_t bottom) {
  for (int16_t y = top; y < bottom; ++y) {
    memset(frame_ + y * kWidth + left, 0, (right - left) * sizeof(uint16_t));
  }
  for (uint8_t id = 0; id < kMaxPieces; ++id) {
    const Piece& piece = pieces[id];
    if (!piece.used) {
      continue;
    }
    const int16_t x0 = max<int16_t>(left, piece.x);
    const int16_t y0 = max<int16_t>(top, piece.y);
    const int16_t x1 = min<int16_t>(right, piece.x + piece.w);
    const int16_t y1 = min<int16_t>(bottom, piece.y + piece.h);
    for (int16_t y = y0; y < y1; ++y) {
      uint16_t* row = frame_ + y * kWidth;
      const int16_t ly = y - piece.y;
      for (int16_t x = x0; x < x1; ++x) {
        const int16_t lx = x - piece.x;
        if (!revealed(piece, lx, ly)) {
          continue;
        }
        const uint32_t at = static_cast<uint32_t>(ly) * piece.w + lx;
        uint32_t alpha = piece.alpha != nullptr ? piece.alpha[at] : 255;
        if (piece.opacity < 255) {
          alpha = alpha * piece.opacity / 255;
        }
        if (alpha == 0) {
          continue;
        }
        row[x] = alpha == 255 ? piece.colours[at] : blend(piece.colours[at], row[x], alpha);
      }
    }
  }
}

bool DisplayFeed::revealed(const Piece& piece, int16_t x, int16_t y) {
  switch (piece.reveal) {
    case kWhole:
      return true;
    case kFromLeft:
      return static_cast<uint32_t>(x) < share(piece.w, piece.amount);
    case kFromRight:
      return static_cast<uint32_t>(x) >= piece.w - share(piece.w, piece.amount);
    case kFromBottom:
      return static_cast<uint32_t>(y) >= piece.h - share(piece.h, piece.amount);
    case kFromTop:
      return static_cast<uint32_t>(y) < share(piece.h, piece.amount);
    case kClockwise: {
      if (piece.amount == 65535) {
        return true;
      }
      if (piece.amount == 0) {
        return false;
      }
      // The angle of the pixel's middle around the centre, clockwise from
      // the start - as anydeck's scene.py does it.
      const float angle = atan2f(y + 0.5f - piece.centreY, x + 0.5f - piece.centreX) * 57.2957795f;
      const float from = fmodf(angle - piece.start + 720.0f, 360.0f);
      return from < piece.sweep * (piece.amount / 65535.0f);
    }
    default:
      return true;
  }
}

bool DisplayFeed::takeOwnScreenRequest() {
  portENTER_CRITICAL(&lock_);
  const bool requested = ownScreenPending_;
  ownScreenPending_ = false;
  portEXIT_CRITICAL(&lock_);
  if (requested) {
    active_ = false;
  }
  return requested;
}
