#!/usr/bin/env python3
"""Flash a published firmware release onto a connected board.

Releases live next to this script as release/<version>/ and are created by
scripts/build.py. Without arguments the newest release is installed.

Usage:
    python3 release/install.py                 # newest release
    python3 release/install.py v1.0.0          # a specific release
    python3 release/install.py --list          # show available releases
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parents[1] / "scripts"
if not (SCRIPTS_DIR / "_common.py").exists():
    sys.exit(f"error: helper module not found, expected it at {SCRIPTS_DIR / '_common.py'}")
sys.path.insert(0, str(SCRIPTS_DIR))

import _common as common  # noqa: E402


def main() -> None:
    parser = argparse.ArgumentParser(description="Install a firmware release.")
    parser.add_argument("version", nargs="?", help="release to install (default: newest)")
    parser.add_argument("-l", "--list", action="store_true", help="list available releases and exit")
    parser.add_argument("-p", "--port", help="serial port to flash, skips the device prompt")
    parser.add_argument("-b", "--baud", type=int, default=common.DEFAULT_UPLOAD_BAUD,
                        help=f"upload baud rate (default: {common.DEFAULT_UPLOAD_BAUD})")
    parser.add_argument("-y", "--yes", action="store_true", help="do not ask for confirmation")
    parser.add_argument("-m", "--monitor", action="store_true",
                        help="open the serial monitor after flashing")
    args = parser.parse_args()

    versions = common.list_versions(common.RELEASE_DIR)
    if not versions:
        common.fail(f"No releases found in {common.RELEASE_DIR}. Create one with scripts/build.py.")

    if args.list:
        common.info("Available releases (newest last):")
        for version in versions:
            print(f"    {version}")
        return

    version = args.version or versions[-1]
    if version not in versions:
        common.fail(f"Release {version} not found. Available: {', '.join(versions)}")

    release_dir = common.RELEASE_DIR / version
    manifest = common.read_manifest(release_dir)
    merged = manifest.get("merged_image", {})
    image = release_dir / merged.get("file", common.MERGED_IMAGE)
    offset = int(merged.get("offset", "0x0"), 16)
    if not image.exists():
        common.fail(f"Image not found: {image}")

    port = args.port or common.select_port("Select the device to flash")
    if not args.yes and not common.confirm(f"Install {version} onto {port}?", default=True):
        common.info("Aborted")
        return

    common.info(f"Installing {version} to {port}")
    common.flash_image(port, image, offset=offset, baud=args.baud)
    common.info("Installation finished")

    if args.monitor:
        # Not wait_for_port(port): the port just flashed may have been the ROM
        # bootloader's interface, and the running firmware enumerates as its own
        # USB device under a different name.
        monitor_port = common.wait_for_app_port(timeout=20.0)
        if monitor_port is None:
            common.info("Device did not re-appear, skipping the serial monitor")
            return
        common.open_monitor(monitor_port, manifest.get("environment") or common.default_environment())


if __name__ == "__main__":
    main()
