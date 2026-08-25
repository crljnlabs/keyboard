// The USB HID interface: how the PC learns what this device is, and how key and
// encoder activity reaches it.
//
// Nothing here may be claimed by an operating system driver: a key press is for
// anydeck to act on, not for the focused window to receive. What decides that is
// the usage of every *collection* in the report descriptor, not only of the
// outermost one. macOS publishes one DeviceUsagePair per application and per
// physical collection, and its drivers match against that whole list - so a
// vendor-defined application collection hides nothing that is named with a
// standard usage inside it. Every collection here is therefore vendor defined.
//
// The items *within* those collections do use standard usages - buttons on the
// button page, the encoder as a dial - because they are not matching surface on
// macOS, and because it lets software that has never heard of this device tell
// the six keys and the encoder apart. Linux is less careful about this and maps
// such items to input events regardless of the collection around them; moving
// them to vendor usages too is what that would take.

#pragma once

#include <Arduino.h>
#include <USBHID.h>

#include "switches.h"

class KeypadHid : public USBHIDDevice {
 public:
  // Registers the interface with the USB stack. This has to happen from a
  // constructor of a global object: the Arduino core calls USB.begin() in
  // app_main, before setup() runs, and the report descriptor has to be known by
  // then or the interface never appears. Global constructors run earlier still,
  // which is exactly how USBHIDKeyboard does it.
  KeypadHid();

  // Call from setup(). Only prepares the send mutex; the interface itself is
  // already registered by the time this runs.
  void begin();

  // --- state, fed in from the input classes --------------------------------
  void setKey(uint8_t index, bool pressed);
  void setEncoderButton(bool pressed);
  // Detents since the last report. Accumulates, so nothing is lost between two
  // reports even when the knob is spun quickly.
  void addRotation(int8_t detents);

  // Sends a report if anything changed and the host is listening. Call from the
  // main loop.
  void update();

  bool connected();

  // --- USBHIDDevice ---------------------------------------------------------
  uint16_t _onGetDescriptor(uint8_t* buffer) override;
  uint16_t _onGetFeature(uint8_t reportId, uint8_t* buffer, uint16_t length) override;

 private:
  USBHID hid_;

  // One bit per key, bit 0 = SW1.
  uint8_t keyMask_ = 0;
  bool encoderButton_ = false;
  int16_t pendingRotation_ = 0;

  uint8_t sentKeyMask_ = 0;
  bool sentEncoderButton_ = false;
  bool everSent_ = false;
};
