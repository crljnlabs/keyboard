#include "display_feed.h"

namespace {

constexpr uint8_t kRectangle = 0x01;
constexpr uint8_t kData = 0x02;
constexpr uint8_t kShow = 0x03;
constexpr uint8_t kBrightness = 0x04;
constexpr uint8_t kOwnScreen = 0x05;

constexpr int16_t kWidth = Display::kWidth;
constexpr int16_t kHeight = Display::kHeight;

uint16_t le16(const uint8_t* at) { return static_cast<uint16_t>(at[0] | (at[1] << 8)); }

uint32_t le32(const uint8_t* at) {
  return static_cast<uint32_t>(at[0]) | (static_cast<uint32_t>(at[1]) << 8) |
         (static_cast<uint32_t>(at[2]) << 16) | (static_cast<uint32_t>(at[3]) << 24);
}

}  // namespace

bool DisplayFeed::begin() {
  const size_t bytes = static_cast<size_t>(kWidth) * kHeight * sizeof(uint16_t);
  frame_ = static_cast<uint16_t*>(ps_malloc(bytes));
  if (frame_ == nullptr) {
    frame_ = static_cast<uint16_t*>(malloc(bytes));
  }
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
    case kRectangle:
      beginRectangle(payload, payloadLength);
      break;
    case kData:
      data(payload, payloadLength);
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

void DisplayFeed::beginRectangle(const uint8_t* payload, uint16_t length) {
  receiving_ = false;
  if (length < 14 || payload[0] != 0) {
    return;
  }
  const uint16_t x = le16(payload + 1);
  const uint16_t y = le16(payload + 3);
  const uint16_t w = le16(payload + 5);
  const uint16_t h = le16(payload + 7);
  const uint8_t encoding = payload[9];
  const uint32_t dataLength = le32(payload + 10);
  if (w == 0 || h == 0 || x + w > kWidth || y + h > kHeight || dataLength == 0) {
    return;
  }
  if (encoding == static_cast<uint8_t>(Encoding::kRaw)) {
    if (dataLength != static_cast<uint32_t>(w) * h * 2) {
      return;
    }
  } else if (encoding != static_cast<uint8_t>(Encoding::kPackBits)) {
    return;
  }

  rectX_ = x;
  rectY_ = y;
  rectW_ = w;
  rectH_ = h;
  encoding_ = static_cast<Encoding>(encoding);
  bytesLeft_ = dataLength;
  pixelsWritten_ = 0;
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
    receiving_ = false;  // a packet went missing: drop the rectangle
    return;
  }
  ++nextNumber_;
  for (uint16_t i = 2; i < length && receiving_ && bytesLeft_ > 0; ++i) {
    --bytesLeft_;
    decode(payload[i]);
  }
  if (!receiving_) {
    return;
  }
  const uint32_t total = static_cast<uint32_t>(rectW_) * rectH_;
  if (pixelsWritten_ >= total || bytesLeft_ == 0) {
    receiving_ = false;
    if (pixelsWritten_ < total) {
      return;  // the data ran out early: not shown
    }
    portENTER_CRITICAL(&lock_);
    dirtyLeft_ = min<int16_t>(dirtyLeft_, rectX_);
    dirtyTop_ = min<int16_t>(dirtyTop_, rectY_);
    dirtyRight_ = max<int16_t>(dirtyRight_, rectX_ + rectW_);
    dirtyBottom_ = max<int16_t>(dirtyBottom_, rectY_ + rectH_);
    portEXIT_CRITICAL(&lock_);
  }
}

void DisplayFeed::decode(uint8_t byte) {
  if (encoding_ == Encoding::kPackBits && needHeader_) {
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
  if (!haveLowByte_) {
    lowByte_ = byte;
    haveLowByte_ = true;
    return;
  }
  haveLowByte_ = false;
  const uint16_t pixel = static_cast<uint16_t>(lowByte_ | (byte << 8));
  if (encoding_ == Encoding::kRaw) {
    putPixel(pixel);
    return;
  }
  if (repeatCount_ > 0) {
    for (uint8_t i = 0; i < repeatCount_; ++i) {
      putPixel(pixel);
    }
    repeatCount_ = 0;
    needHeader_ = true;
  } else {
    putPixel(pixel);
    if (--literalsLeft_ == 0) {
      needHeader_ = true;
    }
  }
}

void DisplayFeed::putPixel(uint16_t pixel) {
  if (pixelsWritten_ >= static_cast<uint32_t>(rectW_) * rectH_) {
    return;
  }
  const uint32_t row = pixelsWritten_ / rectW_;
  const uint32_t column = pixelsWritten_ % rectW_;
  frame_[(rectY_ + row) * kWidth + rectX_ + column] = pixel;
  ++pixelsWritten_;
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

bool DisplayFeed::update() {
  portENTER_CRITICAL(&lock_);
  const bool pending = showPending_;
  const int16_t left = showLeft_, top = showTop_, right = showRight_, bottom = showBottom_;
  showPending_ = false;
  const int16_t brightness = brightnessPending_;
  brightnessPending_ = -1;
  portEXIT_CRITICAL(&lock_);

  if (brightness >= 0) {
    display_.setBrightness(static_cast<uint8_t>(brightness));
  }
  if (pending) {
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
