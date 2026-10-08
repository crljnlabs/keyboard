#!/usr/bin/env python3
"""Compile the firmware and store the result under build/<build id>/.

Asks nothing. A build is not a release: it has no version number, only a
timestamp id, and it lives in build/ which is git-ignored. Choosing a version
and publishing is deploy.py's job.

Usage:
    python3 scripts/build.py
    python3 scripts/build.py -e supermini
"""

from __future__ import annotations

import argparse
import shutil

import _common as common

# Kept in build/ so a crash can be decoded, and left out of a release by
# deploy.py: the ELF is several megabytes and release/ is tracked in git.
EXTRA_ARTIFACTS = ("firmware.elf",)


def build(environment: str | None = None) -> dict:
    """Compile the firmware. Returns where the artifacts went."""
    environment = environment or common.default_environment()
    build_id = common.new_build_id()

    common.info(f"Building {build_id} (environment: {environment})")
    common.run([
        common.pio_executable(), "run",
        "--project-dir", str(common.PROJECT_DIR),
        "--environment", environment,
    ])

    source_dir = common.PROJECT_DIR / ".pio" / "build" / environment
    target_dir = common.BUILD_DIR / build_id
    target_dir.mkdir(parents=True, exist_ok=True)

    for name, _offset in common.FLASH_LAYOUT:
        if name == "boot_app0.bin":
            # Comes from the framework, not from our compilation.
            shutil.copy2(common.boot_app0_path(), target_dir / name)
            continue
        artifact = source_dir / name
        if not artifact.exists():
            common.fail(f"Missing build artifact: {artifact}")
        shutil.copy2(artifact, target_dir / name)

    for name in EXTRA_ARTIFACTS:
        artifact = source_dir / name
        if artifact.exists():
            shutil.copy2(artifact, target_dir / name)

    image = common.merge_artifacts(target_dir)
    common.write_manifest(target_dir, build_id, environment)
    common.info(f"Artifacts written to {target_dir.relative_to(common.PROJECT_DIR)}")

    return {
        "build_id": build_id,
        "environment": environment,
        "directory": target_dir,
        "image": image,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Compile the keypad firmware.")
    parser.add_argument("-e", "--environment", help="PlatformIO environment to build")
    build(environment=parser.parse_args().environment)


if __name__ == "__main__":
    main()
