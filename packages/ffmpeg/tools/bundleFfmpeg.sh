#!/bin/sh
# Puts FFmpeg into a macOS app (docs/design.md §4): the files the package's
# build lists in its BUNDLE (native/build.sh), each where BUNDLE says, as a
# file, not a link:
#
#   Contents/Frameworks/libavutil.<major>.dylib, libavcodec.<major>.dylib
#   Contents/Resources/legal/ffmpeg/NOTICE.md, COPYING.LGPLv2.1, CONFIGURE, SOURCE
#
#   sh bundleFfmpeg.sh <Name.app> <the package's build>
#
# The build is the folder a program prints with ffmpegLibraryFolder() (its
# ffmpeg/lib) or the install folder above it. The glue loads FFmpeg from the
# app's Contents/Frameworks before the build, and the libraries name each other
# by @loader_path, so nothing is rewritten here. Signing the app afterwards is
# the publisher's (docs/design.md §4, library validation). Then check it:
# sh checkFfmpegInApp.sh <Name.app>.
set -eu
app=${1:?usage: bundleFfmpeg.sh <Name.app> <the package build>}
build=${2:?usage: bundleFfmpeg.sh <Name.app> <the package build>}
[ -d "$app/Contents" ] || { echo "bundleFfmpeg: $app is not an app (no Contents)" >&2; exit 1; }
install=""
for candidate in "$build" "$build/.." "$build/../.."; do
  if [ -f "$candidate/BUNDLE" ]; then
    install=$(CDPATH= cd -- "$candidate" && pwd)
    break
  fi
done
[ -n "$install" ] || { echo "bundleFfmpeg: no BUNDLE in or above $build" >&2; exit 1; }
while read -r from to; do
  [ -n "$from" ] || continue
  mkdir -p "$app/$(dirname "$to")"
  # -L: the file a link names, under the link's name
  cp -L "$install/$from" "$app/$to"
  echo "bundled $to"
done < "$install/BUNDLE"
