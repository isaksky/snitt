#!/usr/bin/env python3
"""Focused selection checks for the Mac private multimedia SDK contract."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
BUILDER = ROOT / "bin/build-mac-multimedia-runtime"


def check(qmake):
    return subprocess.run([sys.executable, str(BUILDER), "--check-sdk", "--qmake", str(qmake)],
                          text=True, capture_output=True)


def proxy(directory, real, version):
    script = directory / "qmake6"
    script.write_text(
        "#!/usr/bin/env python3\n"
        "import subprocess, sys\n"
        f"if sys.argv[1:] == ['-query', 'QT_VERSION']: print({version!r})\n"
        f"else: sys.exit(subprocess.call([{str(real)!r}] + sys.argv[1:]))\n")
    script.chmod(0o755)
    return script


def main():
    if sys.platform != "darwin":
        print("macOS SDK selection check skipped")
        return
    real = Path(os.environ.get("SNITT_QMAKE") or shutil.which("qmake6") or "")
    if not real.is_file():
        raise RuntimeError("Select an installed SDK with SNITT_QMAKE")
    actual = subprocess.check_output([str(real), "-query", "QT_VERSION"], text=True).strip()
    result = check(real)
    assert result.returncode == 0, result.stderr
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        result = check(proxy(directory, real, "6.11.3"))
        assert result.returncode != 0 and "unsupported" in result.stderr, result
        mismatch = "6.11.1" if actual == "6.11.2" else "6.11.2"
        result = check(proxy(directory, real, mismatch))
        assert result.returncode != 0 and "mismatched" in result.stderr, result
    print(f"Qt {actual} matched; unpinned newer SDK and mixed SDK rejected before build")


if __name__ == "__main__":
    main()
