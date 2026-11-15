# Video for Rae — research and design

Status: **research and design, no code yet** (written 2026-11-11 in the Rae
repository as `docs/video-design.md`, moved here 2026-11-14). This document
chooses the video technology (codecs, containers, libraries and their
licenses), how decoded frames reach the WebGPU renderer, the color pipeline for
grading (ACES, matching DaVinci Resolve), the timeline model and its clock
shared with audio, and the order of the work. §12 lists the decisions still
open.

**Where it lives.** Video is not part of the Rae repository. It is this
repository's packages, which Rae programs take as dependencies
([`scope.md`](scope.md)):

- `packages/video`: the Media API (V2) and its backend table, the platform and
  permissive-library backends (V1), the frame pipeline (V3), the image passes a
  player needs (V4: YUV to RGB, the output transform) and a simple player (V6).
- `packages/ffmpeg`: FFmpeg built as LGPL, one more V1 backend, opt-in (§4).
- **Rae** keeps what the engine and other programs share: `lib/media` (MP4/MOV
  boxes, `MediaTime`), `lib/webgpu` and the render graph, `lib/color` (§8.2),
  and `packages/audio`, the master clock.
- **`mediaeditor`** holds the editor: the timeline (V5), the grading and
  compositing UI, its own image graph, export of a timeline.

Rae's `docs/video-design.md` is now a pointer here. The section numbers are
the same as there, so a citation in Rae such as "video-design.md §6.3" means
§6.3 of this document.

The goal is the same stack for three programs: a video player, a video editor
at the level of Premiere / Resolve's edit page, and a color-grading tool that
matches Resolve's numbers. Platforms: macOS, Windows, Linux, iOS, Android, and a
lighter web build.

Companion documents, all in the Rae repository:
[`audio-design.md`](https://github.com/jonasmyoha/rae/blob/main/docs/audio-design.md)
(the audio engine, whose clock video follows),
[`color-management-plan.md`](https://github.com/jonasmyoha/rae/blob/main/docs/color-management-plan.md)
(the color principles; its "Metal backend" phase is out of date, because the
renderer is now WebGPU through `lib/webgpu`),
[`tech-stack-and-dependencies.md`](https://github.com/jonasmyoha/rae/blob/main/docs/tech-stack-and-dependencies.md)
(the dependency policy and its first "FFmpeg / VideoToolbox / WebCodecs" note),
[`raepack-v2-and-packages.md`](https://github.com/jonasmyoha/rae/blob/main/docs/raepack-v2-and-packages.md)
(packages with their own C). Below, a bare `audio-design.md` or `AGENTS.md`
means Rae's.

---

## 1. The short answer

- **Decode and encode through the platform first:** VideoToolbox and
  AVFoundation on Apple, Media Foundation on Windows, `MediaCodec` on Android,
  VA-API / NVDEC / NVENC on Linux, WebCodecs in the browser. These are the fast
  (hardware) paths, and they carry the patent licenses for H.264, HEVC and AAC.
- **FFmpeg, built as LGPL, in its own package (`packages/ffmpeg` here)**, for
  everything the platform cannot do: more containers and codecs on the desktop,
  ProRes and DNxHR on Windows and Linux, software fallback. A program that
  imports the package takes on the LGPL duties; a program that does not, never
  ships FFmpeg.
- **Permissive (MIT / BSD) libraries where they are the best tool:** dav1d
  (AV1 decode), libvpx (VP8/VP9), SVT-AV1 or rav1e (AV1 encode), OpenEXR
  (image sequences). There is **no permissive library that replaces FFmpeg** for
  H.264 High / HEVC / ProRes decoding at editing quality (§3.3).
- **Containers in Rae:** the MP4 / MOV box parser and writer is Rae code in
  Rae's `lib/media` (bounded spec, shared with audio's M4A), so the platform decoders and WebCodecs get
  their packets from our own demuxer.
- **Pictures are drawn by our WebGPU renderer.** Decoded frames become textures;
  YUV to RGB, color transforms, grading, compositing, scopes and the output
  transform are WGSL shaders. ACES 2.0 is written in WGSL from the Academy's
  reference and tested against OpenColorIO.
- **Audio is the master clock**, time is rational (never `Float` seconds), and
  the timeline is ECS data.

---

## 2. What was used before, and what it teaches

- **libquicktime** (LGPL, C) was a Linux library with a QuickTime-style API over
  codec plugins, most of which were libavcodec (FFmpeg) underneath. Its last
  release was in 2012 and it is not maintained. Lesson: a wrapper over FFmpeg
  with its own API is the right shape; that wrapper is now our Media API.
- **FFmpeg used directly** (`libavformat`, `libavcodec`, `libswscale`,
  `libswresample`): the broadest support of any library, the API every tool
  uses, LGPL 2.1+ when built without `--enable-gpl` and `--enable-nonfree`.
  Lessons: its API changes between major versions (one wrapper, one place to
  update); `libswscale` is a CPU converter and we do that on the GPU instead;
  hardware decode through `hwaccel` exists but platform APIs used directly give
  better control of zero-copy frames.
- **GStreamer** (LGPL, C): a pipeline framework that wants to own the graph and
  the threads. Rejected for the same reason as JUCE: it imposes its architecture.
- **Professional editors** (Resolve, Premiere, Final Cut): platform hardware
  decode for long-GOP camera files, their own decoders for intra codecs
  (ProRes, DNx), GPU processing in 16-bit or 32-bit float, a frame cache,
  "optimized media" or proxies for heavy footage, and an export path that runs
  the same graph offline.

---

## 3. Codecs, libraries and licenses

### 3.1 The rule

The same rule as audio (`audio-design.md` §7.3, AGENTS.md "Runtime C rule"):
Rae for bounded specs, a proven library for hard domains, the platform when it
is faster or the only legal choice; all of it behind one Rae API, so no program
sees which one ran.

Two things differ from audio:

- **Patents.** H.264, HEVC and AAC are patented in many countries, and the
  license pools charge per device for encoders and decoders. FFmpeg's own legal
  page says the project has not checked patents, and that standards such as
  H.264 may be covered. The LGPL gives no patent license. The platform decoders
  are licensed by the platform vendor. So for H.264 and HEVC, **the platform is
  the default, and FFmpeg's software decoder is a fallback a program chooses**.
- **Royalty-free codecs** (AV1, VP8, VP9, Opus) come with a patent grant
  (AOMedia's for AV1) and permissive libraries.

### 3.2 The table

| codec / format | decode | encode | license of what we use |
|---|---|---|---|
| H.264 (AVC) | platform; FFmpeg fallback | platform (VideoToolbox, Media Foundation, MediaCodec, NVENC/VA-API, WebCodecs) | OS; FFmpeg LGPL. **Not x264** (GPL). |
| HEVC (H.265) | platform; FFmpeg fallback | platform | OS; FFmpeg LGPL. **Not x265** (GPL). Kvazaar (BSD-3) only if a software encoder is needed. |
| AV1 | **dav1d** (BSD-2, C + assembly, the fastest); platform where hardware has it | **SVT-AV1** (BSD-3-Clause-Clear + AOM patent license) or **rav1e** (BSD-2, Rust with a C API) | permissive |
| VP8 / VP9 | **libvpx** (BSD-3); platform | libvpx | permissive |
| ProRes | Apple: VideoToolbox; elsewhere FFmpeg (`prores`) | Apple: VideoToolbox; elsewhere FFmpeg (`prores_ks`) | OS; LGPL |
| DNxHD / DNxHR | FFmpeg | FFmpeg | LGPL |
| Motion JPEG, image sequences (PNG, JPEG, TIFF, EXR) | lodepng (have it), libjpeg-turbo (BSD/IJG/zlib), libtiff (BSD-style), **OpenEXR** (BSD-3) | same | permissive |
| Camera RAW (BRAW, R3D, ARRIRAW, ProRes RAW) | vendor SDKs (free, proprietary); ProRes RAW through Apple | — | later, each its own package |
| Audio in video files (AAC, PCM, Opus) | `audio-design.md` §7.3 | same | — |

**OpenH264** (BSD-2) decodes and encodes H.264, but mainly the Constrained
Baseline profile, and its patent coverage applies only to Cisco's prebuilt
binary downloaded at run time. It is fine for video calls, not for editing
camera footage. Not chosen.

### 3.3 "Is there MIT work that could replace FFmpeg?"

For the royalty-free codecs, yes: dav1d, libvpx, SVT-AV1 and rav1e are BSD,
fast and maintained (dav1d and libvpx are what browsers use). For the codecs an
editor meets most (H.264 High 10 / 4:2:2 from cameras, HEVC, ProRes, DNxHR),
**no**: the only full, fast open implementations are FFmpeg's (LGPL) and the
platforms'. Writing H.264 or HEVC decoding in Rae fails the "small and
controllable" litmus by a wide margin (and the patents remain). So the stack is
platform + permissive libraries by default, with FFmpeg as the optional package
that fills the gaps.

### 3.4 Containers

| container | first implementation | why |
|---|---|---|
| MP4 / MOV (ISO BMFF, QuickTime) | **Rae** reader and writer | the main one; bounded spec (boxes); WebCodecs and the platform decoders need packets from a demuxer anyway; shared with audio's M4A |
| WebM / Matroska | **Rae** reader (EBML), writer later | bounded; libwebm (BSD-3) is the fallback if ours lags |
| MXF, AVI, MPEG-TS, and everything else | FFmpeg package | long tail; not worth owning |

The demuxer gives packets with **rational timestamps** (§6.1), keyframe flags,
and the codec configuration (avcC / hvcC / av1C) the decoders need.

**Landed (2026-11-12), in Rae: the MP4 / MOV reader, `lib/media/Mp4.rae`**, written
with audio's AAC (`audio-design.md` §11 step 2). Per track: the handler,
codec, picture size, codec configuration (avcC / hvcC / av1C), sample sizes,
file offsets, decode times, composition offsets (B-frames), sync samples
(keyframes) and the edit; all in the track's timescale, Int. Checked against
ffprobe on an H.264 clip with B-frames (fixture 1414). Not yet: fragmented MP4
(moof / traf), the `colr` tags, and the packet type with `MediaTime` (§6.1),
which comes with audio step 4.

**Landed (2026-11-15), in Rae: the MP4 / MOV writer, `lib/media/Mp4Writer.rae`**,
beside the reader: pure functions that build ftyp, the moov (mvhd; per track
tkhd, the edit, mdia and the sample table with co64 offsets) and the sample
entries (avc1 / hvc1 / ProRes / DNxHD / FFV1 with colr, mp4a with esds and its
roll group, sowt PCM). `MediaWriter` (§11 step 4) writes the file with them;
Rae's case 1471 reads what they write back with `media/Mp4`.

---

## 4. FFmpeg as a separate package

How it fits the package system (AGENTS.md "A package with its own C", Rae's
`packages/sql` is the model): **`packages/ffmpeg` in this repository**, a
package of its own beside `packages/video` (decided 2026-11-14, §12 V1):

- The package holds the Rae bindings and glue (MIT, ours), a build script,
  and the pin of one FFmpeg release (version + source tarball hash). It does
  **not** hold FFmpeg's source, and neither does this repository; the build
  fetches the tarball and checks the hash.
- The pack's `native` block builds it on first need into
  `~/.rae/packages/ffmpeg/<pin>/install` with:
  `--disable-gpl --disable-nonfree --enable-shared --disable-static
  --disable-programs --disable-doc`, plus an explicit list of decoders,
  encoders, demuxers and muxers (smaller binary, and the program states which
  patented software codecs it ships).
- A program uses it with `dep ffmpeg: { git: "<this repository>", subdir:
  "packages/ffmpeg", version: "…", hash: "…" }` (the tag `ffmpeg/v<version>`)
  and `import ffmpeg/...`. It plugs into the Media API as one more backend
  (§5), so the program's code is the same with or without it. `packages/video`
  does not depend on it; a program that does not declare it never builds or
  links FFmpeg.

What a program that ships the package must do (FFmpeg's own LGPL checklist; not
legal advice):

1. link FFmpeg **dynamically** (`.dylib`, `.dll`, `.so`), so the user can put in
   their own build;
2. ship the LGPL text and a notice that it uses FFmpeg, and offer the exact
   FFmpeg source it used (the pinned tarball and the configure line);
3. not use `--enable-gpl` or `--enable-nonfree`;
4. think about codec patents for the countries it ships in (§3.1).

**Landed 2026-11-14: `packages/ffmpeg` 0.1.0.** FFmpeg 9.0.2 (SHA-256 pinned
in `native/build.sh`), LGPL 2.1 or later, built shared with the decoders
DNxHD/DNxHR, ProRes, VP8, VP9 and FFV1, the encoders DNxHD and FFV1, the MOV,
Matroska and MXF demuxers and the MOV and Matroska muxers, and nothing found on
the machine (`--disable-autodetect`). Rae has no function values, so the
backend seam is C: the package hands a struct of its decoder's calls
(`RaeVideoBackendCalls`, `packages/video/native/rae_video.h`) to the video
package from Rae (`addFfmpegBackend`), and neither package's C names the
other's symbols. Its glue loads libavcodec and libavutil with `dlopen` from the
package's build (or `RAE_FFMPEG_LIBRARY_DIR`), so no program links FFmpeg
(`tools/checkNoFfmpeg.sh`). It decodes packets from our own demuxer; reading
WebM / MXF through its demuxers is not wired yet. Frames come as three planes,
which `YuvPicture` converts with `yuvPlanarToRgb.wgsl` (8-bit; 10-bit DNxHR
HQX and VP9 profile 2 are not shown yet).

**Then, 2026-11-15: `packages/ffmpeg` 0.2.0** adds its encoders: DNxHR LB and HQ
(the dnxhd encoder, 4:2:2) and FFV1 (lossless, given 4:2:0), plugged into
`VideoEncoder` the way its decoders plug into `VideoDecoder`: a second struct
of calls (`RaeVideoEncoderCalls`), a Rae table of which backend encodes which
codec (`EncoderBackend`), the frame converted to planar Y'CbCr on the GPU
(`rgbaToPlanar.wgsl`) and read back. FFV1's extradata goes in MOV's `glbl` box,
which `MediaReader` reads back.

Platform notes:

- **iOS:** an LGPL dynamic library inside an App Store app is legally debated
  (the user cannot easily replace it). Recommendation: **do not ship FFmpeg on
  iOS**; VideoToolbox, AVFoundation and dav1d cover what an iOS editor needs.
- **Web:** no FFmpeg (`ffmpeg.wasm` is slow and large); WebCodecs + our
  demuxer.
- **Android:** `MediaCodec` covers the common codecs; FFmpeg only if a
  program asks.

Why a package of its own in this repository: the build fetches FFmpeg's source,
so no repository holds it, and the Rae repository does not even hold the glue
(video is not engine-level work, `scope.md`). A separate package keeps its
build script, its release (`ffmpeg/v<version>`, apart from `video/v<version>`)
and its license notices to itself, and makes it clear that using it is a choice
with duties. One repository for both keeps the backend and the Media API it
plugs into changing together. (Before the move, the plan was a third
repository just for FFmpeg; §12 V1.)

---

## 5. The layers

Mirrors the audio layers. A program uses the top two.

```
  Program (player, editor, grading tool)
  ───────────────────────────────────────────────────────────────────────
  V6  Simple API        open a file, play/pause/seek, draw it    (Rae)
  V5  Timeline (ECS)    tracks, clips, transitions, grades       (Rae)
  V4  Image graph       YUV→RGB, color transforms, grade,        (Rae + WGSL on
                        composite, scopes, output transform       lib/webgpu)
  V3  Frame pipeline    decode-ahead, frame cache, GPU upload    (Rae, worker threads)
  V2  Media API         demux, decode, encode, mux, one Rae API  (Rae + backends)
  V1  Backends          VideoToolbox/AVFoundation · Media Foundation ·
                        MediaCodec · VA-API/NVDEC/NVENC · WebCodecs ·
                        dav1d · libvpx · SVT-AV1 · OpenEXR · [ffmpeg package]
```

- **V1** backends are one-call shims (platform ABI) or vendored libraries used as
  libraries. No policy in C.
- **V2** is the Rae-owned seam: `MediaReader` (open, tracks, packets, seek),
  `VideoDecoder` (send packet, receive frame, flush), `VideoEncoder`,
  `MediaWriter`. Backends are chosen per codec per platform by a Rae table that
  a program can reorder (for example "prefer the FFmpeg ProRes decoder on
  Linux").
- **V3** decodes ahead on worker threads, keeps a frame cache sized in GPU
  memory, and serves "the frame at time t".
- **V4** is ordinary WebGPU passes in the existing render-graph style (Rae's
  `lib/Rendergraph.rae`).
- **V5** is the edit, as entities and components.

Where each layer lives (`scope.md`): V1 to V3 in `packages/video` (FFmpeg's
backend in `packages/ffmpeg`); of V4, the passes a player needs (YUV to RGB,
the output transform) in `packages/video`, built on Rae's `lib/color`, and the
grading and compositing graph in `mediaeditor`; V5 in `mediaeditor`; V6 in
`packages/video`.

---

## 6. Time, seeking and sync with audio

### 6.1 Rational time

Video time is never a `Float`. A time is an `Int` count of ticks in a stated
timebase (`MediaTime { value: Int, timebase: Rational }`), as in FFmpeg's
`AVRational`, QuickTime's time scales and OpenTimelineIO's `RationalTime`.
29.97 fps is exactly 30000/1001; 23.976 is 24000/1001. A conversion between
timebases rounds once, by a stated rule. The audio sample time
(`audio-design.md` §6.1) is the same kind of value with timebase 1/sampleRate.
Timecode (including drop-frame) is a display format over this.

**Landed (2026-11-12), in Rae: `lib/media/MediaTime.rae`** (`Rational`, `MediaTime`,
rescaling rounded once to the nearest with a half away from zero, exact
comparison), with audio's time step (`audio-design.md` §11 step 4). The audio
engine's sample time is one, and `audio.heardSampleTimeAt(hostNs)` is the hook
§6.3 needs. Not yet: timecode display, and MP4 packets carrying MediaTime
(media/Mp4 still gives Int ticks with the track's timescale beside them).

### 6.2 Frame-accurate seek and scrubbing

Camera codecs are long-GOP: a frame can only be decoded from the keyframe
before it. Seeking to frame n: find the keyframe at or before n in the
container's index, decode forward, keep only n. The frame pipeline keeps the
decoded GOP around the playhead, so stepping one frame forward or back is a
cache hit. Reverse play decodes each GOP forward and serves it backward.
Scrubbing asks for "the nearest frame that can be shown fast" first and the
exact frame after.

When the footage is too heavy for this (4K HEVC 10-bit, many layers), the
editor makes **proxies / optimized media**: an intra-frame copy (ProRes Proxy
via VideoToolbox on Apple, DNxHR LB or ProRes through the FFmpeg package
elsewhere) at a smaller size, swapped in for editing and swapped out for export.
That is how Resolve and Premiere stay fluid.

### 6.3 Playback sync

Audio is the master clock (`audio-design.md` §6.5). On each display refresh,
the player asks the audio clock which media time is being heard at the refresh's
presentation host time, and shows the frame whose time span covers it. When the
video falls behind, frames are dropped; ahead, repeated. Video never adjusts the
audio. Variable frame rate files (phones) are handled by the same rule, because
each frame has its own timestamp.

### 6.4 Export

Export runs the same graph with no device and no display: for each output frame
time, render the image graph, read back or hand the texture to the encoder (a
GPU texture goes straight to VideoToolbox / Media Foundation / NVENC where the
platform allows it); the audio engine renders offline in the same loop by sample
time; the muxer interleaves both. Export is deterministic, which is also how it
is tested (§10).

---

## 7. From a decoded frame to a texture

Decoders give YUV frames: NV12 (8-bit 4:2:0), P010 (10-bit 4:2:0), 4:2:2 and
4:4:4 variants, sometimes RGB (ProRes 4444, EXR).

- **Copy path (works everywhere, first):** upload the Y and UV planes as two
  textures (`r8unorm` + `rg8unorm`, or `r16unorm` + `rg16unorm` for 10-bit) and
  convert in a WGSL shader. At 4K 10-bit, 60 fps that is ~1.5 GB/s of uploads,
  which a desktop GPU handles; a phone may not.
- **Zero-copy path (later, per platform):** the decoder's own GPU surface used
  directly as a texture: IOSurface / `CVPixelBuffer` on Apple, DXGI shared
  handles on Windows, DMA-BUF on Linux, `AHardwareBuffer` on Android,
  `importExternalTexture` with a `VideoFrame` on the web. **`webgpu.h` has no
  standard way to import such a surface.** Dawn has it (`SharedTextureMemory`,
  `WrapIOSurface`); wgpu reaches it only from Rust (`create_texture_from_hal`).
  Rae's renderer uses the native `lib/webgpu` bindings, so this needs either a
  small native extension in Rae's WebGPU build (a Rae change, since
  `lib/webgpu` is Rae's) or a switch of implementation for this feature. It is an open decision (§12 V3) and the first measurement to
  make: is the copy path fast enough on a recent Mac and an iPhone?
- **The YUV to RGB shader** applies the frame's tagged matrix (BT.601, BT.709,
  BT.2020 non-constant), range (limited 16-235 or full), chroma siting and
  upsampling, and then the transfer function to linear (§8). Every frame
  carries these tags from the container / bitstream (`colr` box, VUI); an
  untagged file gets a documented guess (HD = BT.709).

---

## 8. Color: ACES, and matching Resolve

### 8.1 The pipeline

Follows `color-management-plan.md`: linear light inside, every image tagged with
its color space, `rgba16float` working textures (`rgba32float` where a grade
needs it).

```
  decoded frame (YUV, tagged)
    → YUV→RGB, transfer → linear, in the source gamut          (Input Transform / IDT)
    → working space: ACEScg (AP1, linear) for compositing,
                     ACEScct (log) for grading operations
    → grade: CDL, lift/gamma/gain/offset, curves, qualifiers, 3D LUTs
    → composite (titles, layers, transitions)
    → Output Transform: ACES 2.0 for SDR Rec.709, Display P3, HDR PQ/HLG
    → display (and scopes from the same pixels)
```

### 8.2 What is written in WGSL, and from which source

| piece | source to implement from | license / note |
|---|---|---|
| ACES 2.0 Output Transforms (and ACES 1.3 for older projects) | the Academy's reference implementation (`aces-dev` / the ACES 2.0 CTL) | permissive Academy license (check its attribution clause); a WGSL port gets the one-line "Ported from" notice |
| camera input transforms (ARRI LogC3/4, Sony S-Log3, Canon Log, Panasonic V-Log, Apple Log, Blackmagic Film Gen5) | the vendors' published white papers | formulas, no code |
| DaVinci Wide Gamut / DaVinci Intermediate | Blackmagic's published white paper | formulas; needed to match Resolve's own color-managed mode |
| ASC CDL (slope, offset, power, saturation) | the ASC CDL specification | public |
| 1D / 3D LUTs, `.cube` files (Resolve / Adobe format), tetrahedral interpolation | the `.cube` format description | the 3D LUT is a 3D texture; tetrahedral sampling in the shader |
| HDR: PQ (ST 2084), HLG (BT.2100) | ITU / SMPTE specs | public |

**Where the color code lives. Decided 2026-11-14: `lib/color` stays in Rae.**
The renderer and the UI need it too: Rae's `color-management-plan.md` makes
color-space tags, linear light, sRGB / Display P3 and HDR output the base of
every program's drawing, not only of video. So Rae's `lib/color` holds the
color science and its WGSL: the color spaces and gamut matrices, the transfer
functions (sRGB, PQ, HLG, and the camera log curves above, which are formulas
over a color space), 1D / 3D LUTs and the `.cube` reader, the ACES 2.0 output
transforms, and the OCIO-oracle tests (§8.3). This repository uses it:
`packages/video` holds the YUV to RGB pass (§7, it reads the decoded frame's
planes and tags) and calls `lib/color` for the input transform and the output
to the screen. `mediaeditor` holds the grade operations (CDL and the
lift / gamma / gain / curves controls, qualifiers), the Resolve match (§8.4)
and the scopes (§8.6). A color change the renderer does not need still goes in
`lib/color` if it is color science; one that is a grading control goes in
`mediaeditor`.

The existing `aces()` in Rae's `lib/deferred_composite.wgsl` is Narkowicz's curve
fit, a game tone curve, **not** ACES. It stays for games under its own name; the
video pipeline does not use it.

### 8.3 OpenColorIO: an oracle, not a dependency (at first)

OpenColorIO (BSD-3, C++) is the industry library for this, and since OCIO 2.4.2
(March 2025) it ships ACES 2.0 support, with ACES 2.0 built-in configs in 2.5.
It is C++ and would bring its own config system. The plan:

- **Tests:** OCIO is the oracle. A test tool (outside the shipped program) runs
  each transform in OCIO over a set of sample colors and stores the numbers; our
  WGSL must match within 1e-4 (half-float limits).
- **Later, optional:** an `ocio` package that loads a studio's OCIO config and
  bakes it into 3D LUTs our shaders apply, for programs that must follow a
  facility's config. Not needed to match Resolve's ACES mode.

### 8.4 "Match Resolve"

Matching means: the same input file, the same color-management settings, the
same grade numbers give the same output pixels within a small tolerance.
Resolve's two managed modes are both built from published transforms (ACES, and
DaVinci Wide Gamut / Intermediate), so they can be matched by implementing the
same transforms and checking against frames exported from Resolve. Its grade
controls (lift / gamma / gain, offset, contrast and pivot, curves) are not fully
documented; they are matched by measurement (grade a test chart in Resolve,
export, fit and compare), one control at a time. This is a step in §11, with
reference frames as fixtures.

### 8.5 Output to the screen

SDR displays: the output transform to sRGB / Rec.709 / Display P3 and a normal
surface. HDR (a Mac XDR display, an iPhone): an `rgba16float` surface in
extended linear Display P3 (EDR on Apple). Whether our WebGPU build exposes the
surface color space and tone-mapping mode on each platform must be checked
(§12 V4). A grading tool also wants a calibrated external monitor through a
video output card (Blackmagic DeckLink SDK); that is late and its own package.

### 8.6 Scopes

Waveform, parade, vectorscope and histogram are compute shaders over the same
output texture (atomic counters into a buffer), drawn by the UI. Cheap on the
GPU and identical to what is exported.

---

## 9. The editor as ECS, and what a program sees

The editor is `mediaeditor`'s, not this repository's; this section is the
shape this repository's API must serve. What a player sees is the simple API
at the end, from `packages/video`.

- **Entities:** tracks, clips, transitions, effects, grade nodes, markers.
- **Components:** `MediaSource` (file, stream index), `ClipRange` (source in/out,
  timeline position, all `MediaTime`), `ClipSpeed`, `Transform2d`, `Opacity`,
  `BlendMode`, `GradeNodes`, `AudioLink` (the audio clip it moves with).
- **Systems** (each in its own folder, like the audio ones): `timelineSystem`
  (what is under the playhead → which frames to request), `decodeSystem`
  (schedules the frame pipeline, reads finished frames), `imageGraphSystem`
  (builds the per-frame pass list from the clips), `proxySystem`,
  `exportSystem`.
- **Resources on the App:** the `MediaEngine` (backends, worker threads, frame
  cache), the shared `AudioEngine`.
- **Interchange:** OpenTimelineIO (Apache-2.0, C++, with adapters for FCPXML,
  EDL and AAF) to move edits to and from Resolve and Premiere. The OTIO JSON
  format is simple enough to read and write in Rae; the adapters stay outside.

The simple API for a player (sketch; names settle at implementation):

```rae
open video/Video

var media: MediaEngine = mediaStart(audio: audio)
let movieId: MovieId = media.open(path: "clips/holiday.mov")
media.play(movieId: movieId)
media.seek(movieId: movieId, time: secondsTime(seconds: 12))
# Each frame: the texture to draw at this refresh, already in the display's
# color space.
let frame: VideoFrameView = media.frameForDisplay(movieId: movieId, displayTime: frameTime)
```

---

## 10. Testing, headless

- **Demuxers:** small sample files (a few frames each) per container; the packet
  list (timestamps, sizes, keyframe flags) compared with a stored list.
- **Decoders:** decode, hash or compare frames against references. Platform
  decoders can differ by a code value, so the compare is PSNR above a limit
  (e.g. 50 dB), not bit equality; dav1d and libvpx are bit-exact by spec.
- **Seek:** seek to many frames, compare with the frames from a full decode
  (must be identical: same decoder).
- **Color:** every transform over a fixed set of sample colors against OCIO's
  numbers (§8.3); YUV to RGB against reference conversions; LUT sampling against
  a CPU implementation in Rae.
- **Image graph:** render through Rae's GPU readback
  (`runtime_webgpu_readback.c`) and compare with reference images (the same
  tolerance rules as the renderer's tests).
- **Resolve match:** reference frames exported from Resolve with a fixed project
  and grade; our output within a tolerance (§8.4). A real-machine check, not in
  the unit suite.
- **Export round trip:** export a short timeline, decode it, compare with the
  frames rendered for it (PSNR), and check the audio with the audio engine's null
  test; check A/V sync by a test pattern (a flash and a click on the same frame).
- **Hardware checks** (in this repository): hardware decode and encode on each
  platform, the zero-copy path, frames per second at 4K.

The test media is generated by a script from simple math (color bars, a moving
square, a frame counter burned in, a tone), small, in Git LFS; never a
copyrighted clip (`AGENTS.md` here).

---

## 11. What lands first

Depends on the audio steps for its clock (audio 1/6 to 4/6).

1. **Media API and playback on Apple** (`packages/video`, the first release
   `video/v0.1.0`). **Landed 2026-11-14:** `MediaReader`, `VideoBackends`,
   `VideoDecoder` (VideoToolbox), `FrameStream`, `YuvPicture` (the copy path and
   `yuvToRgb.wgsl`), `VideoPlayer`; tests in `tests/` (demux against ffprobe,
   the frame counter, the bars through the GPU, the player against the audio
   clock), the 4K measurement in [measurements.md](measurements.md). **Then,
   2026-11-15:** 10-bit frames (HEVC Main10 and ProRes 422 as 16-bit samples,
   `FrameDepth.source`), bilinear chroma upsampling with the MPEG-2 siting, and
   the transfer to linear (BT.709 / BT.2020, PQ, HLG, sRGB) into rgba16float in
   the source's gamut: `LinearPicture` and `linearCommon.wgsl`, whose curves
   move to `lib/color` when it is written (§8.2). The display path
   (`YuvPicture`) stays 8-bit R'G'B'. `MediaReader` over Rae's MP4/MOV demuxer, VideoToolbox
   decode (H.264, HEVC, ProRes), the copy path to textures, the YUV to RGB
   shader with tags, rational time, a simple player synced to the audio engine;
   demux and decode tests. Measure the copy path at 4K (decides §12 V3).
2. **Color** (Rae's `lib/color`, §8.2): input transforms, ACEScg / ACEScct,
   `.cube` LUTs, ACES 2.0 output transforms in WGSL, OCIO-oracle tests, SDR
   output; `packages/video`'s player then outputs through it. CDL, the grade
   controls and the scopes are `mediaeditor`'s.
3. **The editor core** (frame cache and frame-accurate seek, scrub and reverse
   in `packages/video`'s frame pipeline, **landed 2026-11-15** as
   `FramePipeline`: decode-ahead on a worker thread per file, a GPU frame cache
   with a budget, scrub, the conversion recorded into the caller's recording;
   4K at 59.94 fps plays with no frame skipped, [measurements.md](measurements.md);
   and thumbnails, **landed 2026-11-15** as `Thumbnails`: a frame decoded,
   scaled down on the GPU and read back on a worker thread, keyframes or exact
   frames, the calling thread never waiting; the timeline ECS, proxies and the
   simple composite of layers, opacity, cross dissolve and titles in
   `mediaeditor`).
4. **Export.** VideoToolbox encode (H.264, HEVC, ProRes) and the MP4/MOV writer
   behind `VideoEncoder` / `MediaWriter` in `packages/video` (the box writing in
   Rae's `lib/media`); **landed 2026-11-15** (video 0.6.0): `VideoEncoder`
   (H.264, HEVC, ProRes 422 Proxy / LT / 422 / HQ / 4444, fed from an RGBA
   texture by a GPU pass and a readback, BT.709 tags), `MediaWriter` (MOV and
   MP4: AAC packets, 16-bit PCM in MOV, interleaved by time), the boxes in
   Rae's `lib/media/Mp4Writer.rae` (moved there with video 0.9.0); the round trip
   in `tests/writer`, ffprobe / ffmpeg / AVFoundation in
   `tools/checkWrittenFiles.sh`; the FFmpeg package's DNxHR and FFV1 encoders
   as backends of the same API (video 0.7.0, ffmpeg 0.2.0; on threads, ffmpeg 0.3.0); AAC through
   AudioToolbox (`AacEncoder`, video 0.8.0: here rather than in Rae's audio
   package, beside the writer an export uses), its priming the track's edit
   start, the frames fed its length, AAC's pre-roll written as a roll sample
   group as Apple does (without it AVFoundation takes the priming out twice).
   Input without the readback was measured and not done (2026-11-15,
   measurements.md: the copies are about 1.5 ms of a 4K frame, and the encoder
   limits H.264 and HEVC). Not yet: the offline loop with audio and the round-trip tests
   where the timeline is, in `mediaeditor`.
5. **The FFmpeg package** (`packages/ffmpeg`, `ffmpeg/v0.1.0`, **landed
   2026-11-14**, §4), dav1d, libvpx, the WebM reader.
6. **Other platforms.** Windows (Media Foundation, NVENC), Linux (VA-API, the
   FFmpeg package), Android (`MediaCodec`), the web (WebCodecs,
   `importExternalTexture`).
7. **Pro.** HDR output, the zero-copy paths, the Resolve match by measurement,
   OTIO interchange, camera RAW packages, an OCIO config package, video output
   cards.

---

## 12. Decisions still needed

- **V1 — the FFmpeg package repo. Decided 2026-11-14:** `packages/ffmpeg` in
  this repository, which is public (LGPL obligations are easier with the build
  recipe public), released as `ffmpeg/v<version>` (§4). Its codecs (0.1.0):
  only what the platforms lack (DNxHD/DNxHR, VP8, VP9, FFV1, and ProRes for
  where there is no VideoToolbox); FFmpeg's software H.264 / HEVC decoders are
  not built at all, so their patents stay out (§3.1). A program that needs
  them would get a second, opt-in build of the package.
- **V2 — where video and the permissive libraries live.** Decided 2026-11-11
  as `packages/video` in the Rae repository, like `packages/audio`
  (`audio-design.md` §13 D6). **Changed 2026-11-14:** Rae takes only
  engine-level work, so video is `packages/video` in this repository
  (`scope.md`), declaring Rae's `audio` package as its git dependency at a
  released version, pinned by its hash. dav1d, libvpx, OpenEXR (later
  SVT-AV1) are used as libraries, not rewritten, in the package's `native/`
  (copied, or downloaded by a pinned hash at build time like Rae's
  `packages/sql` does with SQLite when the source is large). FFmpeg (LGPL) is
  its own package here (V1).
- **V3 — zero-copy frames.** If the copy path is too slow on iPhone, the WebGPU
  implementation needs an external-texture import (Dawn has it, wgpu-native does
  not in `webgpu.h`). **Measured 2026-11-14 on an M1 Max
  ([measurements.md](measurements.md)):** the copy path costs about 2 ms per 4K
  frame (both planes) and the conversion about 2.5 ms, and 4K at 29.97 fps plays
  with no frame dropped on one thread. So on Apple Silicon Macs the copy path is
  enough for playback; the peaks come from the decode and the waiting, which the
  frame pipeline (V3 decode-ahead) addresses: with it, 4K at 59.94 fps plays
  with no frame skipped and the main thread's work is about 2.5 ms a refresh
  (measured 2026-11-15). For export too, the readback is not the cost: about
  1.5 ms of CPU copies in a 4K frame that VideoToolbox takes 10.5 ms to encode
  as H.264 (measured 2026-11-15), so export keeps it. Still open for the
  iPhone, 10-bit and 60 fps 4K, and many-layer editing.
- **V4 — HDR surfaces** in our WebGPU build (float surface, extended range,
  color space) on macOS and iOS.
- **V5 — `color-management-plan.md`** (Rae's). Its backend phases (a Metal
  backend) are out of date; update it to point at §8 here, or fold it into a
  `lib/color` design in Rae (`lib/color` stays in Rae, §8.2). A Rae task.
- **V6 — patents.** Shipping software H.264 / HEVC / AAC decoders or encoders in
  a commercial program needs a decision per program and country; the default of
  this design (platform codecs) avoids it.

---

## Sources

- FFmpeg license and legal page (the LGPL checklist, patents):
  https://www.ffmpeg.org/legal.html
- OpenColorIO 2.4 and 2.5 release notes (ACES 2.0 support):
  https://opencolorio.readthedocs.io/en/latest/releases/ocio_2_4.html,
  https://opencolorio.readthedocs.io/en/latest/releases/ocio_2_5.html
- Dawn's IOSurface import (`WrapIOSurface`):
  https://dawn.googlesource.com/dawn/+/06749f92b290507c97a98666bb83a6416efe0c1d/include/dawn/native/MetalBackend.h
- wgpu texture interop from Rust (no C API):
  https://docs.rs/wgpu-native-texture-interop/0.2.0/
