#!/bin/sh
# Makes the test media from simple math (tools/makeTestMedia.swift: color bars,
# a moving square, the frame counter as 16 cells and as digits, a 1 kHz tone)
# through Apple's encoders. macOS only. The small files are committed (Git
# LFS); the 4K one is made on demand into build/media (not committed).
#
#   sh tests/media/make.sh        # the small files, here
#   sh tests/media/make.sh 4k     # build/media/counter4k.mp4 (and its HEVC twin)
#
# The files, and what each one checks:
#   counter709.mp4      H.264 High with B-frames, 320x180, 90 frames at 30000/1001
#                       (29.97 fps), BT.709 limited range, tone: demux (ctts),
#                       decode order vs presentation order, the player's sync
#   counter601full.mp4  H.264, 320x180, 20 frames at 25 fps (600 / 24), BT.601
#                       full range: the YUV to RGB pass honors other tags
#   counterHevc.mp4     HEVC, 320x180, 48 frames at 24000/1001, BT.709, tone
#   counterProres.mov   ProRes 422 Proxy, 320x180, 30 frames at 30 fps (MOV)
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$root/build/tools"
tool="$root/build/tools/makeTestMedia"
if [ ! -x "$tool" ] || [ "$root/tools/makeTestMedia.swift" -nt "$tool" ]; then
  swiftc -O "$root/tools/makeTestMedia.swift" -o "$tool"
fi
if [ "${1:-}" = 4k ]; then
  mkdir -p "$root/build/media"
  "$tool" "$root/build/media/counter4k.mp4" h264 3840 2160 300 30000 1001 709 limited tone
  "$tool" "$root/build/media/counter4kHevc.mp4" hevc 3840 2160 300 30000 1001 709 limited tone
  exit 0
fi
here="$root/tests/media"
"$tool" "$here/counter709.mp4" h264 320 180 90 30000 1001 709 limited tone
"$tool" "$here/counter601full.mp4" h264 320 180 20 600 24 601 full
"$tool" "$here/counterHevc.mp4" hevc 320 180 48 24000 1001 709 limited tone
"$tool" "$here/counterProres.mov" prores 320 180 30 3000 100 709 limited
