"""Shared helpers for the firmware build / flash / install scripts.

Everything in here is tooling only - no firmware logic.
"""

from __future__ import annotations

import configparser
import json
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

# --- project layout ---------------------------------------------------------

PROJECT_DIR = Path(__file__).resolve().parents[1]
PLATFORMIO_INI = PROJECT_DIR / "platformio.ini"
BUILD_DIR = PROJECT_DIR / "build"       # git-ignored scratch output
RELEASE_DIR = PROJECT_DIR / "release"   # tracked in git

CHIP = "esp32s3"

# Flash layout of an Arduino-ESP32 image. The offsets are fixed by the ROM
# bootloader (0x0) and by the Arduino partition scheme (partition table at
# 0x8000, OTA data at 0xe000, first app slot at 0x10000).
FLASH_LAYOUT = (
    ("bootloader.bin", 0x0000),
    ("partitions.bin", 0x8000),
    ("boot_app0.bin", 0xE000),
    ("firmware.bin", 0x10000),
)
MERGED_IMAGE = "firmware-merged.bin"
MANIFEST_NAME = "manifest.json"

VERSION_RE = re.compile(r"^v\d+\.\d+\.\d+(?:-[0-9A-Za-z.]+)?$")

# Builds are named by the moment they were produced. A version number is not
# available at build time - it is chosen when a build is deployed - and a
# timestamp both sorts chronologically as plain text and never collides.
BUILD_ID_RE = re.compile(r"^\d{8}-\d{6}$")

# Remembers the port that last worked, so flashing does not ask every time.
# Git-ignored: it describes this machine, not the project.
PORT_MEMORY = PROJECT_DIR / ".flash-port"

DEFAULT_UPLOAD_BAUD = 921600


# --- console helpers --------------------------------------------------------


def info(message: str) -> None:
    print(f"==> {message}")


def fail(message: str) -> "None":
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def ask(question: str, default: str = "") -> str:
    suffix = f" [{default}]" if default else ""
    try:
        answer = input(f"{question}{suffix}: ").strip()
    except EOFError:
        answer = ""
    return answer or default


def confirm(question: str, default: bool = False) -> bool:
    suffix = "[Y/n]" if default else "[y/N]"
    while True:
        try:
            answer = input(f"{question} {suffix}: ").strip().lower()
        except EOFError:
            return default
        if not answer:
            return default
        if answer in ("y", "yes"):
            return True
        if answer in ("n", "no"):
            return False
        print("Please answer with 'y' or 'n'.")


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess:
    print(f"    $ {' '.join(str(part) for part in command)}")
    return subprocess.run(command, check=True, **kwargs)


# --- versions ---------------------------------------------------------------


def is_valid_version(version: str) -> bool:
    return bool(VERSION_RE.match(version))



def version_sort_key(version: str) -> tuple:
    match = VERSION_RE.match(version)
    if not match:
        # Unknown naming: sort before every valid version.
        return (-1, -1, -1, 0, version)
    numbers = [int(part) for part in version[1:].split("-", 1)[0].split(".")]
    suffix = version.split("-", 1)[1] if "-" in version else ""
    # A pre-release (with suffix) sorts before the final release.
    return (numbers[0], numbers[1], numbers[2], 0 if suffix else 1, suffix)


def next_version(previous: str | None) -> str:
    """The version to suggest after `previous`: one minor step up.

    A minor bump rather than a patch bump because that is what these releases
    are - the firmware gains a capability at a time. Patch numbers stay
    available for typing in by hand.
    """
    if not previous or not is_valid_version(previous):
        return "v0.1.0"
    major, minor, _patch = (int(part) for part in previous[1:].split("-", 1)[0].split("."))
    return f"v{major}.{minor + 1}.0"


def list_versions(directory: Path) -> list[str]:
    if not directory.is_dir():
        return []
    names = [entry.name for entry in directory.iterdir() if entry.is_dir()]
    return sorted(names, key=version_sort_key)


def new_build_id() -> str:
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def list_builds() -> list[str]:
    """Build ids present in build/, oldest first."""
    if not BUILD_DIR.is_dir():
        return []
    return sorted(entry.name for entry in BUILD_DIR.iterdir()
                  if entry.is_dir() and BUILD_ID_RE.match(entry.name))


def latest_build() -> str | None:
    builds = list_builds()
    return builds[-1] if builds else None


def describe_build(build_id: str) -> str:
    """One line about a build, for pickers: its id plus what the manifest says."""
    manifest = read_manifest(BUILD_DIR / build_id)
    parts = [build_id]
    if manifest.get("environment"):
        parts.append(manifest["environment"])
    if manifest.get("commit"):
        parts.append(manifest["commit"])
    return "  ".join(parts)


# --- PlatformIO -------------------------------------------------------------


def pio_executable() -> str:
    for candidate in ("pio", "platformio"):
        found = shutil.which(candidate)
        if found:
            return found
    for candidate in (
        Path.home() / ".platformio" / "penv" / "bin" / "pio",
        Path.home() / ".local" / "bin" / "pio",
    ):
        if candidate.exists():
            return str(candidate)
    fail("PlatformIO Core not found. Install it: pip install platformio")
    raise AssertionError("unreachable")


def default_environment() -> str:
    parser = configparser.ConfigParser()
    parser.read(PLATFORMIO_INI)
    if parser.has_option("platformio", "default_envs"):
        return parser.get("platformio", "default_envs").split()[0].strip()
    for section in parser.sections():
        if section.startswith("env:"):
            return section[len("env:"):]
    fail(f"No build environment found in {PLATFORMIO_INI}")
    raise AssertionError("unreachable")


def pio_core_dir() -> Path:
    try:
        output = subprocess.run(
            [pio_executable(), "system", "info", "--json-output"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        return Path(json.loads(output)["core_dir"]["value"])
    except (subprocess.CalledProcessError, KeyError, ValueError):
        return Path.home() / ".platformio"


def boot_app0_path() -> Path:
    packages = pio_core_dir() / "packages"
    for framework in sorted(packages.glob("framework-arduinoespressif32*")):
        candidate = framework / "tools" / "partitions" / "boot_app0.bin"
        if candidate.exists():
            return candidate
    fail("boot_app0.bin not found - build the project once so PlatformIO installs the framework")
    raise AssertionError("unreachable")


# --- esptool ----------------------------------------------------------------


def esptool_command() -> list[str]:
    """Return the command prefix used to invoke esptool."""
    core_dir = pio_core_dir()
    bundled = core_dir / "packages" / "tool-esptoolpy" / "esptool.py"
    if bundled.exists():
        python = core_dir / "penv" / ("Scripts" if os.name == "nt" else "bin")
        python = python / ("python.exe" if os.name == "nt" else "python")
        if python.exists():
            return [str(python), str(bundled)]
        return [sys.executable, str(bundled)]
    for candidate in ("esptool.py", "esptool"):
        found = shutil.which(candidate)
        if found:
            return [found]
    return [sys.executable, "-m", "esptool"]


def _esptool_write(port: str, image: Path, offset: int, baud: int, before: str = "") -> None:
    command = esptool_command() + ["--chip", CHIP, "--port", port, "--baud", str(baud)]
    if before:
        command += ["--before", before]
    command += ["write_flash", hex(offset), str(image)]
    run(command)


# Opening the app's CDC port at 1200 baud is the standard way to put a
# native-USB ESP32 into download mode without touching a button: Arduino's
# USBCDC::_onLineCoding() reacts to that baud rate with
# usb_persist_restart(RESTART_BOOTLOADER), and on the ESP32-S3 that switches the
# USB peripheral over to the ROM's USB-Serial-JTAG interface. It is the same
# mechanism the Arduino IDE uses to upload.
_TOUCH_SCRIPT = (
    "import sys, serial\n"
    "try:\n"
    "    s = serial.Serial(sys.argv[1], 1200)\n"
    "except Exception:\n"
    "    sys.exit(2)\n"
    "try:\n"
    "    s.dtr = False\n"
    "    s.close()\n"
    "except Exception:\n"
    "    pass\n"
)


def _penv_python() -> str:
    """Path to PlatformIO's bundled interpreter, which always has pyserial."""
    base = pio_core_dir() / "penv" / ("Scripts" if os.name == "nt" else "bin")
    candidate = base / ("python.exe" if os.name == "nt" else "python")
    return str(candidate) if candidate.exists() else ""


def _touch_1200_baud(port: str) -> None:
    """Set the port to 1200 baud, asking a running app to reboot into download mode."""
    python = _penv_python()
    if python:
        command = [python, "-c", _TOUCH_SCRIPT, port]
    else:
        # No pyserial reachable: stty can set the line coding just as well.
        flag = "-f" if sys.platform == "darwin" else "-F"
        command = ["stty", flag, port, "1200"]
    try:
        subprocess.run(command, check=False, capture_output=True, timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        pass


def request_download_mode(timeout: float = 25.0) -> str | None:
    """Get the board into download mode without any button presses.

    Returns the port of the ROM's USB-Serial-JTAG interface, or None on timeout.

    The retry loop matters for a board whose app is crashing: its CDC port only
    exists for the fraction of a second between one boot and the next crash, so
    the 1200 baud touch has to be attempted repeatedly until one lands. Once it
    does, the chip stays in download mode until the next reset.
    """
    deadline = time.monotonic() + timeout
    announced = False
    while time.monotonic() < deadline:
        ports = list_serial_ports()

        for port in ports:
            if looks_like_bootloader(port):
                return port["port"]

        candidates = [p["port"] for p in ports if looks_like_espressif(p)]
        if candidates and not announced:
            info("Asking the board to enter download mode (1200 baud touch)")
            announced = True
        for candidate in candidates:
            _touch_1200_baud(candidate)

        time.sleep(0.4)
    return None


def flash_image(port: str, image: Path, offset: int = 0x0, baud: int = DEFAULT_UPLOAD_BAUD) -> None:
    """Write the merged image, surviving a USB re-enumeration mid-flash.

    The board has no USB-to-UART bridge: the serial port is the ESP32-S3's own
    USB peripheral. When esptool resets the chip into download mode, the running
    app's TinyUSB CDC interface is replaced by the ROM's USB-Serial-JTAG
    interface. VID and PID change, so the operating system tears down the device
    node and esptool's open file descriptor dies part-way through the reset
    sequence - on macOS as "OSError: [Errno 6] Device not configured".

    The chip is then in download mode, but usually under a *different* port
    name, which is why retrying the same path cannot work and re-discovering the
    port can. The attempts below escalate: same port, re-discovered port, and
    finally re-discovered port without another reset, for the case where the
    chip already sits in the ROM bootloader.
    """
    ports = {p["port"]: p for p in list_serial_ports()}

    # Already in download mode: flash it straight away and do not reset it out
    # of the state we need.
    if port in ports and looks_like_bootloader(ports[port]):
        _esptool_write(port, image, offset, baud, before="no_reset")
        return

    try:
        _esptool_write(port, image, offset, baud)
        return
    except subprocess.CalledProcessError:
        pass

    info("Direct flashing failed - the app is most likely crashing and tearing its USB port down")
    boot_port = request_download_mode()
    if boot_port is None:
        fail(
            "Could not get the board into download mode. Do it by hand: hold "
            "BOOT, tap RESET, release BOOT, then run this script again."
        )
        raise AssertionError("unreachable")

    info(f"Board is in download mode on {boot_port}")
    try:
        _esptool_write(boot_port, image, offset, baud, before="no_reset")
        return
    except subprocess.CalledProcessError:
        fail(
            f"The board is in download mode on {boot_port} but flashing still "
            "failed. Run the script again and pick that port directly."
        )


# --- serial ports -----------------------------------------------------------

ESPRESSIF_VID = "303A"


def list_serial_ports() -> list[dict]:
    """List connected serial devices as {port, description, hwid} dicts."""
    ports = _ports_via_pyserial() or _ports_via_platformio() or _ports_via_glob()
    return ports


def _ports_via_pyserial() -> list[dict]:
    try:
        from serial.tools import list_ports  # type: ignore
    except ImportError:
        return []
    return [
        {"port": p.device, "description": p.description or "", "hwid": p.hwid or ""}
        for p in list_ports.comports()
    ]


def _ports_via_platformio() -> list[dict]:
    try:
        output = subprocess.run(
            [pio_executable(), "device", "list", "--json-output"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        return [
            {
                "port": entry.get("port", ""),
                "description": entry.get("description", ""),
                "hwid": entry.get("hwid", ""),
            }
            for entry in json.loads(output)
        ]
    except (subprocess.CalledProcessError, ValueError, SystemExit):
        return []


def _ports_via_glob() -> list[dict]:
    patterns = ("/dev/tty.usb*", "/dev/cu.usb*", "/dev/ttyUSB*", "/dev/ttyACM*")
    found: list[dict] = []
    for pattern in patterns:
        directory, _, glob = pattern.rpartition("/")
        for path in sorted(Path(directory).glob(glob)):
            found.append({"port": str(path), "description": "", "hwid": ""})
    return found


def looks_like_espressif(port: dict) -> bool:
    return ESPRESSIF_VID in port.get("hwid", "").upper()


def looks_like_bootloader(port: dict) -> bool:
    """True for the ROM's USB-Serial-JTAG interface, i.e. the board is in download mode."""
    return "JTAG" in port.get("description", "").upper()


def select_port(prompt: str = "Select device") -> str:
    ports = list_serial_ports()
    if not ports:
        fail("No USB serial devices found. Connect the board and try again.")
    print()
    info("Connected USB serial devices:")
    for index, port in enumerate(ports, start=1):
        if looks_like_bootloader(port):
            marker = " (Espressif, download mode - pick this one to flash)"
        elif looks_like_espressif(port):
            marker = " (Espressif)"
        else:
            marker = ""
        description = f" - {port['description']}" if port["description"] else ""
        print(f"    [{index}] {port['port']}{description}{marker}")

    # A board in download mode exposes the ROM's USB-Serial-JTAG interface,
    # which is the only port that stays put while flashing. A boot-looping app
    # keeps tearing its own CDC port down, so prefer the bootloader whenever it
    # is present.
    default_index = 1
    for index, port in enumerate(ports, start=1):
        if looks_like_bootloader(port):
            default_index = index
            break
    else:
        for index, port in enumerate(ports, start=1):
            if looks_like_espressif(port):
                default_index = index
                break

    while True:
        answer = ask(f"{prompt} [1-{len(ports)}]", str(default_index))
        if answer.isdigit() and 1 <= int(answer) <= len(ports):
            return ports[int(answer) - 1]["port"]
        print("Invalid selection.")


def wait_for_port(preferred: str, timeout: float = 10.0, exclude: str = "") -> str | None:
    """Wait for a port to (re-)appear after a reset, e.g. native USB re-enumeration.

    `exclude` names a port that must not be returned. Pass the port that was
    just flashed: that one is the ROM bootloader's USB-Serial-JTAG interface,
    while the running app enumerates as a separate USB device under a different
    name. Without this, the caller would reattach to the bootloader and see
    nothing.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ports = [p for p in list_serial_ports() if p["port"] != exclude]
        names = [port["port"] for port in ports]
        if preferred in names:
            return preferred
        espressif = [port["port"] for port in ports if looks_like_espressif(port)]
        if espressif:
            return espressif[0]
        time.sleep(0.5)
    return None


def wait_for_app_port(timeout: float = 20.0) -> str | None:
    """Wait for the running app's own USB CDC port to show up after a flash.

    Skips the ROM's USB-Serial-JTAG interface: that one belongs to the
    bootloader, so attaching a monitor to it would show nothing. A crashing app
    republishes its port on every boot, so the first sighting wins.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for port in list_serial_ports():
            if looks_like_espressif(port) and not looks_like_bootloader(port):
                return port["port"]
        time.sleep(0.4)
    return None


def remembered_port() -> str | None:
    """The port that last flashed successfully, if it is still connected.

    Returning None for a port that has gone away is the point: a remembered name
    is only useful if it still exists, and re-asking is better than failing
    against a stale one.
    """
    if not PORT_MEMORY.exists():
        return None
    saved = PORT_MEMORY.read_text().strip()
    if not saved:
        return None
    if saved in (port["port"] for port in list_serial_ports()):
        return saved
    return None


def remember_port(port: str) -> None:
    PORT_MEMORY.write_text(port + "\n")


def open_monitor(port: str, environment: str) -> None:
    info(f"Opening serial monitor on {port} (Ctrl+C to exit)")
    try:
        subprocess.run(
            [
                pio_executable(), "device", "monitor",
                "--project-dir", str(PROJECT_DIR),
                "--environment", environment,
                "--port", port,
            ],
            check=False,
        )
    except KeyboardInterrupt:
        pass


# --- artifacts --------------------------------------------------------------


def merge_artifacts(directory: Path) -> Path:
    """Concatenate the flash parts into a single image that is written at 0x0."""
    parts = []
    for name, offset in FLASH_LAYOUT:
        path = directory / name
        if not path.exists():
            fail(f"Missing build artifact: {path}")
        parts.append((offset, path.read_bytes()))

    size = max(offset + len(data) for offset, data in parts)
    image = bytearray(b"\xff" * size)
    for offset, data in parts:
        image[offset:offset + len(data)] = data

    merged = directory / MERGED_IMAGE
    merged.write_bytes(bytes(image))
    return merged


def git_commit() -> str:
    """Short commit the build came from, or "" outside a git checkout."""
    try:
        result = subprocess.run(
            ["git", "-C", str(PROJECT_DIR), "rev-parse", "--short", "HEAD"],
            check=True, capture_output=True, text=True,
        )
    except (subprocess.CalledProcessError, OSError):
        return ""
    dirty = subprocess.run(
        ["git", "-C", str(PROJECT_DIR), "status", "--porcelain"],
        check=False, capture_output=True, text=True,
    ).stdout.strip()
    return result.stdout.strip() + ("-dirty" if dirty else "")


def write_manifest(directory: Path, build_id: str, environment: str,
                   version: str | None = None) -> Path:
    manifest = {
        "build_id": build_id,
        "version": version,
        "environment": environment,
        "commit": git_commit(),
        "chip": CHIP,
        "built_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "merged_image": {"file": MERGED_IMAGE, "offset": "0x0"},
        "parts": [{"file": name, "offset": hex(offset)} for name, offset in FLASH_LAYOUT],
    }
    path = directory / MANIFEST_NAME
    path.write_text(json.dumps(manifest, indent=2) + "\n")
    return path


def read_manifest(directory: Path) -> dict:
    path = directory / MANIFEST_NAME
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text())
    except ValueError:
        return {}


def replace_directory(source: Path, target: Path, skip: tuple[str, ...] = ()) -> None:
    """Copy source over target, replacing whatever was there before."""
    if target.exists():
        shutil.rmtree(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, target, ignore=shutil.ignore_patterns(*skip) if skip else None)
