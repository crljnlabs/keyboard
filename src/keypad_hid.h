// The USB HID interface: how the PC learns what this device is, and how key and
// encoder activity reaches it.
//
// The device is one vendor-defined application collection, so no operating system
// claims it and no key press is injected into the focused window. Inside that
// collection every item uses a standard usage, so software that has never heard
// of this device can still tell the six keys and the encoder apart.
//
// The byte layouts are specified in ../../hid.md; this file implements them.

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
