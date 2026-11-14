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

## The frame pipeline at 4K and 59.94 fps — 2026-11-15

What it measures: `checks/pipeline4k/Main.rae`, run by hand after
`sh tests/media/make.sh 4k` (which now also makes `build/media/counter4k60.mp4`:
3840x2160, 600 frames at 60000/1001, H.264 High level 5.1).

    ./rae run checks/pipeline4k/Main.rae                                    # 59.94 fps
    ./rae run checks/pipeline4k/Main.rae -- build/media/counter4k.mp4      # 29.97 fps

`VideoPlayer.refreshFromPipeline` plays the file from a `FramePipeline` against
the real clock: the audio engine offline, rendered to keep up with the wall
clock; a refresh every 16.67 ms (60 Hz); the first frame shown paused, then
play; the file's worker thread decodes 16 frames ahead; the main thread uploads
at most 2 arrived frames per refresh and records the shown frame's conversion
into the refresh's recording, submitted and never waited for; a 1 GB cache.
There is no window, so this is the pipeline without a display's presentation
(examples/simplePlayer's README has what a window adds). Apple M1 Max, macOS 27.0.

| file | frames shown | skipped | late refreshes | main thread work per refresh (mean / p95 / largest) | cache most |
|---|---|---|---|---|---|
| 59.94 fps, run 1 | 600 of 600 | 0 | 0 | 2.10 / 2.46 / 5.88 ms | 1020 of 1024 MB |
| 59.94 fps, run 2 | 600 of 600 | 0 | 0 | 2.18 / 2.56 / 6.64 ms | 1020 of 1024 MB |
| 59.94 fps, run 3 | 600 of 600 | 0 | 0 | 2.08 / 2.40 / 6.22 ms | 1020 of 1024 MB |
| 29.97 fps H.264 | 300 of 300 | 0 | 0 | 1.14 / 2.33 / 6.97 ms | 1020 of 1024 MB |
| 29.97 fps HEVC | 300 of 300 | 0 | 0 | 1.10 / 2.13 / 6.72 ms | 1020 of 1024 MB |

In every run the worker kept at least 15 frames decoded ahead of the frame
shown. Compared with the synchronous player of the first measurement (decode,
upload and a waited conversion on the calling thread: p95 10.5 to 13.5 ms a
frame at 29.97 fps), the main thread now does only the upload and the recording:
p95 about 2.5 ms.

What it took besides the threads: at 60 Hz and 59.94 fps the refreshes and the
frames' starts line up for minutes, and a refresh woken 0.1 ms late skipped a
frame (the next one repeating it). `VideoPlayer.frameToShow` now calls a refresh
landing within 2 ms after a frame's start, two frames after the one shown,
jitter, and shows the frame in between: one frame per refresh, at most 2 ms
behind the clock. Before it, 1 or 2 frames of 600 were skipped in some runs.

## Thumbnails of a 4K file — 2026-11-15

What it measures: `checks/thumbnails4k/Main.rae`, run by hand after
`sh tests/media/make.sh 4k`.

    ./rae run checks/thumbnails4k/Main.rae -- build/media/counter4k.mp4
    ./rae run checks/thumbnails4k/Main.rae -- build/media/counter4k.mp4 keyframes

100 thumbnails 160 px wide, of frames 0, 3, ... 297 of the 3840x2160 file, are
asked of `video/Thumbnails` at once; this thread then runs a 60 Hz loop taking
them as they come, as an editor's frame loop would, and times every call it
makes (asking, taking). The worker thread decodes each frame (the exact one, or
with `keyframes` the keyframe it is decoded from), converts it to RGBA and
scales it down on the GPU with a GPU manager of its own, and reads it back.
Every thumbnail's counter is read from it (its 16 cells are 10 px each at
160 px) and is the frame it says it shows. Apple M1 Max, macOS 27.0.

| file | kind | all 100 made in | each, on the worker | longest call on the calling thread | refreshes woken over 1 ms late |
|---|---|---|---|---|---|
| H.264 | exact | 1533 ms | 15 ms | 3 µs | 2 of 92 |
| H.264 | exact | 1250 ms | 12 ms | 14 µs | 0 of 75 |
| HEVC | exact | 1383 ms | 13 ms | 2 µs | 0 of 83 |
| HEVC | exact | 1250 ms | 12 ms | 3 µs | 0 of 75 |
| H.264 | keyframe | 1083 ms | 10 ms | 3 µs | 0 of 65 |
| HEVC | keyframe | 866 ms | 8 ms | 1 µs | 0 of 52 |

All 600 thumbnails showed the frame they say. The calling thread never waits:
asking and taking are channel operations of a few microseconds (the bar was
1 ms). The two late wakes of one run are the OS scheduling the loop's sleep,
not a call (the longest call of that run was 3 µs). Most of a thumbnail's time
is the 4K frame's conversion and the wait for the readback on the worker; a
keyframe thumbnail saves the decode of the frames between the keyframe and the
frame.
