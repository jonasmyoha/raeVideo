# Measurements

Numbers that decide something in [design.md](design.md), with how to get them
again.

## The 4K copy path (decides §12 V3) — 2026-11-14

What it measures: `checks/copyPath4k/Main.rae`, run by hand after
`sh tests/media/make.sh 4k` (it makes `build/media/counter4k.mp4`, H.264, and
`counter4kHevc.mp4`, HEVC: 3840x2160, 300 frames at 29.97 fps, BT.709, made
from the same math as the test media).

    ./rae run checks/copyPath4k/Main.rae

1. For every frame: decode (VideoToolbox, `FrameStream.takeFrame`), upload (the
   queue's copy of the two NV12 planes from the locked frame,
   `YuvPicture.uploadFrame`), convert (the YUV to RGB compute pass, waited for,
   `YuvPicture.convertNow`). All on one thread, one after the other.
2. The file played by `VideoPlayer` against the real clock: the audio engine is
   offline but rendered to keep up with the wall clock, the display refreshes
   at 60 Hz, and each new frame is uploaded and converted at once. "Skipped" is
   the player's `droppedFrames`.

Machine: Apple M1 Max (unified memory), macOS 27.0. Two runs:

| file | step | run 1 mean / p95 / largest (ms) | run 2 mean / p95 / largest (ms) |
|---|---|---|---|
| H.264 | decode | 2.39 / 6.87 / 11.95 | 2.28 / 6.54 / 11.00 |
| H.264 | upload | 2.10 / 2.60 / 7.42 | 1.80 / 2.12 / 3.12 |
| H.264 | convert | 2.41 / 5.58 / 11.70 | 1.98 / 4.99 / 8.47 |
| H.264 | total per frame | 6.91 / 13.28 / 21.48 | 6.06 / 10.56 / 20.23 |
| HEVC | decode | 2.41 / 7.56 / 13.13 | 2.17 / 6.45 / 12.67 |
| HEVC | upload | 2.13 / 2.86 / 4.88 | 1.95 / 2.32 / 5.32 |
| HEVC | convert | 2.53 / 5.04 / 5.80 | 2.44 / 5.49 / 6.48 |
| HEVC | total per frame | 7.07 / 13.47 / 22.10 | 6.56 / 12.27 / 17.52 |

Played at 60 Hz against the clock (601 refreshes, 300 frames each):

| file | run | frames skipped | work per refresh mean / p95 / largest (ms) | refreshes over 16.7 ms |
|---|---|---|---|---|
| H.264 | 1 | 0 | 4.18 / 13.40 / 25.43 | 13 |
| H.264 | 2 | 0 | 3.16 / 11.93 / 15.42 | 0 |
| HEVC | 1 | 0 | 3.13 / 12.12 / 16.90 | 1 |
| HEVC | 2 | 0 | 3.62 / 10.48 / 14.80 | 0 |

What it says:

- **4K at 29.97 fps plays without a dropped frame** on this Mac, with
  everything on one thread: the worst frame (22 ms) is well inside a frame's
  33.4 ms.
- **The copy itself is cheap here**: about 2 ms per 4K frame for both planes
  (12 MB), steady (p95 under 3 ms). The conversion pass is about 2.5 ms. On
  unified memory the copy path is not the cost that matters.
- **The spikes are the decode and the waiting**, not the copy: the decode's
  p95 is 6.5 to 7.5 ms with peaks of 11 to 13 ms, and `convertNow` waits for
  the GPU on the calling thread. At 60 fps (16.7 ms a frame) those peaks
  would drop frames. The fix for that is the frame pipeline (§5 V3: decode
  ahead on worker threads, the conversion recorded into the renderer's own
  frame instead of waited for), not zero-copy.

Caveats: the test files are synthetic and compress very well (280 KB and
350 KB for 10 s), so their decode is lighter than real camera footage at
50 to 150 Mbit/s; the copy and the conversion do not depend on the content.
Not measured: an iPhone (where memory bandwidth is lower), 10-bit (P010)
frames, 60 fps sources, and a discrete-GPU machine where the upload crosses
PCIe.

Decision for §12 V3: on Apple Silicon Macs the copy path is enough, and the
zero-copy import (IOSurface into WebGPU) is not needed for playback; it stays
open for the iPhone, for 10-bit and 60 fps 4K, and for editing with many
layers, to be measured when those land.
