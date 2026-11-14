# raeVideo

Video for [Rae](https://github.com/jonasmyoha/rae) programs: reading and
writing video files, decoding and encoding on the platforms' hardware (and
FFmpeg where they fall short), a frame pipeline onto the GPU, and playback
synced to Rae's audio engine. MIT, except FFmpeg (LGPL, in its own package).

The design is [docs/design.md](docs/design.md); what lives here,
[docs/scope.md](docs/scope.md).

## Packages

- `packages/video` (0.1.0): MP4 and MOV reading (`MediaReader`), decoding
  through VideoToolbox on Apple (H.264, HEVC, ProRes: `VideoDecoder`,
  `FrameStream`), a frame on the GPU as RGBA (`YuvPicture`), and a simple
  player on Rae's audio clock (`VideoPlayer`). It depends on Rae's `audio`
  package 0.15.1.
- `packages/ffmpeg` (planned): FFmpeg (LGPL, dynamically linked) as one more
  backend.

A program takes a package by git at a release tag:

```
dep video: {
  git: "https://github.com/jonasmyoha/raeVideo.git"
  subdir: "packages/video"
  version: "0.1.0"
  hash: "git-tree:e35feff5392866848a906bb1d722b9456cb6ab52"
}
```

The `video/v0.1.0` tag and the `audio/v0.15.1` tag it needs from Rae must be
pushed before another machine can fetch them.

## Building and testing

`./rae` runs the Rae this repository pins. `make test` runs the test
programs in `tests/`; `RAE_ROOT=<a Rae checkout> make test` uses that Rae.
Media files are in Git LFS: `git lfs install` before cloning. The test media
is made by `tests/media/make.sh` (macOS: Apple's encoders), the 4K files for
`checks/copyPath4k` by `sh tests/media/make.sh 4k`.
