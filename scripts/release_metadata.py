#!/usr/bin/env python3
"""Assemble verified release assets and generate package-manager definitions.

No network access or publication. Only the two named platform archives are
accepted, and package definitions are derived from their verified bytes.
"""

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import sys
import zipfile

from scoop_manifest import render as render_scoop


ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")
REPOSITORY = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
PLATFORMS = {"windows": "windows_amd64", "macos": "macos_arm64"}


def validate_identity(version, repository, commit):
    if not VERSION.fullmatch(version):
        raise ValueError("Expected a stable version such as 1.2.3, without v")
    if not REPOSITORY.fullmatch(repository):
        raise ValueError("Expected repository as owner/name")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected a full source commit SHA")


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def read_checksums(path):
    result = {}
    for line in path.read_text(encoding="ascii").splitlines():
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) != 2 or not re.fullmatch(r"[0-9a-f]{64}", parts[0]):
            raise ValueError(f"Invalid checksum record in {path}")
        name = parts[1].removeprefix("*")
        if name in result or "/" in name or "\\" in name:
            raise ValueError(f"Duplicate or unsafe checksum filename: {name}")
        result[name] = parts[0]
    return result


def inspect_archive(path, version, platform):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError(f"Duplicate archive entries in {path.name}")
        for name in names:
            if "\\" in name or name.startswith("/") or ".." in name.split("/"):
                raise ValueError(f"Unsafe archive path: {name}")
        required = {"VERSION", "LICENSE"}
        if platform == "windows":
            required |= {"snitt.exe", "RUNTIME-DEPENDENCIES.json",
                         "licenses/ffmpeg/manifest.json", "licenses/ffmpeg/COPYING.LGPLv2.1"}
        else:
            required |= {"Snitt.app/Contents/Info.plist", "Snitt.app/Contents/MacOS/snitt",
                         "THIRD-PARTY-NOTICES.txt", "DEPENDENCIES.json"}
        if missing := required - set(names):
            raise ValueError(f"Missing release material in {path.name}: {sorted(missing)}")
        if archive.read("VERSION").decode("ascii").strip() != version:
            raise ValueError(f"Archive version does not match {version}: {path.name}")
        if platform == "macos":
            info = plistlib.loads(archive.read("Snitt.app/Contents/Info.plist"))
            if info.get("CFBundleShortVersionString") != version:
                raise ValueError("Mac bundle version does not match release")
        else:
            runtime = json.loads(archive.read("licenses/ffmpeg/manifest.json"))
            if runtime.get("ffmpeg_license") != "LGPL-2.1-or-later":
                raise ValueError("Windows playback runtime does not declare the approved LGPL license")


def render_cask(version, sha256, repository):
    return f'''cask "snitt" do
  version "{version}"
  sha256 "{sha256}"

  url "https://github.com/{repository}/releases/download/v#{{version}}/snitt_#{{version}}_macos_arm64.zip"
  name "Snitt"
  desc "Capture, annotate, combine, and record screen regions"
  homepage "https://github.com/{repository}"

  depends_on arch: :arm64
  depends_on macos: :sequoia

  app "Snitt.app"
  binary "#{{appdir}}/Snitt.app/Contents/MacOS/snitt"

  uninstall quit: "local.snitt"

  caveats <<~EOS
    Snitt is not notarized; macOS may require approval before its first launch.
    Grant screen-recording permission when prompted. To start at login, add
    Snitt in System Settings > General > Login Items.
    Finish captures and quit Snitt before upgrading.
  EOS
end
'''


def write_definitions(version, repository, records, output, *, flat=False):
    win = f"snitt_{version}_windows_amd64.zip"
    mac = f"snitt_{version}_macos_arm64.zip"
    scoop = output / ("snitt.json" if flat else "bucket/snitt.json")
    cask = output / ("snitt.rb" if flat else "Casks/snitt.rb")
    for path in (scoop, cask):
        path.parent.mkdir(parents=True, exist_ok=True)
    scoop.write_text(json.dumps(render_scoop(version, records[win]["sha256"], repository), indent=4) + "\n")
    cask.write_text(render_cask(version, records[mac]["sha256"], repository))


def assemble(version, repository, commit, artifacts, output):
    validate_identity(version, repository, commit)
    records = {}
    archives = []
    for platform, suffix in PLATFORMS.items():
        directory = artifacts / f"{platform}-package"
        archive = directory / f"snitt_{version}_{suffix}.zip"
        checksums = read_checksums(directory / f"snitt_{version}_checksums.txt")
        sha256 = digest(archive)
        if checksums.get(archive.name) != sha256:
            raise ValueError(f"Archive checksum mismatch: {archive.name}")
        inspect_archive(archive, version, platform)
        records[archive.name] = {"sha256": sha256, "bytes": archive.stat().st_size}
        archives.append(archive)
    output.mkdir(parents=True, exist_ok=True)
    for archive in archives:
        shutil.copy2(archive, output / archive.name)
    for platform in PLATFORMS:
        inventory = artifacts / f"{platform}-package" / f"{platform}-build-info.txt"
        shutil.copy2(inventory, output / inventory.name)
    shutil.copy2(ROOT / "platform/windows/Install-Scoop.ps1", output / "Install-Scoop.ps1")
    (output / f"snitt_{version}_checksums.txt").write_text(
        "".join(f"{record['sha256']}  {name}\n" for name, record in sorted(records.items())))
    (output / "release.json").write_text(json.dumps({
        "schema_version": 1, "version": version, "repository": repository,
        "commit": commit, "archives": records,
    }, indent=2) + "\n")
    write_definitions(version, repository, records, output, flat=True)


def definitions(version, repository, commit, release, output):
    validate_identity(version, repository, commit)
    manifest = json.loads((release / "release.json").read_text())
    for key, expected in {"schema_version": 1, "version": version,
                          "repository": repository, "commit": commit}.items():
        if manifest.get(key) != expected:
            raise ValueError(f"Release provenance mismatch: {key}")
    checksums = read_checksums(release / f"snitt_{version}_checksums.txt")
    expected_names = {f"snitt_{version}_{suffix}.zip" for suffix in PLATFORMS.values()}
    records = manifest.get("archives", {})
    if set(records) != expected_names or set(checksums) != expected_names:
        raise ValueError("Release must contain exactly the two expected platform archives")
    for platform, suffix in PLATFORMS.items():
        archive = release / f"snitt_{version}_{suffix}.zip"
        actual = {"sha256": digest(archive), "bytes": archive.stat().st_size}
        if records[archive.name] != actual or checksums[archive.name] != actual["sha256"]:
            raise ValueError(f"Archive checksum/size mismatch: {archive.name}")
        inspect_archive(archive, version, platform)
    write_definitions(version, repository, records, output)


def set_version(version):
    validate_identity(version, "local/snitt", "0" * 40)
    path = ROOT / "platform/macos/Info.plist"
    info = plistlib.loads(path.read_bytes())
    info["CFBundleShortVersionString"] = version
    info["CFBundleVersion"] = version
    path.write_bytes(plistlib.dumps(info, sort_keys=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    version = commands.add_parser("version", help="stamp the CI checkout's macOS metadata")
    version.add_argument("version")
    for name in ("assemble", "definitions"):
        command = commands.add_parser(name)
        command.add_argument("version")
        command.add_argument("--repository", required=True)
        command.add_argument("--commit", required=True)
        command.add_argument("--output", type=Path, required=True)
        command.add_argument("--artifacts" if name == "assemble" else "--release-directory",
                             type=Path, required=True)
    args = parser.parse_args()
    if args.command == "version":
        set_version(args.version)
    elif args.command == "assemble":
        assemble(args.version, args.repository, args.commit, args.artifacts, args.output)
    else:
        definitions(args.version, args.repository, args.commit, args.release_directory, args.output)
    print(f"{args.command}: verified Snitt {args.version}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        sys.exit(f"Release metadata error: {error}")
