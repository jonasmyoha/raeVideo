#!/bin/sh
# Checks the FFmpeg inside a macOS app against the LGPL's conditions and this
# package's rules (docs/design.md §4, legal/NOTICE.md):
#
#   sh checkFfmpegInApp.sh <Name.app>
#
# 1. libavcodec and libavutil are in Contents/Frameworks as separate files:
#    shared libraries (Mach-O dylibs), not links, and no static FFmpeg anywhere
#    in the app (no libav*.a, no FFmpeg function defined in a program in
#    Contents/MacOS).
# 2. Each library names only libraries beside it (@loader_path, @rpath) or the
#    system's: no path of the machine that built it.
# 3. The configure line inside each library (what avcodec_configuration()
#    returns) has no --enable-gpl and no --enable-nonfree, and the license
#    inside it is LGPL.
# 4. Contents/Resources/legal/ffmpeg has NOTICE.md, COPYING.LGPLv2.1, SOURCE and
#    CONFIGURE, and CONFIGURE has no --enable-gpl or --enable-nonfree either.
#
# Prints what it found; exits 1 when anything is wrong. macOS (otool, nm).
set -u
app=${1:?usage: checkFfmpegInApp.sh <Name.app>}
frameworks="$app/Contents/Frameworks"
legal="$app/Contents/Resources/legal/ffmpeg"
failed=0
fail() {
  echo "FAIL: $*"
  failed=1
}
for name in avcodec avutil; do
  found=0
  for library in "$frameworks"/lib$name.*.dylib; do
    [ -e "$library" ] || continue
    found=1
    file=$(basename "$library")
    if [ -L "$library" ]; then
      fail "$file is a link, not the library's own file"
      continue
    fi
    if ! file -b "$library" | grep -q "dynamically linked shared library"; then
      fail "$file is not a shared library ($(file -b "$library" | cut -c1-60))"
      continue
    fi
    others=$(otool -L "$library" | tail -n +2 | awk '{print $1}' |
      grep -v -e '^@loader_path/' -e '^@rpath/' -e '^/usr/lib/' -e '^/System/Library/' || true)
    if [ -n "$others" ]; then
      fail "$file names libraries outside the app and the system: $(echo $others)"
    fi
    configuration=$(strings -a "$library" | grep -e '^--prefix=' -e '^--disable-gpl' -e '^--enable-' | head -1)
    if [ -z "$configuration" ]; then
      fail "$file has no configure line in it"
    elif printf '%s\n' "$configuration" | grep -q -e '--enable-gpl' -e '--enable-nonfree'; then
      fail "$file was configured with $(printf '%s\n' "$configuration" | grep -o -e '--enable-gpl' -e '--enable-nonfree' | sort -u | tr '\n' ' ' | sed 's/ $//')"
    fi
    license=$(strings -a "$library" | grep "lib$name license: " | head -1)
    case "$license" in
      "lib$name license: LGPL version"*) echo "ok: $file is a shared library, $license" ;;
      "") fail "$file says no license" ;;
      *) fail "$file: $license" ;;
    esac
  done
  [ "$found" -eq 1 ] || fail "no lib$name in $frameworks"
done
statics=$(find "$app" -name 'libav*.a' 2>/dev/null)
[ -z "$statics" ] || fail "static FFmpeg in the app: $statics"
for program in "$app"/Contents/MacOS/*; do
  [ -f "$program" ] || continue
  if nm -U "$program" 2>/dev/null | grep -qE ' T _(avcodec_|av_frame_|av_packet_|avutil_)'; then
    fail "$(basename "$program") has FFmpeg linked into it"
  fi
done
for file in NOTICE.md COPYING.LGPLv2.1 SOURCE CONFIGURE; do
  [ -s "$legal/$file" ] || fail "no legal/ffmpeg/$file in the app's Resources"
done
if [ -f "$legal/CONFIGURE" ] && grep -q -e '--enable-gpl' -e '--enable-nonfree' "$legal/CONFIGURE"; then
  fail "legal/ffmpeg/CONFIGURE has --enable-gpl or --enable-nonfree"
fi
[ "$failed" -eq 0 ] && echo "ok: FFmpeg in $(basename "$app") is LGPL, shared and separate, with its legal files"
exit "$failed"
