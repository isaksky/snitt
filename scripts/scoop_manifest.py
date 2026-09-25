#!/usr/bin/env python3
"""Generate and verify the versioned xshot Scoop release contract."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+")
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")


PRIVATE_NOTICE = "Private GitHub release; install and update with Install-Scoop.ps1 using your own gh authentication."


def render(version: str, digest: str, repository: str, private: bool = False) -> dict[str, Any]:
    archive = f"xshot_{version}_windows_amd64.zip"
    manifest = {
        "version": version,
        "description": "Capture, annotate, combine, and record screen regions",
        "homepage": f"https://github.com/{repository}",
        "license": "Unknown",
        "architecture": {
            "64bit": {
                "url": f"https://github.com/{repository}/releases/download/v{version}/{archive}",
                "hash": digest,
            }
        },
        "depends": "main/ffmpeg",
        "bin": "xshot.exe",
        "shortcuts": [["xshot.exe", "xshot", "--show"]],
        "pre_install": [
            "if ($global) { throw 'xshot uses a per-user desktop session. Install without --global.' }",
            "& \"$dir\\Scoop.ps1\" -Action Check -InstallDirectory $dir",
        ],
        "post_install": "& \"$dir\\Scoop.ps1\" -Action Install -InstallDirectory $dir",
        "pre_uninstall": "& \"$dir\\Scoop.ps1\" -Action Stop -InstallDirectory $dir",
        "uninstaller": {"script": "& \"$dir\\Scoop.ps1\" -Action Uninstall -InstallDirectory $dir"},
        "notes": [
            "xshot starts at login. Press Ctrl+Print Screen, then drag a region. Press V in the picker to record video.",
            "Before updating: finish the current session, run xshot --quit, then scoop update xshot.",
        ],
    }
    if private:
        manifest["##"] = PRIVATE_NOTICE
        manifest["notes"][1] = "Before updating: finish the current session, then run Install-Scoop.ps1 again."
        manifest["notes"].append(PRIVATE_NOTICE)
    return manifest


def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate key {key!r}")
        result[key] = value
    return result


def read_manifest(path: Path) -> dict[str, Any]:
    manifest = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=reject_duplicates)
    version = manifest.get("version", "")
    if not isinstance(version, str) or not VERSION.fullmatch(version):
        raise ValueError("version must be a stable SemVer core without a leading v")
    homepage = manifest.get("homepage", "")
    repository = homepage.removeprefix("https://github.com/")
    if not REPOSITORY.fullmatch(repository):
        raise ValueError("homepage must name a GitHub owner/repository")
    digest = manifest.get("architecture", {}).get("64bit", {}).get("hash", "")
    if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ValueError("architecture.64bit.hash must be a lowercase SHA-256")
    if manifest != render(version, digest, repository, private="##" in manifest):
        raise ValueError("manifest does not match the xshot install and release contract")
    return manifest


def release_hash(directory: Path, version: str) -> str:
    name = f"xshot_{version}_windows_amd64.zip"
    archive = directory / name
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksums = directory / f"xshot_{version}_checksums.txt"
    matches = [line.split() for line in checksums.read_text(encoding="ascii").splitlines()
               if len(line.split()) == 2 and line.split()[1].lstrip("*") == name]
    if len(matches) != 1 or matches[0][0] != digest:
        raise ValueError(f"{checksums} must contain exactly one matching SHA-256 for {name}")
    return digest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("validate")
    check.add_argument("manifest", type=Path)
    update = sub.add_parser("update")
    update.add_argument("mode", choices=["write", "check"])
    update.add_argument("version")
    update.add_argument("directory", type=Path, nargs="?", default=ROOT / "build/release")
    update.add_argument("repository", nargs="?", default="isaksky/xshot")
    update.add_argument("--private", action="store_true", help="document authenticated private-release installation")
    args = parser.parse_args()
    try:
        if args.command == "validate":
            manifest = read_manifest(args.manifest)
            print(f"validated {args.manifest} for xshot {manifest['version']}")
            return
        if not VERSION.fullmatch(args.version) or not REPOSITORY.fullmatch(args.repository):
            raise ValueError("expected VERSION as x.y.z and repository as owner/name")
        manifest = render(args.version, release_hash(args.directory, args.version), args.repository, args.private)
        serialized = json.dumps(manifest, indent=4) + "\n"
        target = ROOT / "bucket/xshot.json"
        if args.mode == "write":
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(serialized, encoding="utf-8")
        elif read_manifest(target) != manifest or target.read_text(encoding="utf-8") != serialized:
            raise ValueError(f"{target} differs from release {args.version}; run --write after packaging")
        print(f"{args.mode}: {target} matches {args.directory} for xshot {args.version}")
    except (OSError, ValueError, TypeError, AttributeError) as error:
        print(f"Scoop manifest error: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
