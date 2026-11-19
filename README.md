# raeVideo

Video for [Rae](https://github.com/jonasmyoha/rae) programs: reading and
writing video files, decoding and encoding on the platforms' hardware (and
FFmpeg where they fall short), a frame pipeline onto the GPU, and playback
synced to Rae's audio engine. MIT, except FFmpeg (LGPL, in its own package).

The design is [docs/design.md](docs/design.md); what lives here,
[docs/scope.md](docs/scope.md).

## Packages

- `packages/video` (0.14.0): MP4 and MOV reading (`MediaReader`), decoding
  through VideoToolbox on Apple (H.264, HEVC, ProRes: `VideoDecoder`,
  `FrameStream`) or a registered backend, a frame on the GPU as RGBA
  (`YuvPicture`), 10-bit frames as linear light in rgba16float
  (`LinearPicture`), the frame pipeline (`FramePipeline`: decode-ahead on worker
  threads for several files, a GPU frame cache with a budget, scrubbing),
  thumbnails made on a worker thread (`Thumbnails`), encoding through
  VideoToolbox (`VideoEncoder`: H.264, HEVC, ProRes), AAC through
  AudioToolbox (`AacEncoder`) and MOV / MP4 writing (`MediaWriter`, its boxes from
  Rae's `media/Mp4Writer`), and a simple player on Rae's audio clock
  (`VideoPlayer`). It depends on Rae's `audio` package 0.26.0.
- `packages/ffmpeg` (0.8.0): FFmpeg 9.0.2, built LGPL-only from its pinned
  tarball the first time a program uses the package, loaded at run time, as
  one more decoder of the video package (DNxHD/DNxHR, VP8, VP9, FFV1, ProRes):
  `addFfmpegBackend(table:)`, and encoder (DNxHR LB and HQ, FFV1; on one thread per core):
  `addFfmpegEncoders(table:)`. A program that uses it ships FFmpeg's two
  libraries and the files in its build's `legal/` folder
  (`packages/ffmpeg/legal/NOTICE.md`): `packages/ffmpeg/tools/bundleFfmpeg.sh`
  puts them in a macOS app, whose `Contents/Frameworks` the glue loads from
  first, and `checkFfmpegInApp.sh` checks the app (no GPL or nonfree, shared,
  separate files). macOS; never iOS.

## Examples

- `examples/simplePlayer`: one video file in a Rae UI window, an Open...
  button, a play/pause button and a seek bar (its README has the 4K numbers):
  `make run-player FILE=clip.mp4` (or `make run`; without `FILE` it asks for
  a file).
- `examples/ffmpegApp`: the DNxHR test file decoded through packages/ffmpeg
  and shown on the GPU, the program `tools/checkFfmpegBundle.sh` puts in a
  macOS app with FFmpeg in its `Contents/Frameworks` and runs with the
  package's build gone.

A program takes a package by git at a release tag:

```
dep video: {
  git: "https://github.com/jonasmyoha/raeVideo.git"
  subdir: "packages/video"
  version: "0.14.0"
  hash: "git-tree:14551495ecf7e9250911ac75bf752a645a10209f"
}
dep ffmpeg: {
  git: "https://github.com/jonasmyoha/raeVideo.git"
  subdir: "packages/ffmpeg"
  version: "0.8.0"
  hash: "git-tree:88c7d7680266413ae6ef92b326a650e8f9c1816f"
}
```

The `video/v*` and `ffmpeg/v*` tags, and the `audio/v0.15.1` tag the video
package needs from Rae, must be pushed before another machine can fetch them.

## Building and testing

`./rae` runs the Rae this repository pins. `make test` runs the test
programs in `tests/`; `RAE_ROOT=<a Rae checkout> make test` uses that Rae.
Media files are in Git LFS: `git lfs install` before cloning. The test media
is made by `tests/media/make.sh` (macOS: Apple's encoders), the 4K files for
the measurements in `checks/` (docs/measurements.md) by
`sh tests/media/make.sh 4k`.
