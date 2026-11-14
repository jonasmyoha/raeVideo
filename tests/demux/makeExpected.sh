#!/bin/sh
# Writes tests/demux/expected.txt from ffprobe (a reference outside our code,
# like Rae's fixture 1414): every packet of every track of each test file, in
# decode order, track by track, with its presentation and decode times (in the
# track's timebase, the edit's start taken off), duration, size and keyframe
# flag. Run it again after tests/media/make.sh remakes the media.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
media="$here/../media"
{
  for file in counter709.mp4 counter601full.mp4 counterHevc.mp4 counterProres.mov; do
    ffprobe -v error -show_entries packet=stream_index,pts,dts,duration,size,flags -of csv \
      "$media/$file" | awk -F, -v file="$file" '
      { key = (substr($7, 1, 1) == "K") ? 1 : 0
        n = count[$2] + 0
        count[$2] = n + 1
        line[$2, n] = sprintf("%s track %d sample %d: pts %d dts %d duration %d size %d key %d",
          file, $2, n, $3, $4, $5, $6, key)
        if ($2 > tracks) tracks = $2 }
      END { for (t = 0; t <= tracks; t++) for (i = 0; i < count[t]; i++) print line[t, i] }'
  done
  echo "done"
} > "$here/expected.txt"
wc -l "$here/expected.txt"
