#!/usr/bin/env python3
"""Build the firmware and flash it onto the connected board.

The everyday command: no questions in the normal case. It compiles first, then
flashes the build it just produced onto the port that worked last time. The port
is remembered in .flash-port and only asked for again when the remembered one is
no longer connected - so with the board plugged in, this just runs.

Publishing a version is deploy.py's job; this script never touches release/.

Usage:
    python3 scripts/flash.py
    python3 scripts/flash.py -p /dev/cu.usbmodem1101   # override the port once
    python3 scripts/flash.py --no-monitor
"""

from __future__ import annotations

import argparse

import _common as common
import build as build_script


def select_flash_port(override: str | None) -> str:
    """The port to flash: the override, the remembered one, or ask."""
    if override:
        return override

    remembered = common.remembered_port()
    if remembered:
        common.info(f"Using remembered port {remembered}")
        return remembered

    if common.PORT_MEMORY.exists():
        common.info("The remembered port is not connected any more")
    return common.select_port("Select the device to flash")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build and flash the keypad firmware.")
    parser.add_argument("-e", "--environment", help="PlatformIO environment to build")
    parser.add_argument("-p", "--port", help="serial port to flash, skips the remembered one")
    parser.add_argument("-b", "--baud", type=int, default=common.DEFAULT_UPLOAD_BAUD,
                        help=f"upload baud rate (default: {common.DEFAULT_UPLOAD_BAUD})")
    parser.add_argument("--no-monitor", dest="monitor", action="store_false",
                        help="do not open the serial monitor after flashing")
    args = parser.parse_args()

    result = build_script.build(environment=args.environment)

    port = select_flash_port(args.port)
    common.info(f"Flashing {result['build_id']} to {port}")
    common.flash_image(port, result["image"], offset=0x0, baud=args.baud)
    common.remember_port(port)
    common.info("Flashing finished")

    if not args.monitor:
        return

    # The port just flashed may have been the ROM bootloader's USB-Serial-JTAG
    # interface, while the running app enumerates as a separate USB device under
    # a different name - so wait for the app's port instead of reusing this one.
    monitor_port = common.wait_for_app_port(timeout=20.0)
    if monitor_port is None:
        common.info("The app's serial port did not appear - replug the USB cable and run:")
        common.info(f"    pio device monitor -d {common.PROJECT_DIR} -e {result['environment']}")
        return
    common.open_monitor(monitor_port, result["environment"])


if __name__ == "__main__":
    main()
