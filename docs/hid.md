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
| report descriptor | offered, 120 bytes |
| report `0x01`, input | sent |
| report `0x10`, capability block | answered |
| report `0x11`, fast buttons | answered and taken, kept in flash |
| report `0x20`, display data | received: pieces, places, show, brightness, own screen |

What anydeck does with each of them today is in the contract's last section.

## Identity

| | | where it comes from |
|---|---|---|
| vendor id | `0x303A` | the Arduino core's `esp32s3` variant |
| product id | `0x1001` | the Arduino core's `esp32s3` variant |
| manufacturer | `crljn.labs` | `src/main.cpp` |
| product | `crljn-board` | `src/main.cpp` |
| serial number | the chip's MAC address | the Arduino core's default |

The product and the manufacturer are set by this project. The serial number
is the chip's factory MAC address as twelve hex digits - `2884856D6EC0` on the
first unit - so every keypad has its own, which is what the contract asks for.

The vendor and product id are Espressif's generic pair, shared by many ESP32-S3
boards. That does no harm: anydeck does not pick devices by those ids, and the
serial number tells the units apart.

anydeck names a keypad after its product name when it first finds it.

## Report descriptor

120 bytes. Offered by the device at plug-in time. One vendor-defined
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

    0x85, 0x11,              //   Report ID (0x11) - fast buttons
    0x09, 0x11,              //   Usage (0x11)
    0x75, 0x08,              //   Report Size (8)
    0x95, 0x01,              //   Report Count (1)
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
collection, never from its label. The anydeck library's `src/anydeck/Layout.cpp`
has the full account.

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
report; it asks instead. A GET_REPORT for report `0x01` answers with the keys and
the button as they are now and `0` in byte 3 - asking is not turning, and
detents not yet sent still go out with the next report. anydeck asks once,
when it opens the keypad.

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
02 0D 18 01 F0 00 01 00 03 2B 46 01 17 01 1F    display, 13 bytes
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
| | 7 | corner radius in pixels | `43` |
| | 8..9 | physical width, tenths of a millimetre | `326` |
| | 10..11 | physical height, tenths of a millimetre | `279` |
| | 12 | capability flags | `0b0010_0001` |

All 13 bytes of the display entry are sent, so no default applies anywhere.

Width and height are the panel as the firmware draws on it, after rotation `3`.
The panel itself is 240 x 280 (`TFT_WIDTH` and `TFT_HEIGHT` in
`platformio.ini`), and 280 x 240 is the same panel lying on its side; the values
come from `Display::kWidth` and `Display::kHeight`.

The physical size is the 1.69 inch panel's visible area, 32.6 by 27.9 mm in this
orientation. The corner radius of `43` was measured from a photo of the panel -
all four corners came out at 42 to 44 px - and confirmed on the device, where an
arc of that radius ran along the edge of the glass (`Display::kCornerRadius`).

The flags say: the backlight can be dimmed (bit 0) and the display puts pieces
together (bit 5, report `0x20` below). Bits 1 to 4 stood for pixel rectangles,
which the display no longer takes; they are 0.

## Report 0x11 — fast buttons, feature, read and written by the PC

One byte after the report id. A bit stands for a button by its number in the
descriptor, so the contract's rule - bit n for button n + 1 - comes out as:

| Bit | Button | Element |
|---|---|---|
| 0..5 | 1..6 | keys SW1..SW6 |
| 6 | 7 | the encoder's push button |
| 7 | — | always `0` |

`1` is fast, `0` normal, and normal is what a new keypad starts with. Both wait
15 ms (`DebouncedInput`): normal until the contact has been stable that long,
fast after the first edge, which it reports at once. The encoder's turning has
no such setting: its state machine needs no waiting.

A SET_REPORT takes effect at once - a GET_REPORT straight after it answers with
what was set, bit 7 cleared - and the main loop applies it to the inputs and
stores it in flash, NVS namespace `anydeck`, key `fast-buttons`, one byte. Written only
when the PC sends it, so the flash does not wear; read at every start, so the
keypad keeps the setting after a power cut, with or without a PC.

## Report 0x20 — display data, output, PC to device

The PC draws, the device puts together. The PC sends pieces - pictures with
transparency, already drawn - and where each goes; the device keeps them and
lays them over each other. It knows nothing of what a piece shows: a moved
piece is a new place, a faded one a new opacity, a bar's value the share of its
fill to show. Every packet is one report `0x20` of 63 bytes; the first byte
says what it is (the anydeck library's `src/anydeck/DisplayFeed.h`):

| Byte 0 | Packet | Payload |
|---|---|---|
| `0x02` | data | number within the piece, from 0 (2), then up to 60 bytes |
| `0x03` | show | what changed since the last show appears at once |
| `0x04` | brightness | display (1), 0..255 |
| `0x05` | own screen | the device draws its status screen again |
| `0x06` | clear | display (1): every piece gone |
| `0x07` | piece | display, id (1 each), x, y (2 each, signed), width, height (2 each), alpha (1: `0` none, `1` one byte per pixel), length of the colours, length of the alpha (4 each) |
| `0x08` | place | display, id (1 each), x, y (2 each, signed), opacity (1, 0..255), reveal (1), amount (2, 0..65535), start, sweep (2 each, degrees, signed), centre x, centre y (2 each, from the piece's corner) |
| `0x09` | remove | display, id (1 each) |

- All numbers little endian. Colours are RGB565, PackBits on whole pixels: a
  header byte n of 0..127 is followed by n + 1 literal pixels, 128..255 by one
  pixel repeated n - 126 times. The alpha follows the colours, PackBits the
  same way on bytes. A value or a run may span two packets.
- Ids 0 to 63. Pieces are drawn in the order of their ids, 0 at the bottom; the
  PC sends the background as piece 0. A piece sent again under its id keeps its
  opacity and share and takes the new x and y. A place for an id that holds no
  piece is ignored.
- Reveal, the share that shows (amount / 65535 of it): `0` all, `1` from the
  left, `2` from the bottom, `3` from the right, `4` from the top - rounded to
  whole rows or columns, (size x amount + 32767) / 65535 of them - and `5`
  clockwise from `start` over `sweep` degrees around the centre, by the angle
  of each pixel's middle (0° points right, angles grow clockwise).
- Opacity multiplies a pixel's alpha: alpha x opacity / 255. A pixel is laid
  over what is below it per RGB565 component.
- A data packet whose number is not the next one, data that runs out before
  the pixels do, or no memory for the piece: the piece is not taken, what was
  under its id stays.
- Pieces live in PSRAM, decoded on the USB task. The main loop puts together
  the area that changed into a frame buffer of 134400 bytes at "show" and
  pushes it to the panel row by row. Memory of a replaced piece is freed by the
  main loop, once nothing draws with it. Until the first "show", and after
  "own screen", the status screen is drawn as before.
- Unknown commands are ignored.
- Arrival: hidapi writes over the interrupt OUT endpoint (64 bytes, 1 ms),
  which the core hands to `_onOutput`. A SET_REPORT over the control pipe
  arrives in `_onSetFeature`; both go to the same place.

At one packet per millisecond a full background of flat areas is well under a
second, a photo about two; a place is one packet.

## See also

- anydeck's device contract, `docs/device-protocol.md` in the anydeck
  repository: the rules this file follows, the capability block's format with
  all its defaults, and how anydeck finds and reads a device.
- The anydeck library (`../anydeck-library`), which builds all of this from
  the parts `src/main.cpp` names: `src/anydeck/Layout.cpp` the descriptor,
  `src/anydeck/Device.cpp` the reports, the capability block and report
  `0x11`, `src/anydeck/DebouncedInput.h` the two ways a button is debounced.
