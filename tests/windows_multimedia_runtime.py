#!/usr/bin/env python3
"""Check that the Windows runtime gate rejects incompatible or GPL builds."""

import importlib.util
from pathlib import Path
import shlex
import unittest


path = Path(__file__).resolve().parents[1] / "bin/build-windows-multimedia-runtime.py"
spec = importlib.util.spec_from_file_location("windows_runtime", path)
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class RuntimePolicyTests(unittest.TestCase):
    def test_approved_configuration_with_install_path(self):
        runtime.check_configuration(
            shlex.join(["--prefix=C:/a path/runtime", *runtime.FLAGS]), runtime.LICENSE)

    def test_rejects_gpl_or_nonfree_runtime(self):
        for license_text in ("GPL version 3 or later", "nonfree and unredistributable"):
            with self.subTest(license_text=license_text), self.assertRaises(RuntimeError):
                runtime.check_configuration(shlex.join(runtime.FLAGS), license_text)
        for flag in ("--enable-gpl", "--enable-nonfree", "--enable-version3", "--enable-libx264"):
            with self.subTest(flag=flag), self.assertRaises(RuntimeError):
                runtime.check_configuration(shlex.join([*runtime.FLAGS, flag]), runtime.LICENSE)

    def test_rejects_missing_autodetection_guard(self):
        flags = [flag for flag in runtime.FLAGS if flag != "--disable-autodetect"]
        with self.assertRaises(RuntimeError):
            runtime.check_configuration(shlex.join(flags), runtime.LICENSE)

    def test_plugin_abi_ignores_system_and_qt_imports(self):
        runtime.check_codec_imports(runtime.DLLS | {"KERNEL32.dll", "Qt6Multimedia.dll"})

    def test_rejects_other_ffmpeg_abi_or_extra_library(self):
        changed = runtime.DLLS - {"avcodec-63.dll"} | {"avcodec-62.dll"}
        for names in (changed, runtime.DLLS | {"avfilter-12.dll"}, runtime.DLLS - {"swscale-10.dll"}):
            with self.subTest(names=names), self.assertRaises(RuntimeError):
                runtime.check_codec_imports(names)


if __name__ == "__main__":
    unittest.main()
