#!/usr/bin/env python3
"""Publish an existing build as a versioned release under release/<version>/.

Deploying never compiles. It takes a build that already exists in build/, gives
it a version number and copies it to release/, which - unlike build/ - is
tracked in git. Keeping the two apart means the bytes that were tested are the
bytes that get published, instead of a fresh compilation that merely came from
the same source.

Both questions have a default worth accepting: the newest build, and one minor
step up from the newest release.

Usage:
    python3 scripts/deploy.py                            # fully interactive
    python3 scripts/deploy.py -v v0.2.0                  # skip the version prompt
    python3 scripts/deploy.py -v v0.2.0 -b 20260823-021405  # ... and the build prompt
"""

from __future__ import annotations

import argparse

import _common as common

# Left out of a release: the ELF is several megabytes and release/ is in git.
SKIP_IN_RELEASE = ("firmware.elf",)


def select_build(override: str | None) -> str:
    builds = common.list_builds()
    if not builds:
        common.fail("No builds found. Run scripts/build.py first.")

    if override:
        if override not in builds:
            common.fail(f"Unknown build: {override}")
        return override

    default = builds[-1]
    print()
    common.info("Available builds:")
    for index, build_id in enumerate(builds, start=1):
        marker = "  <- newest" if build_id == default else ""
        print(f"    [{index}] {common.describe_build(build_id)}{marker}")

    while True:
        answer = common.ask(f"Select the build to deploy [1-{len(builds)}]", str(len(builds)))
        if answer.isdigit() and 1 <= int(answer) <= len(builds):
            return builds[int(answer) - 1]
        if answer in builds:
            return answer
        print("Invalid selection.")


def select_version(override: str | None) -> str:
    if override:
        if not common.is_valid_version(override):
            common.fail(f"Invalid version: {override}")
        return override

    published = common.list_versions(common.RELEASE_DIR)
    latest = published[-1] if published else None
    suggestion = common.next_version(latest)

    print()
    if latest:
        common.info(f"Latest release: {latest}")
    else:
        common.info("No release published yet")

    while True:
        answer = common.ask("Version to publish", suggestion)
        if common.is_valid_version(answer):
            if (common.RELEASE_DIR / answer).exists():
                if not common.confirm(f"{answer} already exists. Replace it?", default=False):
                    continue
            return answer
        print("Invalid version. Expected format: v<major>.<minor>.<patch>[-suffix]")


def main() -> None:
    parser = argparse.ArgumentParser(description="Publish a build as a release.")
    parser.add_argument("-v", "--version", help="version to publish, e.g. v0.2.0")
    parser.add_argument("-b", "--build", help="build id to publish, e.g. 20260823-021405")
    args = parser.parse_args()

    build_id = select_build(args.build)
    version = select_version(args.version)

    source = common.BUILD_DIR / build_id
    target = common.RELEASE_DIR / version
    manifest = common.read_manifest(source)

    common.info(f"Publishing build {build_id} as {version}")
    common.replace_directory(source, target, skip=SKIP_IN_RELEASE)
    # Rewrite the manifest so the release records the version it was given while
    # keeping the build it came from - that pair is what makes a release
    # traceable back to the bytes that were tested.
    common.write_manifest(target, build_id, manifest.get("environment", ""), version=version)
    common.info(f"Release published to {target.relative_to(common.PROJECT_DIR)}")


if __name__ == "__main__":
    main()
