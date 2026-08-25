#include "keypad_hid.h"

#include "display.h"

namespace {

// --- report ids -------------------------------------------------------------
constexpr uint8_t kReportInput = 0x01;
constexpr uint8_t kReportCapabilities = 0x10;

// --- the report descriptor --------------------------------------------------
// A sequence of items. Most only fill in a value the reader keeps ("usage page",
// "report size"); only Input/Output/Feature turn the values collected so far
// into actual fields.
const uint8_t kReportDescriptor[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (0x01)
    0xA1, 0x01,        // Collection (Application)

    0x85, kReportInput,  //   Report ID (1)

    // --- the six keys, one part per button ---------------------------------
    // The group is named with a vendor usage, not with Generic Desktop's
    // "Keypad". A collection's usage is not private to the collection: macOS
    // publishes one DeviceUsagePair per Application *and* Physical collection in
    // the descriptor, and its HID Keyboard Driver matches on Generic Desktop
    // usages 0x06 Keyboard, 0x07 Keypad, 0x08 Multi-axis and 0x80 System
    // Control. Naming this group "Keypad" therefore handed the device to that
    // driver - which then turned the buttons below into pointer clicks in the
    // focused window - even though the enclosing application collection is
    // vendor defined. Nothing else in the descriptor is enough to match, so a
    // vendor usage here is what keeps the operating system out.
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x02,        //   Usage (0x02) - names the group
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x06,        //     Usage Maximum (Button 6)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x06,        //     Report Count (6)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x75, 0x02,        //     Report Size (2)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x03,        //     Input (Constant) - padding to a byte
    0xC0,              //   End Collection

    // --- the encoder: dial and click are one part --------------------------
    // A vendor usage again, for the reason above. Generic Desktop's "Dial" is
    // not one of the four the keyboard driver claims, but a collection usage is
    // matching surface either way, and the two groups should not differ in how
    // careful they are.
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x03,        //   Usage (0x03) - names the group
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x09, 0x07,        //     Usage (Button 7) - the click
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x75, 0x07,        //     Report Size (7)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x03,        //     Input (Constant) - padding to a byte
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x37,        //     Usage (Dial)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,              //   End Collection

    // --- what the descriptor cannot express: the display -------------------
    0x85, kReportCapabilities,  //   Report ID (0x10)
    0x06, 0x00, 0xFF,           //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x10,                 //   Usage (0x10)
    0x15, 0x00,                 //   Logical Minimum (0)
    0x26, 0xFF, 0x00,           //   Logical Maximum (255)
    0x75, 0x08,                 //   Report Size (8)
    0x95, 0x3F,                 //   Report Count (63)
    0xB1, 0x02,                 //   Feature (Data, Variable, Absolute)

    // --- reserved for display data, PC to device ---------------------------
    0x85, 0x20,  //   Report ID (0x20)
    0x09, 0x20,  //   Usage (0x20)
    0x75, 0x08,  //   Report Size (8)
    0x95, 0x3F,  //   Report Count (63)
    0x91, 0x02,  //   Output (Data, Variable, Absolute)

    0xC0,  // End Collection
};

// --- capability block -------------------------------------------------------
// A list of entries: one type byte, one length byte, then that many payload
// bytes. A reader that does not know an entry type skips it using the length,
// which is what lets this grow later without breaking an older PC application.
constexpr uint8_t kEntryDeviceInfo = 0x01;
constexpr uint8_t kEntryDisplay = 0x02;
constexpr uint8_t kEntryEnd = 0x00;

constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kDisplayCount = 1;
constexpr uint8_t kPixelFormatRgb565 = 1;

// Physical size of the visible area, in tenths of a millimetre. The 1.69 inch
// panel measures 32.6 by 27.9 mm in this orientation.
constexpr uint16_t kPhysicalWidthTenthMm = 326;
constexpr uint16_t kPhysicalHeightTenthMm = 279;

// Bit 0: backlight is dimmable. Bit 1: accepts a full frame.
constexpr uint8_t kDisplayFlags = 0b00000011;

}  // namespace

KeypadHid::KeypadHid() : hid_() {
  static bool registered = false;
  if (!registered) {
    registered = true;
    hid_.addDevice(this, sizeof(kReportDescriptor));
  }
}

void KeypadHid::begin() { hid_.begin(); }

bool KeypadHid::connected() { return hid_.ready(); }

uint16_t KeypadHid::_onGetDescriptor(uint8_t* buffer) {
  memcpy(buffer, kReportDescriptor, sizeof(kReportDescriptor));
  return sizeof(kReportDescriptor);
}

uint16_t KeypadHid::_onGetFeature(uint8_t reportId, uint8_t* buffer, uint16_t length) {
  if (reportId != kReportCapabilities) {
    return 0;
  }

  // Mandatory fields first, then the optional ones in a fixed order: the length
  // byte says how far this device got, and a reader takes the documented default
  // for everything past it. This device sends all of them.
  const uint8_t displayPayload[] = {
      static_cast<uint8_t>(Display::kWidth & 0xFF),
      static_cast<uint8_t>(Display::kWidth >> 8),
      static_cast<uint8_t>(Display::kHeight & 0xFF),
      static_cast<uint8_t>(Display::kHeight >> 8),
      kPixelFormatRgb565,
      0,  // display index
      Display::kRotation,
      Display::kSafeInset,  // corner radius
      static_cast<uint8_t>(kPhysicalWidthTenthMm & 0xFF),
      static_cast<uint8_t>(kPhysicalWidthTenthMm >> 8),
      static_cast<uint8_t>(kPhysicalHeightTenthMm & 0xFF),
      static_cast<uint8_t>(kPhysicalHeightTenthMm >> 8),
      kDisplayFlags,
  };

  const uint16_t needed = 2 + 2 + 2 + sizeof(displayPayload) + 2;
  if (length < needed) {
    return 0;
  }

  uint16_t at = 0;
  buffer[at++] = kEntryDeviceInfo;
  buffer[at++] = 2;
  buffer[at++] = kProtocolVersion;
  buffer[at++] = kDisplayCount;

  buffer[at++] = kEntryDisplay;
  buffer[at++] = sizeof(displayPayload);
  memcpy(buffer + at, displayPayload, sizeof(displayPayload));
  at += sizeof(displayPayload);

  buffer[at++] = kEntryEnd;
  buffer[at++] = 0;
  return at;
}

void KeypadHid::setKey(uint8_t index, bool pressed) {
  if (index >= Switches::count()) {
    return;
  }
  if (pressed) {
    keyMask_ |= static_cast<uint8_t>(1u << index);
  } else {
    keyMask_ &= static_cast<uint8_t>(~(1u << index));
  }
}

void KeypadHid::setEncoderButton(bool pressed) { encoderButton_ = pressed; }

void KeypadHid::addRotation(int8_t detents) { pendingRotation_ += detents; }

void KeypadHid::update() {
  const bool changed = !everSent_ || keyMask_ != sentKeyMask_ ||
                       encoderButton_ != sentEncoderButton_ || pendingRotation_ != 0;
  if (!changed || !hid_.ready()) {
    return;
  }

  // One report carries at most a full byte of rotation. Anything beyond that
  // stays in the accumulator and goes out with the next report rather than being
  // clipped, which keeps a fast spin accurate.
  int16_t rotation = pendingRotation_;
  if (rotation > 127) {
    rotation = 127;
  } else if (rotation < -127) {
    rotation = -127;
  }

  const uint8_t payload[] = {
      keyMask_,
      static_cast<uint8_t>(encoderButton_ ? 1 : 0),
      static_cast<uint8_t>(static_cast<int8_t>(rotation)),
  };
  if (!hid_.SendReport(kReportInput, payload, sizeof(payload))) {
    return;  // host not ready; keep the state and try again next pass
  }

  pendingRotation_ -= rotation;
  sentKeyMask_ = keyMask_;
  sentEncoderButton_ = encoderButton_;
  everSent_ = true;
}
