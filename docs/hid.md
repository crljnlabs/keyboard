# USB protocol of the crljn keypad

What this firmware tells the PC about itself, and what it sends, byte for byte.

The rules behind it are not this device's own. A device that is to work with
anydeck keeps to anydeck's device contract, `docs/device-protocol.md` in the
anydeck repository (github.com/crljnlabs/anydeck): why every collection is
vendor-defined, which standard usages anydeck recognises, why the serial number
matters, and how the capability report is built, with all its defaults. This
file is the keypad's answer to it and does not repeat it.

Status in this firmware:

| | |
|---|---|
| report descriptor | offered, 110 bytes |
| report `0x01`, input | sent |
| report `0x10`, capability block | answered |
| report `0x20`, display data | declared, and ignored when it arrives |

What anydeck does with each of them today is in the contract's last section.

## Identity

| | | where it comes from |
|---|---|---|
| vendor id | `0x303A` | the Arduino core's `esp32s3` variant |
| product id | `0x1001` | the Arduino core's `esp32s3` variant |
| manufacturer | `crljn.labs` | `USB_MANUFACTURER` in `platformio.ini` |
| product | `crljn-board` | `USB_PRODUCT` in `platformio.ini` |
| serial number | the chip's MAC address | the Arduino core's default |

The product and the manufacturer are set by this project. The serial number
is the chip's factory MAC address as twelve hex digits - `2884856D6EC0` on the
first unit - so every keypad has its own, which is what the contract asks for.

The vendor and product id are Espressif's generic pair, shared by many ESP32-S3
boards. That does no harm: anydeck does not pick devices by those ids, and the
serial number tells the units apart.

anydeck names a keypad after its product name when it first finds it.

## Report descriptor

110 bytes. Offered by the device at plug-in time. One vendor-defined
application collection, a vendor-defined physical collection for the keys and
one for the encoder, and standard usages on the data items inside.

```c
static const uint8_t kReportDescriptor[] = {
    0x06, 0x00, 0xFF,        // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,              // Usage (0x01)
    0xA1, 0x01,              // Collection (Application)

    0x85, 0x01,              //   Report ID (1)

    0x06, 0x00, 0xFF,        //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x02,              //   Usage (0x02) - the keys
    0xA1, 0x00,              //   Collection (Physical)
    0x05, 0x09,              //     Usage Page (Button)
    0x19, 0x01,              //     Usage Minimum (Button 1)
    0x29, 0x06,              //     Usage Maximum (Button 6)
    0x15, 0x00,              //     Logical Minimum (0)
    0x25, 0x01,              //     Logical Maximum (1)
    0x75, 0x01,              //     Report Size (1)
    0x95, 0x06,              //     Report Count (6)
    0x81, 0x02,              //     Input (Data, Variable, Absolute)
    0x75, 0x02,              //     Report Size (2)
    0x95, 0x01,              //     Report Count (1)
    0x81, 0x03,              //     Input (Constant) - padding to a byte
    0xC0,                    //   End Collection

    0x06, 0x00, 0xFF,        //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x03,              //   Usage (0x03) - the encoder
    0xA1, 0x00,              //   Collection (Physical)
    0x05, 0x09,              //     Usage Page (Button)
    0x09, 0x07,              //     Usage (Button 7) - the encoder's click
    0x15, 0x00,              //     Logical Minimum (0)
    0x25, 0x01,              //     Logical Maximum (1)
    0x75, 0x01,              //     Report Size (1)
    0x95, 0x01,              //     Report Count (1)
    0x81, 0x02,              //     Input (Data, Variable, Absolute)
    0x75, 0x07,              //     Report Size (7)
    0x95, 0x01,              //     Report Count (1)
    0x81, 0x03,              //     Input (Constant) - padding to a byte
    0x05, 0x01,              //     Usage Page (Generic Desktop)
    0x09, 0x37,              //     Usage (Dial)
    0x15, 0x81,              //     Logical Minimum (-127)
    0x25, 0x7F,              //     Logical Maximum (127)
    0x75, 0x08,              //     Report Size (8)
    0x95, 0x01,              //     Report Count (1)
    0x81, 0x06,              //     Input (Data, Variable, Relative)
    0xC0,                    //   End Collection

    0x85, 0x10,              //   Report ID (0x10) - capability block
    0x06, 0x00, 0xFF,        //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x10,              //   Usage (0x10)
    0x15, 0x00,              //   Logical Minimum (0)
    0x26, 0xFF, 0x00,        //   Logical Maximum (255)
    0x75, 0x08,              //   Report Size (8)
    0x95, 0x3F,              //   Report Count (63)
    0xB1, 0x02,              //   Feature (Data, Variable, Absolute)

    0x85, 0x20,              //   Report ID (0x20) - display data, PC -> device
    0x09, 0x20,              //   Usage (0x20)
    0x75, 0x08,              //   Report Size (8)
    0x95, 0x3F,              //   Report Count (63)
    0x91, 0x02,              //   Output (Data, Variable, Absolute)

    0xC0,                    // End Collection
};
```

What anydeck makes of it: six keys, from the six buttons, and one rotary
encoder, from the dial and the button in the same collection.

### Why the keys and the encoder are not labelled `Keypad` and `Dial`

They were once. With the keys' collection labelled `Keypad`, macOS's keyboard
driver took the device and turned its buttons into clicks in the focused
window, although the application collection around it was vendor-defined:
macOS publishes the usage of every application *and* every physical collection,
and its keyboard driver matches on them. Vendor usages `0x02` and `0x03` keep
it out, and cost nothing - what an element is follows from the items inside a
collection, never from its label. `src/keypad_hid.cpp` has the full account.

### Listed three times on macOS

macOS lists every collection usage it finds, not only the application one, so
this device appears **three times** in `hid.enumerate()` - as usages `0x01`,
`0x02` and `0x03` on page `0xFF00` - all carrying the same path. It is one
device, listed once per collection.

## Report 0x01 — input, device to PC, 4 bytes

| Byte | Bits | Meaning |
|---|---|---|
| 0 | 7..0 | report id, always `0x01` |
| 1 | 0..5 | keys SW1..SW6, one bit each, `1` = pressed |
| 1 | 6..7 | padding, always `0` |
| 2 | 0 | encoder push button, `1` = pressed |
| 2 | 1..7 | padding, always `0` |
| 3 | 7..0 | encoder movement since the previous report, signed, `+` = clockwise |

Sent when something changed. Also sent once after start-up, as soon as the PC
has set the device up, with whatever is held at that moment - even if that is
nothing. A program that opens the device only later does not get that first
report, and there is no other way to ask the keypad for its current state.

Byte 3 is a **difference, not a position**. The firmware adds up detents between
reports. A report takes as many of them as fit - up to ±127 - and leaves the
rest for the next one, so a fast spin arrives in full rather than clipped. A
report counts as sent once it is queued for the endpoint; only one that could
not be queued keeps its state for another try, so no detent is lost and none
arrives twice. Apart from the first report, none is sent for a difference of
zero with no button change.

## Report 0x10 — capability block, feature, read by the PC

Declared as 63 bytes. The firmware answers whenever it is asked, with the 21
bytes it has to say rather than 63 - two entries and the end of the list. The
format allows that: a reader goes by the entries, not by the report's length.

On the wire, after the report id:

```
01 02 01 01                                     device info, 2 bytes
02 0D 18 01 F0 00 01 00 03 0E 46 01 17 01 03    display, 13 bytes
00 00                                           end of list
```

| Entry | Byte | Field | Value on this device |
|---|---|---|---|
| `0x01` device info | 0 | protocol version | `1` |
| | 1 | display entries that follow | `1` |
| `0x02` display | 0..1 | width in pixels | `280` |
| | 2..3 | height in pixels | `240` |
| | 4 | pixel format | `1`, RGB565 |
| | 5 | display index | `0` |
| | 6 | rotation the firmware applies, in 90° steps | `3` |
| | 7 | corner radius in pixels | `14` |
| | 8..9 | physical width, tenths of a millimetre | `326` |
| | 10..11 | physical height, tenths of a millimetre | `279` |
| | 12 | capability flags | `0b0000_0011` |

All 13 bytes of the display entry are sent, so no default applies anywhere.

Width and height are the panel as the firmware draws on it, after rotation `3`.
The panel itself is 240 x 280 (`TFT_WIDTH` and `TFT_HEIGHT` in
`platformio.ini`), and 280 x 240 is the same panel lying on its side; the values
come from `Display::kWidth` and `Display::kHeight`.

The physical size is the 1.69 inch panel's visible area, 32.6 by 27.9 mm in this
orientation. The corner radius of `14` is the firmware's safe inset
(`Display::kSafeInset`), not a measurement.

The flags say the backlight can be dimmed (bit 0) and the display takes full
frames (bit 1). Both are true of the panel - the firmware dims the backlight
itself, through PWM - and neither can be reached from the PC yet: there is no
command for the brightness, and nothing receives a frame. The flags describe
the display as it is; using them is the display feed's work.

## Report 0x20 — display data, output, PC to device

Declared as 63 bytes per packet. The firmware ignores whatever arrives on it,
and the framing for pixel data and commands is not specified yet; specifying it
is part of building the display feed. A full frame of this display, 280 x 240 in
RGB565, is 134400 bytes, so it will have to arrive in pieces.

## Open points

- The corner radius of `14` should be measured on the real panel.

## See also

- anydeck's device contract, `docs/device-protocol.md` in the anydeck
  repository: the rules this file follows, the capability block's format with
  all its defaults, and how anydeck finds and reads a device.
- `src/keypad_hid.cpp`: the descriptor and the capability block as the firmware
  builds them.
