#!/usr/bin/env python3
"""Build the firmware and flash it onto a connected board.

Runs scripts/build.py first (including the version prompt), then lists the
connected USB serial devices, flashes the freshly built image onto the selected
one and opens the serial monitor. The firmware exposes a USB CDC interface next
to its HID interface, so log output stays available while the device is in use.

Usage:
    python3 scripts/flash.py                       # fully interactive
    python3 scripts/flash.py -v v1.0.0 -p /dev/cu.usbmodem1101
    python3 scripts/flash.py --no-monitor          # flash only, no log window
"""

from __future__ import annotations

import argparse

import _common as common
import build as build_script


def main() -> None:
    parser = argparse.ArgumentParser(description="Build and flash the keypad firmware.")
    parser.add_argument("-v", "--version", help="version to build, e.g. v1.0.0")
    parser.add_argument("-e", "--environment", help="PlatformIO environment to build")
    parser.add_argument("-p", "--port", help="serial port to flash, skips the device prompt")
    parser.add_argument("-b", "--baud", type=int, default=common.DEFAULT_UPLOAD_BAUD,
                        help=f"upload baud rate (default: {common.DEFAULT_UPLOAD_BAUD})")
    parser.add_argument("--no-monitor", dest="monitor", action="store_false",
                        help="do not open the serial monitor after flashing")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("-r", "--release", action="store_true", default=None,
                       help="publish the build as a release without asking")
    group.add_argument("-R", "--no-release", dest="release", action="store_false",
                       help="skip the release prompt")
    args = parser.parse_args()

    result = build_script.build(
        version=args.version,
        environment=args.environment,
        release=args.release,
    )

    port = args.port or common.select_port("Select the device to flash")
    common.info(f"Flashing {result['version']} to {port}")
    common.flash_image(port, result["image"], offset=0x0, baud=args.baud)
    common.info("Flashing finished")

    if not args.monitor:
        return

    # Native USB re-enumerates after the reset, so the port may come back
    # under a different name.
    monitor_port = common.wait_for_port(port)
    if monitor_port is None:
        common.info("Device did not re-appear, skipping the serial monitor")
        return
    common.open_monitor(monitor_port, result["environment"])


if __name__ == "__main__":
    main()
