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

## The 4K export and its readback (decides §12 V3 for export) — 2026-11-15

What it measures: `checks/export4k/Main.rae`, run by hand after
`sh tests/media/make.sh 4k`.

    ./rae run checks/export4k/Main.rae

`build/media/counter4k.mp4` (3840x2160, 300 frames at 29.97 fps) is decoded,
converted to RGBA on the GPU (both waited for: "decode + convert") and exported
to a MOV through `VideoEncoder.encodeTexture` and `MediaWriter`, once per
encoder. `encodeTexture`'s steps, timed inside it (`lastPassNs` ...
`lastHandOffNs`):

- **pass + copy**: the GPU pass that packs the frame (BGRA for VideoToolbox,
  planar 4:2:2 for DNxHR) and the copy into the readback buffer, waited for;
- **map**: mapping the readback buffer;
- **copy to a list**: the mapped bytes into the encoder's list (33 MB of BGRA,
  16.6 MB of 4:2:2);
- **hand-off**: the call into the encoder. For VideoToolbox, the glue copies
  the frame into a pixel buffer from the session's pool and queues it, and the
  call waits when VideoToolbox's queue is full. FFmpeg encodes on the caller's
  thread, so for DNxHR this is the whole encode.

Apple M1 Max, macOS 27.0. Means in ms (p95 in brackets), with the list made
once per encoder (video 0.9.1). Three runs agreed within 0.4 ms a frame (ProRes
7.78 to 8.18 ms); the table is the second:

| encoder | export per frame | decode + convert | pass + copy | map | copy to a list | hand-off |
|---|---|---|---|---|---|---|
| H.264 (VideoToolbox) | 10.54 | 5.11 (9.24) | 1.08 (1.18) | 0.003 | 0.72 (0.82) | 3.19 (5.77) |
| HEVC (VideoToolbox) | 10.53 | 5.05 (9.12) | 1.06 (1.15) | 0.002 | 0.72 (0.81) | 3.26 (5.74) |
| ProRes 422 HQ (VideoToolbox) | 7.93 | 5.10 (9.03) | 1.12 (1.25) | 0.002 | 0.72 (0.79) | 0.83 (0.89) |
| DNxHR HQ (FFmpeg) | 40.15 | 5.29 (9.53) | 3.16 (3.62) | 0.003 | 0.41 (0.42) | 29.61 (30.47) |

Before this measurement, `encodeTexture` made a new 33 MB list for every frame.
That made "copy to a list" 1.35 ms, and ProRes 422 HQ took 8.61 ms a frame.
H.264 and HEVC took the same 10.5 ms a frame either way: the time saved moved
into the hand-off, waiting for VideoToolbox.

What it says:

- **The readback is not what limits an export.** Mapping costs nothing on
  unified memory. The two CPU copies an IOSurface-backed texture would save, the
  list (0.72 ms) and the pixel buffer (about 0.8 ms, ProRes's hand-off), come
  to about 1.5 ms of a 4K frame. H.264 and HEVC run at about 95 frames a second
  (3.2 times real time at 29.97 fps), held back by VideoToolbox's encoder: the
  frames would wait for it anyway. ProRes 422 HQ runs at about 125 frames a second,
  and the most zero-copy could save there is about 20%.
- **The largest step is the source's decode and conversion** (5 ms, waited for
  in this check). An export of a timeline would decode ahead on worker threads
  (the frame pipeline), which matters more than the copies.
- **DNxHR is slow, but not because of its input.** At 40 ms a frame (0.8 times
  real time), 30 ms is FFmpeg's encode on one thread: the glue sets no
  `thread_count` for encoders (it does for decoders). Its fix is FFmpeg's
  threads, not zero-copy: done in ffmpeg 0.3.0 (the next section).

Decision for §12 V3: export keeps the readback. Zero-copy (an IOSurface-backed
texture imported into WebGPU, which `webgpu.h` cannot do) would save at most
about 1.5 ms of a 4K frame, and nothing for H.264 or HEVC. Not measured: an
iPhone, 10-bit sources and ProRes 4444 (64-bit BGRA would double the copies),
and an export that decodes ahead.

## FFmpeg's encoders on threads — 2026-11-15

What it measures: the same `checks/export4k/Main.rae`, which now also exports
FFV1 (4:2:0, 12.4 MB read back a frame). packages/ffmpeg 0.3.0 opens its
encoders with `thread_count` 0 (one thread per core) and both slice and frame
threads. FFmpeg uses whichever the codec has: DNxHR encodes several frames side
by side, and FFV1 encodes the slices of a frame side by side, so FFV1 is now
version 3 with 24 slices. Frame threads hold packets back a few frames. The
glue already queues whatever comes out, and `finish` flushes the rest, so the
writer sees nothing different. Apple M1 Max (10 cores), macOS 27.0, means in ms
(p95 in brackets):

| encoder | threads | export per frame | hand-off (the encode on the caller's thread) | real time at 29.97 fps |
|---|---|---|---|---|
| DNxHR HQ | one | 41.34 | 30.36 (31.34) | 0.8 times |
| DNxHR HQ | per core | 11.51 | 0.61 (0.65) | 2.9 times |
| FFV1 | one | 36.33 | 27.17 (28.81) | 0.9 times |
| FFV1, 24 slices | per core | 13.85 | 5.03 (6.05) | 2.4 times |

ffprobe counts 300 frames in both threaded files, and ffmpeg decodes them
without an error. tests/writer still reads its DNxHR and FFV1 files back with
every counter in order.

What it says:

- **DNxHR now exports almost as fast as H.264** (11.5 against 10.5 ms a
  frame), and its encode hardly holds up the caller (a 0.6 ms hand-off against
  H.264's 3 ms). What is left is the source's decode and conversion (5.5 ms)
  and the planar pass (3.2 ms).
- **FFV1 slices are encoded in parallel within a frame only**, so the caller
  still waits 5 ms a frame for it. That is enough for a lossless master at 2.4
  times real time.
- **The planar pass is 3.2 ms, against 1.1 ms for BGRA**, although it writes
  less. It packs a 4-byte word per invocation, reading 4 to 8 texels for it;
  that is the next thing to look at if the planar export matters.
