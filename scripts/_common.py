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


def prompt_version() -> str:
    while True:
        version = ask("Version to build (e.g. v1.0.0)")
        if is_valid_version(version):
            return version
        print("Invalid version. Expected format: v<major>.<minor>.<patch>[-suffix]")


def version_sort_key(version: str) -> tuple:
    match = VERSION_RE.match(version)
    if not match:
        # Unknown naming: sort before every valid version.
        return (-1, -1, -1, 0, version)
    numbers = [int(part) for part in version[1:].split("-", 1)[0].split(".")]
    suffix = version.split("-", 1)[1] if "-" in version else ""
    # A pre-release (with suffix) sorts before the final release.
    return (numbers[0], numbers[1], numbers[2], 0 if suffix else 1, suffix)


def list_versions(directory: Path) -> list[str]:
    if not directory.is_dir():
        return []
    names = [entry.name for entry in directory.iterdir() if entry.is_dir()]
    return sorted(names, key=version_sort_key)


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


def flash_image(port: str, image: Path, offset: int = 0x0, baud: int = DEFAULT_UPLOAD_BAUD) -> None:
    run(
        esptool_command()
        + [
            "--chip", CHIP,
            "--port", port,
            "--baud", str(baud),
            "write_flash", hex(offset), str(image),
        ]
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


def select_port(prompt: str = "Select device") -> str:
    ports = list_serial_ports()
    if not ports:
        fail("No USB serial devices found. Connect the board and try again.")
    print()
    info("Connected USB serial devices:")
    for index, port in enumerate(ports, start=1):
        marker = " (Espressif)" if looks_like_espressif(port) else ""
        description = f" - {port['description']}" if port["description"] else ""
        print(f"    [{index}] {port['port']}{description}{marker}")

    default_index = 1
    for index, port in enumerate(ports, start=1):
        if looks_like_espressif(port):
            default_index = index
            break

    while True:
        answer = ask(f"{prompt} [1-{len(ports)}]", str(default_index))
        if answer.isdigit() and 1 <= int(answer) <= len(ports):
            return ports[int(answer) - 1]["port"]
        print("Invalid selection.")


def wait_for_port(preferred: str, timeout: float = 10.0) -> str | None:
    """Wait for a port to (re-)appear after a reset, e.g. native USB re-enumeration."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ports = list_serial_ports()
        names = [port["port"] for port in ports]
        if preferred in names:
            return preferred
        espressif = [port["port"] for port in ports if looks_like_espressif(port)]
        if espressif:
            return espressif[0]
        time.sleep(0.5)
    return None


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


def write_manifest(directory: Path, version: str, environment: str) -> Path:
    manifest = {
        "version": version,
        "environment": environment,
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
