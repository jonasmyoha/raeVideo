#!/bin/sh
# The files tests/writer wrote (build/writer), checked by others than our own
# reader: ffprobe accepts each and ffmpeg decodes it with no error; ffmpeg's
# decode of the MOV's PCM is the pattern tests/writer wrote, byte for byte; and
# AVFoundation (what QuickTime Player is built on) decodes every frame and
# sound sample (tools/checkAvFoundation.swift). Run by tools/test.sh after the
# test programs; SKIP when a tool is missing. Opening them in QuickTime Player
# itself stays a check by hand.
set -u
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 1
for tool in ffprobe ffmpeg python3 swift; do
  command -v "$tool" >/dev/null 2>&1 || { echo "SKIP: no $tool"; exit 0; }
done
files=$(ls build/writer/*.mov build/writer/*.mp4 2>/dev/null)
[ -n "$files" ] || { echo "FAIL: no files in build/writer (tests/writer writes them)"; exit 1; }
failed=0
for file in $files; do
  if ! ffprobe -v error -show_entries stream=codec_name -of csv "$file" >/dev/null 2>&1; then
    echo "FAIL: ffprobe refuses $file"; failed=1
  fi
  errors=$(ffmpeg -v error -i "$file" -f null - 2>&1)
  if [ -n "$errors" ]; then
    echo "FAIL: ffmpeg decoding $file: $errors" | head -3; failed=1
  fi
done
same=$(ffmpeg -v error -i build/writer/hevcPcm.mov -map 0:a -f s16le - | python3 -I -c "
import sys
d = sys.stdin.buffer.read()
print(len(d) > 0 and d == bytes(((i * 7919 + i // 3) % 251) for i in range(len(d))))")
[ "$same" = True ] || { echo "FAIL: ffmpeg's decode of the PCM is not the pattern"; failed=1; }
if ! perl -e 'alarm shift; exec @ARGV' 300 swift tools/checkAvFoundation.swift $files > build/writer/avfoundation.log 2>&1; then
  grep -v 'warning' build/writer/avfoundation.log | grep 'FAILED\|cannot' | head -5
  echo "FAIL: AVFoundation could not decode every file"; failed=1
fi
[ "$failed" -eq 0 ] && echo "ffprobe, ffmpeg and AVFoundation read every written file; the PCM is exact"
exit "$failed"
