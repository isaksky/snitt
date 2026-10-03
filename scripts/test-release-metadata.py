#!/usr/bin/env python3
"""Fast release contract checks; no compiler, desktop, or network required."""

import hashlib
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import release_metadata as release
from scoop_manifest import read_manifest, render


class ReleaseMetadataTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.artifacts = self.root / "artifacts"
        self.output = self.root / "release"
        self.version = "1.2.3"
        self.repository = "example/snitt"
        self.commit = "a" * 40
        self.make_artifacts()

    def make_artifacts(self):
        for platform, suffix in release.PLATFORMS.items():
            directory = self.artifacts / f"{platform}-package"
            directory.mkdir(parents=True, exist_ok=True)
            archive = directory / f"snitt_{self.version}_{suffix}.zip"
            files = {"VERSION": self.version, "LICENSE": "MIT fixture"}
            if platform == "windows":
                files.update({"snitt.exe": "fixture", "RUNTIME-DEPENDENCIES.json": "{}",
                              "licenses/ffmpeg/COPYING.LGPLv2.1": "license fixture",
                              "licenses/ffmpeg/manifest.json": json.dumps({"ffmpeg_license": "LGPL-2.1-or-later"})})
            else:
                info = {"CFBundleShortVersionString": self.version,
                        "CFBundleVersion": self.version}
                if "-" in self.version:
                    info = {"CFBundleShortVersionString": self.version.split("-")[0],
                            "CFBundleVersion": self.version.replace("-alpha.", "a").replace("-beta.", "b").replace("-rc.", "fc"),
                            "SnittReleaseVersion": self.version}
                files.update({"Snitt.app/Contents/MacOS/snitt": "fixture",
                              "Snitt.app/Contents/Info.plist": plistlib.dumps(info),
                              "THIRD-PARTY-NOTICES.txt": "fixture", "DEPENDENCIES.json": "{}"})
            with zipfile.ZipFile(archive, "w") as bundle:
                for name, content in files.items():
                    bundle.writestr(name, content)
            self.record_checksum(archive)
            (directory / f"{platform}-build-info.txt").write_text("SDK fixture\n")

    def record_checksum(self, archive):
        (archive.parent / f"snitt_{self.version}_checksums.txt").write_text(
            f"{release.digest(archive)}  {archive.name}\n")

    def assemble(self):
        release.assemble(self.version, self.repository, self.commit, self.artifacts, self.output)

    def definitions(self):
        release.definitions(self.version, self.repository, self.commit, self.output, self.root / "checkout")

    def test_platform_checksums_survive_merge_and_definitions_match_archives(self):
        self.assemble()
        self.definitions()
        records = release.read_checksums(self.output / "snitt_1.2.3_checksums.txt")
        self.assertEqual(len(records), 2)
        manifest = read_manifest(self.root / "checkout/bucket/snitt.json")
        self.assertEqual(manifest["license"], "MIT")
        self.assertNotIn("##", manifest)
        self.assertEqual(manifest["architecture"]["64bit"]["hash"], records["snitt_1.2.3_windows_amd64.zip"])
        cask = (self.root / "checkout/Casks/snitt.rb").read_text()
        self.assertIn(records["snitt_1.2.3_macos_arm64.zip"], cask)
        self.assertIn('app "Snitt.app"', cask)
        self.assertIn('depends_on arch: :arm64', cask)
        self.assertNotIn("no-quarantine", cask)
        for name, digest in records.items():
            self.assertEqual(hashlib.sha256((self.output / name).read_bytes()).hexdigest(), digest)

    def test_rejects_corrupted_build_artifact_before_writing_output(self):
        archive = next((self.artifacts / "windows-package").glob("*.zip"))
        with archive.open("ab") as stream:
            stream.write(b"changed after checksum")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_requires_both_platforms(self):
        next((self.artifacts / "macos-package").glob("*.zip")).unlink()
        with self.assertRaises(FileNotFoundError):
            self.assemble()

    def test_rejects_archive_from_another_version_even_with_valid_hash(self):
        archive = next((self.artifacts / "macos-package").glob("*.zip"))
        with zipfile.ZipFile(archive) as original:
            contents = {name: original.read(name) for name in original.namelist()}
        contents["VERSION"] = "1.2.2"
        with zipfile.ZipFile(archive, "w") as changed:
            for name, data in contents.items():
                changed.writestr(name, data)
        self.record_checksum(archive)
        with self.assertRaisesRegex(ValueError, "version"):
            self.assemble()

    def test_rejects_traversal_in_archive(self):
        archive = next((self.artifacts / "windows-package").glob("*.zip"))
        with zipfile.ZipFile(archive, "a") as changed:
            changed.writestr("../unwanted", "fixture")
        self.record_checksum(archive)
        with self.assertRaisesRegex(ValueError, "Unsafe archive path"):
            self.assemble()

    def test_rejects_duplicate_checksum(self):
        sums = next((self.artifacts / "windows-package").glob("*checksums.txt"))
        sums.write_text(sums.read_text() * 2)
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.assemble()

    def test_published_artifact_cannot_change_after_assembly(self):
        self.assemble()
        archive = next(self.output.glob("*.zip"))
        with archive.open("ab") as stream:
            stream.write(b"changed")
        with self.assertRaisesRegex(ValueError, "checksum/size"):
            self.definitions()
        self.assertFalse((self.root / "checkout").exists())

    def test_source_commit_is_checked_before_package_update(self):
        self.assemble()
        self.commit = "b" * 40
        with self.assertRaisesRegex(ValueError, "provenance mismatch: commit"):
            self.definitions()

    def test_rejects_unsafe_or_invalid_version_input(self):
        for value in ("v1.2.3", "../1.2.3", "01.2.3", "1.2.3-rc1", "1.2.3\nrun: anything",
                      "1.2.3-alpha", "1.2.3-alpha.0", "1.2.3-alpha.01", "1.2.3-alpha.256",
                      "1.2.3-preview.1", "1.2.3-alpha.1.2", "1.2.3-alpha.1+build"):
            with self.subTest(version=value), self.assertRaises(ValueError):
                release.validate_identity(value, self.repository, self.commit)
        with self.assertRaises(ValueError):
            release.validate_identity(self.version, 'example/";bad', self.commit)

    def test_stamps_both_macos_version_fields(self):
        path = self.root / "platform/macos/Info.plist"
        path.parent.mkdir(parents=True)
        path.write_bytes(plistlib.dumps({"CFBundleIdentifier": "local.snitt", "CFBundleVersion": "1"}))
        with patch.object(release, "ROOT", self.root):
            release.set_version(self.version)
        info = plistlib.loads(path.read_bytes())
        self.assertEqual(info["CFBundleVersion"], self.version)
        self.assertEqual(info["CFBundleShortVersionString"], self.version)
        self.assertEqual(info["SnittReleaseVersion"], self.version)
        self.assertEqual(info["CFBundleIdentifier"], "local.snitt")

    def test_prereleases_have_distinct_valid_macos_build_numbers(self):
        for stage, suffix, number in (("alpha", "a", 1), ("beta", "b", 2), ("rc", "fc", 255)):
            version = f"1.2.3-{stage}.{number}"
            with self.subTest(version=version):
                release.validate_identity(version, self.repository, self.commit)
                self.assertTrue(release.is_prerelease(version))
                self.assertEqual(release.macos_versions(version), {
                    "CFBundleShortVersionString": "1.2.3",
                    "CFBundleVersion": f"1.2.3{suffix}{number}",
                    "SnittReleaseVersion": version})
        self.assertFalse(release.is_prerelease("1.2.3"))

    def test_alpha_assets_exclude_stable_installation_and_reject_package_updates(self):
        self.version = "1.2.3-alpha.1"
        self.make_artifacts()
        self.assemble()
        self.assertEqual({p.name for p in self.output.iterdir()}, {
            "snitt_1.2.3-alpha.1_windows_amd64.zip", "snitt_1.2.3-alpha.1_macos_arm64.zip",
            "snitt_1.2.3-alpha.1_checksums.txt", "release.json",
            "windows-build-info.txt", "macos-build-info.txt"})
        manifest = json.loads((self.output / "release.json").read_text())
        self.assertEqual(manifest["version"], self.version)
        self.assertEqual(manifest["commit"], self.commit)
        with self.assertRaisesRegex(ValueError, "stable package"):
            self.definitions()
        self.assertFalse((self.root / "checkout").exists())

    def test_alpha_rejects_a_different_build_with_the_same_numeric_macos_version(self):
        self.version = "1.2.3-alpha.1"
        self.make_artifacts()
        archive = self.artifacts / "macos-package" / f"snitt_{self.version}_macos_arm64.zip"
        with zipfile.ZipFile(archive) as original:
            contents = {name: original.read(name) for name in original.namelist()}
        key = "Snitt.app/Contents/Info.plist"
        info = plistlib.loads(contents[key])
        info["SnittReleaseVersion"] = "1.2.3-alpha.2"
        contents[key] = plistlib.dumps(info)
        with zipfile.ZipFile(archive, "w") as changed:
            for name, data in contents.items():
                changed.writestr(name, data)
        self.record_checksum(archive)
        with self.assertRaisesRegex(ValueError, "SnittReleaseVersion"):
            self.assemble()
        self.assertFalse(self.output.exists())

    def test_assembly_rejects_leftover_assets_from_another_release(self):
        self.output.mkdir()
        (self.output / "snitt.json").write_text("old stable package definition")
        with self.assertRaisesRegex(ValueError, "must be empty"):
            self.assemble()

    def test_historical_scoop_manifest_still_validates(self):
        path = self.root / "old.json"
        path.write_text(json.dumps(render("0.1.0", "a" * 64, self.repository, private=True, source_license="Unknown")))
        self.assertEqual(read_manifest(path)["license"], "Unknown")


if __name__ == "__main__":
    unittest.main()
