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
// the six keys and the encoder apart.
//
// What Linux makes of it has not been tried on a Linux machine yet. Reading
// the kernel's hid-input, it looks for at least one collection with an input
// usage (mouse, keyboard, joystick, ...) before it creates an input device at
// all, and finds none here - but that is a reading, not a test. The test is in
// anydeck's docs/testing.md.

#pragma once

#include <Arduino.h>
#include <USBHID.h>

#include <atomic>

#include "switches.h"

class DisplayFeed;

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

  // --- which buttons report at once: feature report 0x11 -------------------
  // Bit n stands for button n + 1 of the report descriptor: SW1..SW6, then the
  // encoder's click. This is what the keypad starts with, read from flash.
  void setFastButtons(uint8_t mask);
  // A setting the PC sent, once: true and the new mask when one came since the
  // last call. Call from the main loop, which applies and stores it.
  bool takeFastButtons(uint8_t& mask);

  bool connected();

  // Where report 0x20 goes. Without one it is ignored.
  void attachDisplayFeed(DisplayFeed* feed);

  // --- USBHIDDevice ---------------------------------------------------------
  uint16_t _onGetDescriptor(uint8_t* buffer) override;
  uint16_t _onGetFeature(uint8_t reportId, uint8_t* buffer, uint16_t length) override;
  void _onOutput(uint8_t reportId, const uint8_t* buffer, uint16_t length) override;
  void _onSetFeature(uint8_t reportId, const uint8_t* buffer, uint16_t length) override;

 private:
  // Report 0x01's payload as things stand: keys, button, no rotation.
  uint16_t currentInput(uint8_t* buffer, uint16_t length) const;

  USBHID hid_;
  DisplayFeed* feed_ = nullptr;

  // One bit per key, bit 0 = SW1.
  uint8_t keyMask_ = 0;
  bool encoderButton_ = false;
  int16_t pendingRotation_ = 0;

  uint8_t sentKeyMask_ = 0;
  bool sentEncoderButton_ = false;
  bool everSent_ = false;

  // The report handed to the USB task to send - sent there, not from the main
  // loop: TinyUSB is not made for two tasks at once, and a report sent from
  // the loop while the USB task took display packets threw the keypad off
  // the bus now and then. 0 nothing queued, 1 queued, 2 sent, 3 not taken.
  static void sendQueued(void* self);
  uint8_t queued_[3] = {};
  int16_t queuedRotation_ = 0;
  uint8_t queuedKeyMask_ = 0;
  bool queuedEncoderButton_ = false;
  std::atomic<uint8_t> sendState_{0};

  // When the last display packet came in (micros). While they stream in, a
  // report waits for the next gap: this core's USB driver lost the bus now
  // and then when a report went out while OUT packets arrived.
  std::atomic<uint32_t> lastOutputUs_{0};

  // Written by the USB task when the PC sets them, read by the main loop.
  std::atomic<uint8_t> fastButtons_{0};
  std::atomic<bool> fastChanged_{false};
};
