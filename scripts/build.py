#!/usr/bin/env python3
"""Build the firmware and store the artifacts under build/<version>/.

Asks for a version number, compiles the PlatformIO project and collects the
flashable artifacts. An existing build/<version>/ directory is replaced.
Afterwards it offers to publish the build as a release (release/<version>/,
which - unlike build/ - is tracked in git).

Usage:
    python3 scripts/build.py                 # fully interactive
    python3 scripts/build.py -v v1.0.0       # skip the version prompt
    python3 scripts/build.py -v v1.0.0 -r    # ... and publish a release
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

import _common as common

# Kept in build/ for local debugging (exception decoding), but left out of a
# release: the ELF is several megabytes and release/ is tracked in git.
EXTRA_ARTIFACTS = ("firmware.elf",)


def build(version: str | None = None,
          environment: str | None = None,
          release: bool | None = None) -> dict:
    """Compile the firmware. Returns info about the produced artifacts."""
    environment = environment or common.default_environment()
    version = version or common.prompt_version()
    if not common.is_valid_version(version):
        common.fail(f"Invalid version: {version}")

    common.info(f"Building {version} (environment: {environment})")
    common.run([
        common.pio_executable(), "run",
        "--project-dir", str(common.PROJECT_DIR),
        "--environment", environment,
    ])

    source_dir = common.PROJECT_DIR / ".pio" / "build" / environment
    target_dir = common.BUILD_DIR / version
    if target_dir.exists():
        common.info(f"Replacing existing build directory {target_dir.relative_to(common.PROJECT_DIR)}")
        shutil.rmtree(target_dir)
    target_dir.mkdir(parents=True)

    for name, _offset in common.FLASH_LAYOUT:
        if name == "boot_app0.bin":
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
    common.write_manifest(target_dir, version, environment)
    common.info(f"Artifacts written to {target_dir.relative_to(common.PROJECT_DIR)}")

    if release is None:
        release = common.confirm(f"Publish {version} as a release?", default=False)
    release_dir = None
    if release:
        release_dir = common.RELEASE_DIR / version
        common.replace_directory(target_dir, release_dir, skip=EXTRA_ARTIFACTS)
        common.info(f"Release published to {release_dir.relative_to(common.PROJECT_DIR)}")

    return {
        "version": version,
        "environment": environment,
        "directory": target_dir,
        "image": image,
        "release_directory": release_dir,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the keypad firmware.")
    parser.add_argument("-v", "--version", help="version to build, e.g. v1.0.0")
    parser.add_argument("-e", "--environment", help="PlatformIO environment to build")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("-r", "--release", action="store_true", default=None,
                       help="publish the build as a release without asking")
    group.add_argument("-R", "--no-release", dest="release", action="store_false",
                       help="skip the release prompt")
    args = parser.parse_args()

    build(version=args.version, environment=args.environment, release=args.release)


if __name__ == "__main__":
    main()
