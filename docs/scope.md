# What lives here

Decided 2026-11-14. Rae takes only engine-level work (the language, `lib`,
engine-level audio in `packages/audio`). Video is a set of Rae packages in
this public repository instead of `packages/video` in Rae (which Rae's
`docs/video-design.md` §12 V2 decided on 2026-11-11, before this rule).

| where | what |
|---|---|
| here, `packages/video` | V2 Media API (`MediaReader`, `VideoDecoder`, `VideoEncoder`, `MediaWriter`) and its backend table; V1 shims for the platforms (VideoToolbox/AVFoundation first, then Media Foundation, MediaCodec, VA-API/NVDEC, WebCodecs) and permissive libraries (dav1d, libvpx, OpenEXR); V3 frame pipeline; the V4 passes a player needs; V6 a simple player |
| here, `packages/ffmpeg` | FFmpeg built as LGPL, dynamically linked, as one more V1 backend; opt-in |
| Rae | `lib/media` (MP4/MOV boxes, `MediaTime`), `lib/webgpu` and the render graph, `lib/color` if the renderer uses it too, `packages/audio` (the master clock) |
| `mediaeditor` | the editor's timeline (V5), grading and compositing UI, its own image graph |

The V-layers are those of Rae's `docs/video-design.md` §5.
