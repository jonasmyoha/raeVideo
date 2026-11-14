/* The C glue of the FFmpeg package (docs/design.md §4): FFmpeg's libavcodec and
 * libavutil loaded at run time (dlopen) from the package's own build, and its
 * decoders handed to the video package as a registered backend (a
 * RaeVideoBackendCalls, packages/video/native/rae_video.h). Each function makes
 * one FFmpeg call or one small group that only makes sense together; which
 * codec goes to FFmpeg is Rae (ffmpeg/Ffmpeg). */
#ifndef RAE_FFMPEG_H
#define RAE_FFMPEG_H

#include <stdint.h>

/* 1 when FFmpeg's libraries loaded and are the versions the glue was built
 * against, else 0 (rae_ffmpeg_text(4, ...) then says why) */
int64_t rae_ffmpeg_available(void);
/* The calls to give to video's rae_video_backend_register, or NULL when FFmpeg
 * is not available */
void* rae_ffmpeg_backend_calls(void);
/* A text about the loaded FFmpeg into `into` (at most `capacity` bytes, no
 * zero added): 1 its configure line, 2 its license, 3 its version, 4 why it
 * did not load. Its length. */
int64_t rae_ffmpeg_text(int64_t which, uint8_t* into, int64_t capacity);

#endif
