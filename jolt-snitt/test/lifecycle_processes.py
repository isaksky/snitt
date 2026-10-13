"""Exercise the release launcher with private socket/pipe and settings identities."""

import contextlib
import hashlib
import os
import pathlib
import secrets
import socket
import subprocess
import sys
import tempfile
import time


binary = pathlib.Path(sys.argv[1]).resolve()
image = pathlib.Path(sys.argv[2]).resolve()
assert image.is_file()
WINDOWS = os.name == "nt"


def windows_session():
    import ctypes
    session = ctypes.c_ulong()
    assert ctypes.windll.kernel32.ProcessIdToSessionId(os.getpid(), ctypes.byref(session))
    return session.value


def fixture(root, suffix):
    identity = f"test-{secrets.token_hex(8)}-{suffix}"
    env = os.environ.copy()
    env.update(
        SNITT_JOLT_IPC_ID=identity,
        SNITT_JOLT_DATA_DIR=str(root / identity),
        SNITT_JOLT_FAKE_HOTKEY="1",
        QT_QPA_PLATFORM="offscreen",
    )
    # QDir::homePath() spells the profile with forward slashes on Windows.
    name = (f"snittj-{hashlib.sha256(pathlib.Path.home().as_posix().encode()).hexdigest()[:16]}-"
            f"{hashlib.sha256(identity.encode()).hexdigest()[:12]}")
    if WINDOWS:
        name += f"-{windows_session()}"
    endpoint = pathlib.Path(tempfile.gettempdir()) / name
    lock = endpoint.with_name(endpoint.name + ".lock")
    return env, endpoint, lock


@contextlib.contextmanager
def foreign_endpoint(endpoint, listening):
    """Occupy the endpoint name from this process: refused, or accepted but never answered."""
    if WINDOWS:
        # A named pipe disappears with its last server handle, so Windows has no
        # stale endpoint; one never-serviced pipe instance is unresponsive.
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateNamedPipeW.restype = wintypes.HANDLE
        kernel32.CreateNamedPipeW.argtypes = [wintypes.LPCWSTR] + [wintypes.DWORD] * 6 + [wintypes.LPVOID]
        pipe = kernel32.CreateNamedPipeW("\\\\.\\pipe\\" + endpoint.name, 3, 0, 1, 4096, 4096, 0, None)
        if pipe in (None, wintypes.HANDLE(-1).value):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            yield
        finally:
            kernel32.CloseHandle(wintypes.HANDLE(pipe))
        return
    assert not endpoint.exists()
    peer = socket.socket(socket.AF_UNIX)
    peer.bind(str(endpoint))
    if listening:
        peer.listen(2)
    else:
        peer.close()
    try:
        yield
        assert endpoint.exists(), "another process's endpoint was removed"
    finally:
        peer.close()
        endpoint.unlink(missing_ok=True)


def launch(env, *args):
    return subprocess.Popen([str(binary), *args], cwd=binary.parent.parent.parent / "jolt-snitt",
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def client(env, *args, expected=0):
    result = subprocess.run([str(binary), *args], cwd=binary.parent.parent.parent / "jolt-snitt",
                            env=env, capture_output=True, text=True, timeout=12)
    assert result.returncode == expected, (args, result.returncode, result.stderr)


def stop(process):
    if process.poll() is None:
        process.terminate()
    try:
        out, err = process.communicate(timeout=8)
    except subprocess.TimeoutExpired:
        process.kill()
        out, err = process.communicate(timeout=8)
        raise AssertionError(f"launcher did not stop: {err}")
    return out, err


with tempfile.TemporaryDirectory(prefix="snitt-jolt-lifecycle-") as directory:
    root = pathlib.Path(directory)
    env, endpoint, lock = fixture(root, "routing")
    client(env, "--quit")
    owner = launch(env, "--background")
    try:
        deadline = time.monotonic() + 12
        while not lock.exists() and owner.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        assert owner.poll() is None and lock.exists(), (owner.poll(), stop(owner))
        client(env, "--background")
        client(env, "--show")
        client(env, os.path.relpath(image, binary.parent.parent.parent / "jolt-snitt"))
        assert owner.poll() is None, stop(owner)
        client(env, "--quit")
        owner.wait(timeout=8)
        assert owner.returncode == 0, stop(owner)
    finally:
        stop(owner)

    # Refused connections cannot distinguish a stale endpoint from a live
    # listener with a full backlog. Neither endpoint may be removed blindly.
    for suffix, listening in [("stale", False), ("unresponsive", True)]:
        if WINDOWS and not listening:
            continue
        env, endpoint, lock = fixture(root, suffix)
        with foreign_endpoint(endpoint, listening):
            client(env, "--background", expected=1)
            client(env, "--quit", expected=1)

    env, endpoint, lock = fixture(root, "race")
    contenders = [launch(env, "--background") for _ in range(3)]
    try:
        deadline = time.monotonic() + 15
        while sum(p.poll() is not None for p in contenders) < 2 and time.monotonic() < deadline:
            time.sleep(0.1)
        live = [p for p in contenders if p.poll() is None]
        assert len(live) == 1, [(p.poll(), p.stderr.read() if p.poll() is not None else "")
                                for p in contenders]
        assert all(p.returncode == 0 for p in contenders if p.poll() is not None)
        client(env, "--quit")
        live[0].wait(timeout=8)
        assert live[0].returncode == 0, stop(live[0])
    finally:
        for process in contenders:
            stop(process)

print("PASS: isolated forwarding, safe socket refusal, and concurrent ownership")
