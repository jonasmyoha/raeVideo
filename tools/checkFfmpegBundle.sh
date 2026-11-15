#!/bin/sh
# FFmpeg in a shipped macOS app (docs/design.md §4, packages/ffmpeg/tools):
#
# 1. Builds examples/ffmpegApp with a Rae home of its own (build/bundleHome,
#    whose FFmpeg build is this check's alone), puts it in an .app and FFmpeg
#    in the app (bundleFfmpeg.sh), and checks the app (checkFfmpegInApp.sh).
# 2. Takes that FFmpeg build away and runs the app: it decodes and shows the
#    DNxHR test file with FFmpeg from its own Contents/Frameworks. The same
#    program outside the app then finds no FFmpeg at all.
# 3. Checks that checkFfmpegInApp.sh refuses an app whose libavcodec was
#    configured with --enable-gpl (a stand-in library with that configure line
#    and license), and one whose libavcodec is a static archive.
#
# Run by tools/test.sh; macOS. The FFmpeg build is moved back at the end, so
# only the first run builds FFmpeg (about a minute).
set -u
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 1
tools="$root/packages/ffmpeg/tools"
userHome="${RAE_HOME:-$HOME/.rae}"
home="$root/build/bundleHome"
out=$(mktemp -d)
away="$home/packages/ffmpeg.away"
restore() {
  if [ -d "$away" ]; then
    rm -rf "$home/packages/ffmpeg"
    mv "$away" "$home/packages/ffmpeg"
  fi
  rm -rf "$out"
}
trap restore EXIT
failed=0
fail() {
  echo "FAIL: $*"
  failed=1
}

# The pinned toolchain (./rae installs it), run with the check's own home; the
# other packages' builds are shared with the usual home
./rae --version > /dev/null 2>&1 || { echo "FAIL: ./rae does not run"; exit 1; }
tool=$(sed -n 's/^tool="\(.*\)"$/\1/p' rae)
toolchain="$userHome/toolchains/$tool"
[ -x "$toolchain/compiler/bin/rae" ] || { echo "FAIL: no toolchain $tool in $userHome"; exit 1; }
[ ! -f "$toolchain/.deps" ] || . "$toolchain/.deps"
mkdir -p "$home/packages"
for package in audio video; do
  [ -e "$home/packages/$package" ] || ln -s "$userHome/packages/$package" "$home/packages/$package"
done
program="$out/ffmpegApp"
RAE_HOME="$home" RAE_RUN_KEEP_BINARY="$program" perl -e 'alarm shift; exec @ARGV' 900 \
  "$toolchain/compiler/bin/rae" run examples/ffmpegApp/Main.rae -- --ffmpeg-folder > "$out/build.log" 2>&1
folder=$(grep -v '^@@' "$out/build.log" | grep -v '^note:' | tail -1)
[ -f "$program" ] || { echo "FAIL: building examples/ffmpegApp"; tail -5 "$out/build.log"; exit 1; }
case "$folder" in
  "$home"/packages/ffmpeg/*) ;;
  *) echo "FAIL: the program's FFmpeg is not the check's own build: $folder"; exit 1 ;;
esac

# The app: the program, a plist, FFmpeg
app="$out/FfmpegApp.app"
mkdir -p "$app/Contents/MacOS"
cp "$program" "$app/Contents/MacOS/ffmpegApp"
cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>ffmpegApp</string>
  <key>CFBundleIdentifier</key><string>org.raevideo.ffmpegapp</string>
  <key>CFBundleName</key><string>FfmpegApp</string>
  <key>CFBundlePackageType</key><string>APPL</string>
</dict>
</plist>
PLIST
sh "$tools/bundleFfmpeg.sh" "$app" "$folder" > "$out/bundle.log" 2>&1 ||
  { echo "FAIL: bundleFfmpeg.sh"; cat "$out/bundle.log"; exit 1; }
echo "$(grep -c '^bundled' "$out/bundle.log") files bundled: $(sed -n 's|^bundled Contents/||p' "$out/bundle.log" | tr '\n' ' ')"
if sh "$tools/checkFfmpegInApp.sh" "$app" > "$out/check.log" 2>&1; then
  sed -n 's/^ok: //p' "$out/check.log"
else
  fail "checkFfmpegInApp.sh refused the app:"; cat "$out/check.log"
fi

# The FFmpeg build gone: the app plays from its own Frameworks, the bare
# program finds nothing
mv "$home/packages/ffmpeg" "$away"
played=$(perl -e 'alarm shift; exec @ARGV' 300 "$app/Contents/MacOS/ffmpegApp" \
  "$root/tests/media/counterDnxhr.mov" 2>&1)
status=$?
printf '%s\n' "$played" | sed "s|$out/||g; s|$root/||g"
[ "$status" -eq 0 ] || fail "the app exited with $status"
printf '%s\n' "$played" | grep -q "from $app/Contents/Frameworks\$" ||
  fail "the app's FFmpeg did not come from its Frameworks"
printf '%s\n' "$played" | grep -q "30 of 30 frames shown on the GPU, counters 0..29 in order: true" ||
  fail "the app did not show the DNxHR file's frames"
bare=$(perl -e 'alarm shift; exec @ARGV' 60 "$program" --ffmpeg-folder 2>&1)
if printf '%s\n' "$bare" | grep -q "FFmpeg did not load"; then
  echo "outside the app, with the build gone: FFmpeg did not load"
else
  fail "the program outside the app still found FFmpeg: $bare"
fi

# What the check refuses: a GPL libavcodec, a static one
refused() {
  label=$1
  bad=$2
  if sh "$tools/checkFfmpegInApp.sh" "$bad" > "$out/refused.log" 2>&1; then
    fail "checkFfmpegInApp.sh accepted $label"
  else
    echo "refused $label:"
    grep "^FAIL" "$out/refused.log" | sed "s/^FAIL: /  /"
  fi
}
avcodec=$(cd "$app/Contents/Frameworks" && ls libavcodec.*.dylib)
gpl="$out/Gpl.app"
cp -R "$app" "$gpl"
cat > "$out/gpl.c" <<'C'
/* A stand-in libavcodec as a GPL build would say it was configured */
const char* standInConfiguration = "--prefix=/ffmpeg --enable-gpl --enable-shared --disable-static";
const char* standInLicense = "libavcodec license: GPL version 2 or later";
C
cc -dynamiclib -install_name "@loader_path/$avcodec" -o "$gpl/Contents/Frameworks/$avcodec" "$out/gpl.c" || fail "compiling the stand-in"
refused "a libavcodec configured with --enable-gpl" "$gpl"
static="$out/Static.app"
cp -R "$app" "$static"
cc -c -o "$out/gpl.o" "$out/gpl.c" && rm -f "$static/Contents/Frameworks/$avcodec" &&
  ar rcs "$static/Contents/Frameworks/$avcodec" "$out/gpl.o"
refused "a static libavcodec" "$static"
exit "$failed"
