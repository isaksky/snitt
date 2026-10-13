"""Generated media only: verify Jolt process ownership and retained packet data."""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def ffmpeg(*args):
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', *args], check=True)


def probe(path):
    return json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_format',
        '-show_streams', '-show_packets', '-show_data_hash', 'sha256', '-of', 'json', str(path)]))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


with tempfile.TemporaryDirectory(prefix='snitt-jolt-trim-') as directory:
    root = Path(directory)
    os.environ['SNITT_JOLT_DATA_DIR'] = str(root / 'settings')
    regular = root / 'regular.mp4'
    ffmpeg('-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=30:duration=4',
           '-f', 'lavfi', '-i', 'sine=frequency=440:duration=4', '-c:v', 'libx264',
           '-g', '60', '-bf', '3', '-c:a', 'aac', str(regular))
    reordered = root / 'reordered.mp4'
    ffmpeg('-f', 'lavfi', '-i', 'color=c=red:s=160x90:r=30:d=10',
           '-f', 'lavfi', '-i', 'sine=frequency=440:duration=10',
           '-vf', "drawbox=c=green:t=fill:enable='gte(t,4)*lt(t,7.1)',drawbox=c=blue:t=fill:enable='gte(t,7.1)',select='eq(n,0)+eq(n,12)+gte(n,24)'",
           '-fps_mode', 'vfr', '-c:v', 'libx264', '-g', '300', '-bf', '3', '-c:a', 'aac', str(reordered))
    held = root / 'held.mp4'
    ffmpeg('-f', 'lavfi', '-i', 'color=c=blue:s=160x90:r=1/6:d=6',
           '-frames:v', '1', '-c:v', 'libx264', str(held))
    short = root / 'short.mp4'
    ffmpeg('-f', 'lavfi', '-i', 'testsrc2=size=160x90:rate=30:duration=0.24',
           '-c:v', 'libx264', str(short))

    def run(source, mode, start=500, end=2500):
        path = root / f'{source.stem}-{mode}.mp4'
        shutil.copyfile(source, path)
        if mode in ("replace-failure", "retry"):
            Path(str(path) + ".locked").mkdir()
        before = digest(path)
        original = probe(path)
        # A held POSIX descriptor must retain the original inode across replace;
        # failing/canceled operations must leave both descriptor and path intact.
        # Windows cannot replace a file another process holds without delete sharing.
        with (path.open('rb') if os.name == 'posix' else contextlib.nullcontext()) as opened:
            result = subprocess.run([os.environ.get('JOLT', 'jolt'), '-M:trim', str(path), mode,
                                     str(start), str(end)], capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            if opened:
                assert hashlib.sha256(opened.read()).hexdigest() == before
        print(next(line for line in result.stdout.splitlines() if line.startswith('PASS:')))
        assert not list(root.glob('.snitt-trim-*'))
        assert not list(root.glob('.snitt-original-*'))
        if mode in ('success', 'retry'):
            after = probe(path)
            assert abs(float(after['format']['duration']) - (end - start) / 1000) <= .1
            assert {s['codec_type'] for s in after['streams']} == {s['codec_type'] for s in original['streams']}
            original_hashes = {p['data_hash'] for p in original['packets']}
            assert all(p['data_hash'] in original_hashes for p in after['packets'])
            for stream in after['streams']:
                assert float(stream['duration']) <= (end - start) / 1000 + .002
                index = stream['index']
                copied = [p['data_hash'] for p in after['packets'] if p['stream_index'] == index]
                prior = [p['data_hash'] for p in original['packets'] if p['stream_index'] == index]
                assert copied and any(prior[i:i + len(copied)] == copied for i in range(len(prior)))
            if source == reordered:
                pixels = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(path),
                    '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'])
                centers = [pixels[i + (45 * 160 + 80) * 3:i + (45 * 160 + 80) * 3 + 3]
                           for i in range(0, len(pixels), 160 * 90 * 3)]
                assert centers and all(b < max(r, g) for r, g, b in centers)
                assert any(g > r * 2 for r, g, b in centers)
            subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 'null', '-'], check=True)
        else:
            assert digest(path) == before

    run(regular, 'success')
    run(reordered, 'success', 960, 7070)
    run(short, 'success', 33, 200)
    for mode in ('cancel', 'cancel-running', 'cancel-probe', 'process-failure', 'exit-failure',
                 'probe-failure', 'probe-start-failure', 'replace-failure', 'retry', 'dispose'):
        run(regular, mode)
    run(regular, 'thumbnails')
    run(held, 'thumbnails')
    run(short, 'thumbnails')
held = 'POSIX held file descriptors, ' if os.name == 'posix' else ''
print(f'PASS: packets/audio/timing, {held}failures and temporary cleanup')
