#!/bin/bash
# Build the FFmpeg package's C (docs/design.md §4): FFmpeg itself, LGPL-only and
# shared, then the glue that loads it at run time.
#
# `rae` runs this for a program that uses the package, once per version of it, with
# RAE_PACKAGE_INSTALL (where the result goes) and RAE_PACKAGE_CACHE (its folder for the rest).
#
#   1. Fetches the one pinned FFmpeg release below into the cache (never into this
#      repository) and refuses it unless its SHA-256 is the pinned one.
#   2. Configures it LGPL-only (--disable-gpl --disable-nonfree), shared, without its
#      programs, docs or any library it would find on the machine (--disable-autodetect),
#      with an explicit list of decoders, encoders, demuxers, muxers, parsers and
#      bitstream filters; refuses the result if its config.h says GPL or nonfree.
#   3. Builds and installs it into $RAE_PACKAGE_INSTALL/ffmpeg: libavcodec, libavformat and
#      libavutil as dylibs (.so on Linux). Its prefix is a neutral /ffmpeg (installed there
#      under DESTDIR), so no path of the machine that built it is inside them, and on macOS
#      each names the others by @loader_path: they load from any one folder, the package's
#      build or an app's Contents/Frameworks, and a user can put their own build in their
#      place (the LGPL's condition, legal/NOTICE.md).
#   4. Compiles native/rae_ffmpeg_glue.c (which loads libavcodec and libavutil with dlopen:
#      a program links no FFmpeg) into lib/libraeffmpeg.a, and puts the LGPL text, the
#      notice, the configure line and the source's address and hash in legal/.
#   5. Writes BUNDLE: the files a program that ships must carry, one per line, each a path
#      in $RAE_PACKAGE_INSTALL then where it goes in a macOS app (tools/bundleFfmpeg.sh).
#
# macOS first (Linux builds the same way, untested). Never iOS: an LGPL library inside an
# App Store app is not what this package offers (docs/design.md §4).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
INSTALL="$RAE_PACKAGE_INSTALL"
CACHE="${RAE_PACKAGE_CACHE:-$INSTALL.cache}"

VERSION="9.0.2"
SHA256="8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"
URL="https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
# What it can decode, encode, read and write; nothing else is built
COMPONENTS=(
  --enable-decoder=dnxhd,prores,vp8,vp9,ffv1
  --enable-encoder=dnxhd,ffv1
  --enable-demuxer=mov,matroska,mxf
  --enable-muxer=mov,matroska
  --enable-parser=dnxhd,vp8,vp9
  --enable-bsf=vp9_superframe_split
  --enable-protocol=file
)

case "$(uname -s)" in
  Darwin) libraryEnding="dylib" ;;
  Linux) libraryEnding="so" ;;
  *) echo "ffmpeg: this package builds on macOS (and Linux), not on $(uname -s)" >&2; exit 1 ;;
esac
start=$(date +%s)
mkdir -p "$CACHE"
tarball="$CACHE/ffmpeg-$VERSION.tar.xz"
if [ ! -f "$tarball" ]; then
  echo "ffmpeg: fetching $URL"
  curl -fL --retry 3 --max-time 600 -o "$tarball.part" "$URL"
  mv "$tarball.part" "$tarball"
fi
got="$(shasum -a 256 "$tarball" | cut -d' ' -f1)"
if [ "$got" != "$SHA256" ]; then
  rm -f "$tarball"
  echo "ffmpeg: $URL has SHA-256 $got, not the pinned $SHA256; refused" >&2
  exit 1
fi

source="$CACHE/source"
rm -rf "$source"
mkdir -p "$source"
tar -xf "$tarball" -C "$source"
cd "$source/ffmpeg-$VERSION"

rm -rf "${INSTALL:?}"
mkdir -p "$INSTALL/include" "$INSTALL/lib" "$INSTALL/legal"
CONFIGURE=(
  --prefix=/ffmpeg
  --disable-gpl --disable-nonfree
  --enable-shared --disable-static
  --disable-programs --disable-doc
  --disable-autodetect --disable-everything --disable-network
  --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample
  "${COMPONENTS[@]}"
)
if [ "$libraryEnding" = dylib ]; then
  CONFIGURE+=(--install-name-dir=@loader_path)
fi
echo "ffmpeg: configuring $VERSION (LGPL only)"
./configure "${CONFIGURE[@]}" > "$CACHE/configure.log" 2>&1 || {
  tail -20 "$CACHE/configure.log" >&2
  exit 1
}
if grep -q "#define CONFIG_GPL 1\|#define CONFIG_NONFREE 1" config.h; then
  echo "ffmpeg: the configured build would be GPL or nonfree; refused" >&2
  exit 1
fi
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
echo "ffmpeg: building with $jobs jobs"
make -j"$jobs" > "$CACHE/make.log" 2>&1 || { tail -20 "$CACHE/make.log" >&2; exit 1; }
make install DESTDIR="$INSTALL" > "$CACHE/install.log" 2>&1 || { tail -20 "$CACHE/install.log" >&2; exit 1; }

cp COPYING.LGPLv2.1 "$INSTALL/legal/COPYING.LGPLv2.1"
cp "$HERE/../legal/NOTICE.md" "$INSTALL/legal/NOTICE.md"
printf '%s\n' "${CONFIGURE[@]}" > "$INSTALL/legal/CONFIGURE"
printf 'FFmpeg %s\n%s\nSHA-256 %s\n' "$VERSION" "$URL" "$SHA256" > "$INSTALL/legal/SOURCE"

cp "$HERE/rae_ffmpeg.h" "$INSTALL/include/"
objs="$(mktemp -d)"
cc -O2 -DNDEBUG -I"$HERE" -I"$INSTALL/ffmpeg/include" \
  -DRAE_FFMPEG_LIBRARY_DIR="\"$INSTALL/ffmpeg/lib\"" -DRAE_FFMPEG_LIBRARY_ENDING="\"$libraryEnding\"" \
  -c "$HERE/rae_ffmpeg_glue.c" -o "$objs/rae_ffmpeg_glue.o"
ar rcs "$INSTALL/lib/libraeffmpeg.a" "$objs"/*.o
rm -rf "${objs:?}" "$source"

# What a shipped program carries (the LGPL's conditions, legal/NOTICE.md): the two
# libraries the glue loads, as files (not links), and legal/
avcodecMajor="$(sed -n 's/^#define LIBAVCODEC_VERSION_MAJOR *//p' "$INSTALL/ffmpeg/include/libavcodec/version_major.h")"
avutilMajor="$(sed -n 's/^#define LIBAVUTIL_VERSION_MAJOR *//p' "$INSTALL/ffmpeg/include/libavutil/version.h")"
if [ "$libraryEnding" = dylib ]; then
  avcodecFile="libavcodec.$avcodecMajor.dylib"
  avutilFile="libavutil.$avutilMajor.dylib"
else
  avcodecFile="libavcodec.so.$avcodecMajor"
  avutilFile="libavutil.so.$avutilMajor"
fi
{
  echo "ffmpeg/lib/$avutilFile Contents/Frameworks/$avutilFile"
  echo "ffmpeg/lib/$avcodecFile Contents/Frameworks/$avcodecFile"
  for file in NOTICE.md COPYING.LGPLv2.1 CONFIGURE SOURCE; do
    echo "legal/$file Contents/Resources/legal/ffmpeg/$file"
  done
} > "$INSTALL/BUNDLE"
end=$(date +%s)
echo "ffmpeg: FFmpeg $VERSION built into $INSTALL in $((end - start)) s"
