# Recording startup measurements

`recording_startup_benchmark.cpp` reproduces the original and tuned FFmpeg
arguments without changing production behavior. A Qt widget receives a
synthetic mouse release over a visible marker and starts FFmpeg from that
release handler. A monotonic clock records:

- **Process**: `QProcess::started` after release.
- **First screen packet**: the first video `demuxer ->` line from FFmpeg
  `-debug_ts`. This is the earliest input frame observable at FFmpeg's
  demuxer, rather than an OS-level capture timestamp.
- **Readiness**: the first positive `frame=` on `-progress pipe:1`, matching
  the FFmpeg readiness signal used by Snitt.

Both modes use `-debug_ts -loglevel info` for measurement and the same
capture, filter, encoder, and output options. The baseline uses the original
0.2-second progress interval and default input probing. The tuned mode uses
the 0.05-second interval, `-probesize 32`, and `-analyzeduration 0` added for
`xshot-7` (the original issue ID). Run each mode multiple times in alternating order on an interactive
desktop; the benchmark removes its temporary MP4 after exit.

From the repository root, create and enter `build/recording-startup`, then
run `qmake6 ../../tests/recording_startup_benchmark.pro` and `make` on macOS
or `mingw32-make` on Windows. Run the resulting
`recording_startup_benchmark baseline` and
`recording_startup_benchmark tuned` executables with a visible desktop and
FFmpeg on `PATH`. This benchmark does not replace the app's interactive
recording tests, which validate saved video, overlays, and file reveal.

## Results (September 2026)

Milliseconds are measured from the release event. Each row is three runs in
alternating baseline/tuned order; values are individual samples followed by
their median.

| OS and path | Process / stream request | First screen packet / sample | Readiness |
| --- | ---: | ---: | ---: |
| macOS FFmpeg baseline | 2, 1, 2 (**2**) | 409, 392, 394 (**394**) | 1226, 1206, 1209 (**1209**) |
| macOS FFmpeg tuned | 2, 2, 1 (**2**) | 403, 389, 393 (**393**) | 1055, 1039, 1038 (**1039**) |
| Windows FFmpeg baseline | 3, 3, 4 (**3**) | 753, 753, 752 (**753**) | 968, 968, 968 (**968**) |
| Windows FFmpeg tuned | 2, 3, 3 (**3**) | 125, 97, 98 (**98**) | 703, 719, 719 (**719**) |

The Windows input-probe change brought the first demuxed frame forward by
about 655 ms and the readiness report by about 249 ms. The remaining gap
between its first packet and readiness is FFmpeg's encoding/progress path.
On macOS, FFmpeg's capture packet time was almost unchanged by probing,
while readiness improved about 170 ms. The native ScreenCaptureKit recorder
removes that FFmpeg path and excludes Snitt's own windows from the stream.

In the actual interactive macOS app test, the earlier FFmpeg version reached
process launch 46–48 ms and positive progress/readiness 1270–1375 ms after
selection release. The ScreenCaptureKit version requested the stream at
132 ms and observed its first complete frame **and** recording-output start
at 209 ms; there is no child process to launch. On Windows, the current
interactive app test observed process launch at 31 ms and readiness at 720
ms. The combined-branch Windows test observed readiness at 747 ms. The
synthetic harness isolates FFmpeg timing; its immediate start from the
release callback is why its process times are lower than the app's.

The interactive app tests also decoded the first five video frames and
checked the marker color at the center. This caught startup-overlay
contamination during development and passed for the final native macOS and
FFmpeg Windows paths.
