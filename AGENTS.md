# AGENTS.md — working in raeVideo

This repository is **Rae's video packages**: video for Rae programs, outside
the Rae repository. It is public (MIT). It follows the Rae repository's
rules (its `AGENTS.md`); the ones that matter most are repeated here.

## What lives here

Read `docs/scope.md` first. In short:

- `packages/video`: the Media API (demux, decode, encode, mux behind one Rae
  API), the frame pipeline (decode-ahead, frame cache, the GPU upload), the
  image passes a player needs (YUV to RGB, the output transform), and a
  simple player synced to Rae's audio engine.
- `packages/ffmpeg`: FFmpeg as a backend of the Media API, a separate package
  so a program that does not import it never ships FFmpeg.
- **Not here:** engine-level audio and the language's `lib` (Rae, including
  `lib/media` and `lib/webgpu`), the editor's timeline and grading UI
  (`mediaeditor`), Myoha's accounts and licenses (`myohaPlatform`).

## Licenses: the hard rule

- Our code is MIT. Libraries we link must be permissive (MIT, BSD, zlib,
  Apache-2.0, ISC, CC0, public domain) or Rae's own.
- **FFmpeg is the one LGPL exception** and only in `packages/ffmpeg`:
  configured with `--disable-gpl --disable-nonfree --enable-shared`,
  **dynamically linked**, its source fetched by a pinned tarball hash (never
  committed here), with the LGPL text and a notice shipped. Never on iOS.
  Never anything GPL or nonfree.
- Codec patents (H.264, HEVC, AAC): prefer the platforms' codecs, which carry
  their licenses; a program enables software patent-encumbered codecs
  knowingly.

## Video rules

- **Time is rational**, an Int in a stated timebase (`lib/media/MediaTime`),
  never Float seconds. **Audio is the master clock.**
- **Pictures are drawn on the GPU** by WGSL passes; the CPU does not touch
  pixels after decoding, except where a platform forces a copy.
  **Performance is the point:** every playback task measures frame time and
  dropped frames, and 4K must play without drops.
- Backends are one-call C shims over a platform's API or a library; policy
  (which backend for which codec) is a Rae table, not C.
- Test media is generated (a script beside it makes it from simple math:
  color bars, a moving square, a frame counter burned in, a tone), small,
  and in Git LFS. Never a copyrighted clip.

## Rae rules

- Names: types and module files `PascalCase`, everything else `camelCase`
  (functions, parameters, fields, enum cases, constants); package folders
  `camelCase`. No `snake_case`, no `SHOUTING_CASE`. US spelling.
- No abbreviations and no single-letter names (except `i`/`j`, `x`/`y`/`z`/`w`,
  `r`/`g`/`b`/`a`). A binding holding one instance of a type is named after the
  type (`timeline: TimelineState`); an id is named for the id (`entityId`).
- Every binding states its type; there is no type inference. Struct literals
  take the type from the left: `let point: Point = { x: 1, y: 2 }`.
- `let` is frozen as a whole; `var` only for what changes. No module-level
  `var` (state is a resource on its owner). No `pub`, no `@` attributes.
- ECS: per-thing data is components on entities, state there is one of is a
  resource (a plain field on its owner), logic is systems in their own
  camelCase folder with a PascalCase module (`timelineSystem/TimelineSystem.rae`).
- Files stay under 1,000 lines. `rae format` owns the layout: 2-space indent,
  100 columns; the build formats first, `RAE_FORMAT=check` refuses to write.
- Real-time code (the audio thread) does not allocate, lock or wait;
  `RAE_REALTIME_CHECK=1` checks it in the offline tests.

## Building and testing

- `./rae` runs the Rae this repository pins; `RAE_ROOT=<a Rae checkout>` uses
  that checkout for one command.
- `make test` (or `sh tools/test.sh <name>`) runs the test programs in
  `tests/`, each against its `expected.txt`. Run long commands with a timeout
  (`perl -e 'alarm 600; exec @ARGV' ...`). Leave no program running.
- Dependencies: Rae's `audio` package by git at a released version, pinned
  with its hash (`rae fetch`, `rae update audio`).
- Releases: each package is tagged `<package>/v<version>` (`video/v0.1.0`),
  its `meta.version` raised in the same commit; programs take a package by
  that tag.
- `make install-hooks` once per clone: the banned-terms gate (`tools/banned-terms-gate.sh`,
  the same term list as Rae's, kept outside the repository) runs before every commit and on
  its message, and `make test` runs it over the tree. Never commit the list or its terms.
- Binary files are in Git LFS (`.gitattributes`); a new binary type gets its
  line there in the same commit as its first file.

## Workflow

- `QUEUE.md` is this repository's task list (SUMU reads it): take the top
  open item, do it, check it, commit, write the outcome marker on its line.
  New items go in without an id, where they belong by priority.
- Commit finished work with a message that says why; end it with a
  `Co-Authored-By:` trailer naming the model. Push only when asked.
- Explain work in short plain sentences that name the files and functions.

## Source formatting: `rae format` owns layout

- 2-space indent, 100 columns, LF, one final newline, no trailing whitespace,
  braces on the declaration line; files are capped at 1,000 lines.
- Function parameters and call arguments: 1-3 items may share the line if the
  whole header/call fits in 100 columns; from FOUR items, or whenever it does
  not fit, every item goes on its own line. Object and collection literals and
  enum members: the same from FIVE items. A `type` declaration is always one
  field per line. Never hand-align.
