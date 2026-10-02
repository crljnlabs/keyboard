# keyboard

The crljn keypad: six keys, a rotary encoder with a push button and a 1.69 inch
display, on an ESP32-S3 Super Mini. This repository holds its firmware and its
circuit board.

Over USB the keypad is a vendor-defined HID device, so no operating system
treats it as a keyboard and nothing it sends ever types into a window. What a
key does is up to the program on the PC,
[anydeck](https://github.com/crljnlabs/anydeck), whose device contract
(`docs/device-protocol.md` there) this firmware keeps to.

- What the keypad says over USB, byte for byte: [docs/hid.md](docs/hid.md)

## What the firmware does

- Reads the six keys and the encoder, and reports them to the PC.
- Tells the PC what it is made of, the display included.
- Shows a status screen of its own: the encoder's position and last direction,
  and which keys are held.
- Logs over the same USB cable, as a serial port.

Not yet: showing on the display what the PC sends.

## Building and flashing

Needs Python 3 and PlatformIO (`pio`). Everything else - the toolchain, the
libraries, esptool - PlatformIO fetches itself on the first build.

```bash
python3 scripts/flash.py
```

The everyday command: compiles, flashes the board and opens the serial monitor.
It remembers the port that worked and only asks again when that one is gone.
`-p <port>` picks a port once, `--no-monitor` leaves the monitor closed.

```bash
python3 scripts/build.py
```

Compiles only, into `build/<timestamp>/`, which is not tracked.

```bash
python3 scripts/deploy.py
```

Publishes a build that already exists as a version, `release/<version>/`, which
is tracked. It never compiles, so the bytes that were tested are the bytes that
get published. It suggests the newest build and the next minor version.

```bash
python3 release/install.py
```

Flashes a published version, the newest unless one is named; `--list` shows
them. Nothing is published yet.

## Technology

- C++ with PlatformIO and the Arduino framework, for the ESP32-S3 Super Mini
  (module ESP32-S3FH4R2: 4 MB flash, 2 MB PSRAM). The platform is pinned,
  `espressif32@6.12.0` with Arduino core 2.0.17, so a build is reproducible.
- USB through TinyUSB (`ARDUINO_USB_MODE=0`), which a vendor-defined HID
  interface needs; the serial port sits next to it on the same device.
- The display, an ST7789 panel, through TFT_eSPI, configured only through build
  flags in `platformio.ini`.

## Where things are

| | |
|---|---|
| `src/` | the firmware |
| `include/pins.h` | every GPIO the firmware touches, and why the pins do not match the schematic's net names |
| `platformio.ini` | the board, the USB setup and the display configuration |
| `docs/` | the USB protocol |
| `kicad/` | schematic, board, 3D model, gerbers and an interactive BOM |
| `scripts/` | building, flashing and publishing |
| `release/` | published versions, and the script that installs them |
