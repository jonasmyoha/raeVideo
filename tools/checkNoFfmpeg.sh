#!/bin/sh
# A program without packages/ffmpeg links no FFmpeg (docs/design.md §4): builds
# tests/decode (video only) and checks that its binary has no FFmpeg symbol, no
# FFmpeg library among the ones it loads, and not even the name libavcodec. And
# builds tests/ffmpeg: it holds the package's glue, but still links no FFmpeg
# library, because the glue loads FFmpeg at run time (dlopen). Run by
# tools/test.sh; macOS (otool).
set -u
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 1
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
failed=0
# `rae run` links the program and keeps a copy of it (RAE_RUN_KEEP_BINARY)
build() {
  RAE_RUN_KEEP_BINARY="$out/$1" perl -e 'alarm shift; exec @ARGV' 900 ./rae run "tests/$1/Main.rae" \
    > "$out/$1.log" 2>&1
  [ -f "$out/$1" ] || { echo "FAIL: building tests/$1"; tail -5 "$out/$1.log"; return 1; }
}
build decode || exit 1
build ffmpeg || exit 1
if nm "$out/decode" | grep -qE ' _(av_|avcodec_|avutil_|rae_ffmpeg_)'; then
  echo "FAIL: the video-only program has FFmpeg symbols"; failed=1
fi
if otool -L "$out/decode" | grep -q 'libav'; then
  echo "FAIL: the video-only program links an FFmpeg library"; failed=1
fi
if strings "$out/decode" | grep -q 'libavcodec'; then
  echo "FAIL: the video-only program names libavcodec"; failed=1
fi
if ! nm "$out/ffmpeg" | grep -q ' _rae_ffmpeg_backend_calls'; then
  echo "FAIL: the FFmpeg test program has no FFmpeg glue"; failed=1
fi
if otool -L "$out/ffmpeg" | grep -q 'libav'; then
  echo "FAIL: the FFmpeg test program links an FFmpeg library instead of loading it"; failed=1
fi
if [ "$failed" -eq 0 ]; then
  echo "a program without packages/ffmpeg has no FFmpeg symbol, library or name; one with it loads FFmpeg at run time"
fi
exit "$failed"
