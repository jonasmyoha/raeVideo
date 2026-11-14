# raeVideo

Video for [Rae](https://github.com/jonasmyoha/rae) programs: reading and
writing video files, decoding and encoding on the platforms' hardware (and
FFmpeg where they fall short), a frame pipeline onto the GPU, and playback
synced to Rae's audio engine. MIT, except FFmpeg (LGPL, in its own package).

Early: there is no code yet. The design is Rae's `docs/video-design.md`,
moving here first (QUEUE.md).

## Packages (planned)

- `packages/video`: the Media API, the frame pipeline, a simple player.
- `packages/ffmpeg`: FFmpeg (LGPL, dynamically linked) as one more backend.

A program takes a package by git at a release tag:

```
dep video: {
  git: "https://github.com/jonasmyoha/raeVideo.git"
  subdir: "packages/video"
  version: "0.1.0"
  hash: "..."
}
```

## Building and testing

`./rae` runs the Rae this repository pins. `make test` runs the test
programs in `tests/`; `RAE_ROOT=<a Rae checkout> make test` uses that Rae.
Media files are in Git LFS: `git lfs install` before cloning.
