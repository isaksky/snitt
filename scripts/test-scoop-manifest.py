#!/usr/bin/env python3
"""Exercise release checksums and reject unsafe/incomplete Scoop metadata."""

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scoop_manifest import read_manifest, release_hash, render


class ScoopManifestTests(unittest.TestCase):
    def test_release_checksum_matches_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / 'snitt_1.2.3_windows_amd64.zip'
            archive.write_bytes(b'release fixture')
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            sums = root / 'snitt_1.2.3_checksums.txt'
            sums.write_text(f'{digest}  {archive.name}\n')
            self.assertEqual(release_hash(root, '1.2.3'), digest)
            archive.write_bytes(b'corrupted release')
            with self.assertRaises(ValueError):
                release_hash(root, '1.2.3')

    def test_duplicate_checksums_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            name = 'snitt_1.2.3_windows_amd64.zip'
            (root / name).write_bytes(b'fixture')
            digest = hashlib.sha256(b'fixture').hexdigest()
            (root / 'snitt_1.2.3_checksums.txt').write_text(f'{digest}  {name}\n' * 2)
            with self.assertRaises(ValueError):
                release_hash(root, '1.2.3')

    def test_manifest_requires_dependencies_and_scoped_hooks(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'snitt.json'
            manifest = render('1.2.3', 'a' * 64, 'example/snitt-releases')
            path.write_text(json.dumps(manifest))
            self.assertEqual(read_manifest(path), manifest)
            for key in ('depends', 'pre_install', 'post_install', 'pre_uninstall', 'uninstaller'):
                changed = dict(manifest)
                del changed[key]
                path.write_text(json.dumps(changed))
                with self.subTest(key=key), self.assertRaises(ValueError):
                    read_manifest(path)

    def test_duplicate_json_keys_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'snitt.json'
            path.write_text('{"version": "1.2.3", "version": "4.5.6"}')
            with self.assertRaises(ValueError):
                read_manifest(path)

    def test_private_manifest_retains_original_release_url_and_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'snitt.json'
            public = render('1.2.3', 'b' * 64, 'example/private-snitt')
            private = render('1.2.3', 'b' * 64, 'example/private-snitt', private=True)
            self.assertEqual(private['architecture'], public['architecture'])
            self.assertEqual(private['depends'], public['depends'])
            self.assertIn('scoop update snitt', public['notes'][1])
            self.assertIn('Install-Scoop.ps1 again', private['notes'][1])
            self.assertNotIn('scoop update snitt', private['notes'][1])
            path.write_text(json.dumps(private))
            self.assertEqual(read_manifest(path), private)


if __name__ == '__main__':
    unittest.main()
