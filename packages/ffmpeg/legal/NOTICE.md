# FFmpeg in this program

This program uses FFmpeg (https://ffmpeg.org), version 9.0.2, licensed under the
GNU Lesser General Public License version 2.1 or later (COPYING.LGPLv2.1 beside
this file). FFmpeg is not part of this program's own code and is not covered by
its license.

- **Which FFmpeg:** the release tarball https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz,
  SHA-256 8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e (SOURCE),
  unmodified. That tarball is its complete source; ask the program's publisher
  for a copy if it did not come with the program.
- **How it was built:** the configure line is in CONFIGURE: LGPL only
  (`--disable-gpl --disable-nonfree`), shared libraries, and only the listed
  decoders, encoders, demuxers and muxers. No GPL or nonfree code is in it.
- **How it is used:** the program loads libavcodec and libavutil at run time from
  their own files (dynamic linking): in a macOS app, Contents/Frameworks. You may
  replace those files with your own build of FFmpeg of the same major versions,
  built from the source above or a modified one, and the program will use it.
  On macOS a signed app may then need signing again, the app and the libraries
  with one identity of your own (`codesign --force --deep --sign <identity>
  <the app>`), because macOS refuses a library signed by someone else in it
  unless the app allows that.
- **Codec patents:** some formats FFmpeg can decode may be covered by patents
  in some countries; FFmpeg's license gives no patent license (docs/design.md
  §3.1 in raeVideo).

The glue between the program and FFmpeg (raeVideo's packages/ffmpeg) is MIT.
