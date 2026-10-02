# USB protocol between the keypad and anydeck

Normative part only: the byte layouts both halves have to agree on. The
explanation of *why* it looks like this, and what every HID term means, is in the
companion document (see the end of this file).

Status: implemented on both sides. The descriptor below is the one the firmware
actually offers, read back from a running device rather than written by hand.

## Design rule

The device is one **vendor-defined** application collection, so no operating
system claims it and no key press is ever injected into the focused window. But
every data item *inside* that collection uses a **standard usage**, so a parser
that knows nothing about this particular device can still tell what the items
mean. Standard where a standard exists, vendor-defined only where none does.

## Report descriptor

110 bytes. Offered by the device at plug-in time.

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

### On the collection labels

The two physical collections carry vendor-defined usages, `0x02` and `0x03`,
rather than descriptive ones like `Keypad` or `Dial`. Both are legal. Nothing
reads them: what identifies an element is the usage of the *data items* inside
the collection, never the label on the collection itself.

One consequence is worth knowing before it surprises somebody. macOS lists every
collection usage it finds, not only the application one, so this device appears
**three times** in `hid.enumerate()` - as usages `0x01`, `0x02` and `0x03` on
page `0xFF00` - all carrying the same path. It is one device, listed once per
collection.

### The serial number is not optional

A device is identified by vendor id, product id and **serial number**, in that
combination and nothing else. Not by its USB path: that changes with the port,
and a configuration that moved when somebody plugged the keypad into the other
side of the machine would be worse than none.

Which leaves the serial number carrying the whole weight of "this device". A
firmware that reports none gives two identical units the same identity, and then
they share one row, one name and one set of mappings: plug in the second and the
first one's configuration follows it. Nothing on the host can repair that,
because the two devices are saying the same thing about themselves.

So: **always report a serial number, and make it unique per unit.** Anything
stable and per-device does - a MAC address, the chip's own unique id, a number
burned in at production. anydeck will not offer to take on a device that reports
no vendor or product id at all, for the same reason; a missing serial is accepted
today, and the cost of it is described above.

## Report 0x01 — input, device to PC, 4 bytes

Sent only when something changed.

| Byte | Bits | Meaning |
|---|---|---|
| 0 | 7..0 | report id, always `0x01` |
| 1 | 0..5 | keys SW1..SW6, one bit each, `1` = pressed |
| 1 | 6..7 | padding, always `0` |
| 2 | 0 | encoder push button, `1` = pressed |
| 2 | 1..7 | padding, always `0` |
| 3 | 7..0 | encoder movement since the previous report, signed, `+` = clockwise |

Byte 3 is a **difference, not a position**. The firmware accumulates detents
between reports and clears the accumulator once a report has been sent, so no
movement is lost when several detents happen between two reports. A report is
never sent for a difference of zero with no button change.

## Report 0x10 — capability block, feature, read by the PC

63 bytes. Answers what the report descriptor structurally cannot say — today
that is the display. Read once after connecting.

The payload is a list of entries. Each entry is a type byte, a length byte, and
that many payload bytes. Unknown types are skipped using the length, so the
firmware can add entries later without breaking an older anydeck, and an older
device stays readable by a newer anydeck.

| Type | Name | Payload length |
|---|---|---|
| `0x00` | end of list | 0 |
| `0x01` | device info | 2 |
| `0x02` | display | 5 to 13 |

### Mandatory versus default

Within an entry the **mandatory fields come first**, then the optional ones in a
fixed order. The length byte says how far the sender got: everything the length
does not cover takes its documented default.

That makes a minimal device cheap to implement — a plain display sends 5 bytes
and is done — while a device with more to say simply sends a longer entry. No
flags, no second format, and an old anydeck reading a longer entry still gets
the fields it knows.

A default is only sound where the fallback is genuinely the common case. Pixel
count and colour format have no sensible fallback, so they are mandatory.
Rounded corners do: a display without stated rounding is a rectangular one.

### `0x01` device info

| Byte | Meaning |
|---|---|
| 0 | protocol version, currently `1` |
| 1 | number of display entries that follow |

### `0x02` display

Mandatory, always present:

| Byte | Meaning | Value on this device |
|---|---|---|
| 0..1 | width in pixels, little endian | `280` |
| 2..3 | height in pixels, little endian | `240` |
| 4 | pixel format: `1` = RGB565, little endian | `1` |

Optional, in this order. A shorter entry stops here and the rest is the default:

| Byte | Meaning | Default when absent | Value on this device |
|---|---|---|---|
| 5 | display index | position among the display entries | `0` |
| 6 | rotation already applied by the firmware, in 90° steps | `0`, not rotated | `3` |
| 7 | corner radius in pixels | `0`, square corners | `14` |
| 8..9 | physical width in tenths of a millimetre | `0`, unknown | `326` |
| 10..11 | physical height in tenths of a millimetre | `0`, unknown | `279` |
| 12 | capability flags, see below | `0`, full frames only | `0b0000_0011` |

Capability flags:

| Bit | Meaning | Default |
|---|---|---|
| 0 | backlight brightness can be set | not settable |
| 1 | accepts a full frame | see note |
| 2 | accepts a partial rectangle | not accepted |
| 3..7 | reserved | — |

A flags byte of `0` would say the display accepts nothing at all, which is not a
useful reading of "the sender said nothing". So when byte 12 is absent, bit 1 is
taken to be set: a display accepts full frames unless it says otherwise. When
byte 12 *is* present, it is read literally.

This device sends the full 13 bytes.

Width and height are what the PC can address, i.e. **after** the rotation in
byte 6 has been applied. Byte 6 is informational, so the interface can show the
display the right way round in the visual editor; the PC never has to rotate
pixel data itself.

The corner radius is a real constraint, not a hint: the outermost pixels of the
rectangle are behind rounded glass and are not all visible.

## Report 0x20 — display data, output, PC to device

63 bytes per packet, reserved. The framing for pixel data and commands is not
specified yet; specifying it is part of implementing the display feed.

## Open points

- The corner radius of `14` is an estimate carried over from the firmware's safe
  inset. It should be measured on the real panel.
- Whether one USB packet can carry a whole 280x240 frame is a question for the
  display feed, not for this document: at RGB565 a frame is 134400 bytes, so it
  will need chunking either way.

## Companion document

The full explanation of HID, why the device is vendor-defined at the top level,
and a proposed detection algorithm for anydeck: `Keypad HID Protocol` artifact.
