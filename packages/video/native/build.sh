#!/bin/bash
# Build the video package's C (docs/design.md §5 V1).
#
# `rae` runs this for a program that uses the package, once per version of it, with
# RAE_PACKAGE_INSTALL (where the result goes) and RAE_PACKAGE_CACHE (its folder for the rest).
# It compiles rae_video_glue.c into lib/libraevideo.a; VideoToolbox, CoreMedia, CoreVideo and
# CoreFoundation are loaded at run time, so a program links no framework for them.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
INSTALL="$RAE_PACKAGE_INSTALL"
rm -rf "${INSTALL:?}"
mkdir -p "$INSTALL/include" "$INSTALL/lib" "$INSTALL/src"
cp "$HERE/rae_video.h" "$INSTALL/include/"
cp "$HERE/rae_video.h" "$HERE/rae_video_glue.c" "$INSTALL/src/"
echo "" > "$INSTALL/DEVICE_DEFINES"
objs="$(mktemp -d)"
cc -O2 -DNDEBUG -I"$HERE" -c "$HERE/rae_video_glue.c" -o "$objs/rae_video_glue.o"
ar rcs "$INSTALL/lib/libraevideo.a" "$objs"/*.o
rm -rf "${objs:?}"
echo "video: built into $INSTALL"
