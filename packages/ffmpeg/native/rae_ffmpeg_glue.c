/* The FFmpeg package's C (rae_ffmpeg.h). Platform reason: the codecs the
 * platforms do not decode (DNxHR, VP9 on a Mac, ...) are FFmpeg's to decode
 * (docs/design.md §3.3: no permissive decoder exists for most of them), and
 * FFmpeg is LGPL, so it is a shared library loaded at run time: a program that
 * does not use this package never links it, and one that does can have its
 * FFmpeg replaced (legal/NOTICE.md).
 *
 * The struct layouts (AVCodecContext, AVFrame, AVPacket) come from the headers
 * of the build this glue is compiled with; at load the libraries' major
 * versions must be those headers', or nothing is used. */
#include "rae_ffmpeg.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>

#ifndef RAE_FFMPEG_LIBRARY_DIR
#define RAE_FFMPEG_LIBRARY_DIR "."
#endif
#ifndef RAE_FFMPEG_LIBRARY_ENDING
#define RAE_FFMPEG_LIBRARY_ENDING "dylib"
#endif

/* The video package's struct (packages/video/native/rae_video.h), the same
 * layout under this file's own name so the two headers never meet */
typedef struct {
  int64_t version;
  int64_t (*open)(uint8_t*, int64_t, int64_t, uint8_t*, int64_t);
  int64_t (*decode)(int64_t, uint8_t*, int64_t, int64_t, int64_t);
  void (*close)(int64_t);
  int64_t (*frame_layout)(int64_t);
  int64_t (*frame_width)(int64_t);
  int64_t (*frame_height)(int64_t);
  int64_t (*frame_plane_bytes_per_row)(int64_t, int64_t);
  uint8_t* (*frame_plane_address)(int64_t, int64_t);
  int64_t (*frame_tags)(int64_t);
  void (*frame_release)(int64_t);
} RaeFfmpegBackendCalls;
#define RAE_FFMPEG_BACKEND_CALLS_VERSION 1

#define RAE_FFMPEG_FUNCTIONS(X)      \
  X(avutil, avutil_version)          \
  X(avutil, av_version_info)         \
  X(avutil, av_frame_alloc)          \
  X(avutil, av_frame_free)           \
  X(avutil, av_mallocz)              \
  X(avcodec, avcodec_version)        \
  X(avcodec, avcodec_configuration)  \
  X(avcodec, avcodec_license)        \
  X(avcodec, avcodec_find_decoder_by_name) \
  X(avcodec, avcodec_alloc_context3) \
  X(avcodec, avcodec_open2)          \
  X(avcodec, avcodec_free_context)   \
  X(avcodec, avcodec_send_packet)    \
  X(avcodec, avcodec_receive_frame)  \
  X(avcodec, av_packet_alloc)        \
  X(avcodec, av_packet_free)         \
  X(avcodec, av_packet_unref)        \
  X(avcodec, av_new_packet)

#define RAE_FFMPEG_DECLARE(library, name) static __typeof__(&name) p_##name;
RAE_FFMPEG_FUNCTIONS(RAE_FFMPEG_DECLARE)

static pthread_once_t g_rae_ffmpeg_once = PTHREAD_ONCE_INIT;
static int g_rae_ffmpeg_ready = 0;
static char g_rae_ffmpeg_problem[512] = "";

static void* rae_ffmpeg_open_library(const char* name, int major) {
  char path[1024];
  const char* folder = getenv("RAE_FFMPEG_LIBRARY_DIR");
  if (!folder || !folder[0]) folder = RAE_FFMPEG_LIBRARY_DIR;
#if defined(__APPLE__)
  snprintf(path, sizeof(path), "%s/lib%s.%d.%s", folder, name, major, RAE_FFMPEG_LIBRARY_ENDING);
#else
  snprintf(path, sizeof(path), "%s/lib%s.%s.%d", folder, name, RAE_FFMPEG_LIBRARY_ENDING, major);
#endif
  void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!library) snprintf(g_rae_ffmpeg_problem, sizeof(g_rae_ffmpeg_problem), "cannot load %s", path);
  return library;
}

static void rae_ffmpeg_load(void) {
  /* libavutil first: libavcodec names it by its full path */
  void* avutil = rae_ffmpeg_open_library("avutil", LIBAVUTIL_VERSION_MAJOR);
  if (!avutil) return;
  void* avcodec = rae_ffmpeg_open_library("avcodec", LIBAVCODEC_VERSION_MAJOR);
  if (!avcodec) return;
  int ready = 1;
#define RAE_FFMPEG_LOAD(library, name)                       \
  p_##name = (__typeof__(&name))dlsym(library, #name);       \
  if (!p_##name) {                                           \
    ready = 0;                                               \
    snprintf(g_rae_ffmpeg_problem, sizeof(g_rae_ffmpeg_problem), "no %s in lib%s", #name, #library); \
  }
  RAE_FFMPEG_FUNCTIONS(RAE_FFMPEG_LOAD)
  if (ready && ((p_avcodec_version() >> 16) != LIBAVCODEC_VERSION_MAJOR ||
                (p_avutil_version() >> 16) != LIBAVUTIL_VERSION_MAJOR)) {
    snprintf(g_rae_ffmpeg_problem, sizeof(g_rae_ffmpeg_problem),
             "libavcodec %u / libavutil %u are not the majors %d / %d this glue was built for",
             p_avcodec_version() >> 16, p_avutil_version() >> 16, LIBAVCODEC_VERSION_MAJOR,
             LIBAVUTIL_VERSION_MAJOR);
    ready = 0;
  }
  g_rae_ffmpeg_ready = ready;
}

int64_t rae_ffmpeg_available(void) {
  pthread_once(&g_rae_ffmpeg_once, rae_ffmpeg_load);
  return g_rae_ffmpeg_ready;
}

int64_t rae_ffmpeg_text(int64_t which, uint8_t* into, int64_t capacity) {
  const char* text = g_rae_ffmpeg_problem;
  if (which != 4) {
    if (!rae_ffmpeg_available()) return 0;
    text = which == 1 ? p_avcodec_configuration()
         : which == 2 ? p_avcodec_license()
         : p_av_version_info();
  }
  if (!text || !into || capacity <= 0) return 0;
  int64_t length = (int64_t)strlen(text);
  if (length > capacity) length = capacity;
  memcpy(into, text, (size_t)length);
  return length;
}

/* ---- the decoder ------------------------------------------------------------ */
typedef struct {
  AVCodecContext* context;
  AVPacket* packet;
} RaeFfmpegDecoder;

static void rae_ffmpeg_close(int64_t handle) {
  RaeFfmpegDecoder* decoder = (RaeFfmpegDecoder*)(intptr_t)handle;
  if (!decoder) return;
  if (decoder->packet) p_av_packet_free(&decoder->packet);
  if (decoder->context) p_avcodec_free_context(&decoder->context);
  free(decoder);
}

static int64_t rae_ffmpeg_open(uint8_t* codec_z, int64_t width, int64_t height, uint8_t* config,
                               int64_t config_size) {
  if (!rae_ffmpeg_available() || !codec_z) return 0;
  const AVCodec* codec = p_avcodec_find_decoder_by_name((const char*)codec_z);
  if (!codec) return 0;
  RaeFfmpegDecoder* decoder = (RaeFfmpegDecoder*)calloc(1, sizeof(RaeFfmpegDecoder));
  if (!decoder) return 0;
  decoder->context = p_avcodec_alloc_context3(codec);
  decoder->packet = p_av_packet_alloc();
  if (!decoder->context || !decoder->packet) {
    rae_ffmpeg_close((int64_t)(intptr_t)decoder);
    return 0;
  }
  decoder->context->width = (int)width;
  decoder->context->height = (int)height;
  /* threads within a frame only: a frame comes out of the packet it came in
   * with, as the video package expects of a backend */
  decoder->context->thread_count = 0;
  decoder->context->thread_type = FF_THREAD_SLICE;
  if (config && config_size > 0) {
    decoder->context->extradata = (uint8_t*)p_av_mallocz((size_t)config_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (decoder->context->extradata) {
      memcpy(decoder->context->extradata, config, (size_t)config_size);
      decoder->context->extradata_size = (int)config_size;
    }
  }
  if (p_avcodec_open2(decoder->context, codec, NULL) < 0) {
    rae_ffmpeg_close((int64_t)(intptr_t)decoder);
    return 0;
  }
  return (int64_t)(intptr_t)decoder;
}

static int64_t rae_ffmpeg_decode(int64_t handle, uint8_t* bytes, int64_t size, int64_t time,
                                 int64_t timescale) {
  (void)timescale;
  RaeFfmpegDecoder* decoder = (RaeFfmpegDecoder*)(intptr_t)handle;
  if (!decoder || !bytes || size <= 0) return -1;
  /* av_new_packet gives the padding FFmpeg's readers may read past the end */
  int status = p_av_new_packet(decoder->packet, (int)size);
  if (status < 0) return status;
  memcpy(decoder->packet->data, bytes, (size_t)size);
  decoder->packet->pts = time;
  status = p_avcodec_send_packet(decoder->context, decoder->packet);
  p_av_packet_unref(decoder->packet);
  if (status < 0 && status != AVERROR(EAGAIN)) return status;
  AVFrame* frame = p_av_frame_alloc();
  if (!frame) return -1;
  status = p_avcodec_receive_frame(decoder->context, frame);
  if (status == 0) return (int64_t)(intptr_t)frame;
  p_av_frame_free(&frame);
  return status == AVERROR(EAGAIN) ? 0 : status;
}

/* ---- frames (AVFrame) ---------------------------------------------------------- */
#define RAE_FFMPEG_FRAME(handle) ((AVFrame*)(intptr_t)(handle))

static int64_t rae_ffmpeg_frame_layout(int64_t handle) {
  switch (RAE_FFMPEG_FRAME(handle)->format) {
    case AV_PIX_FMT_NV12: return 1;
    case AV_PIX_FMT_YUV420P: case AV_PIX_FMT_YUVJ420P: return 2;
    case AV_PIX_FMT_YUV422P: case AV_PIX_FMT_YUVJ422P: return 3;
    case AV_PIX_FMT_YUV444P: case AV_PIX_FMT_YUVJ444P: return 4;
    default: return 0;
  }
}

static int64_t rae_ffmpeg_frame_width(int64_t handle) { return RAE_FFMPEG_FRAME(handle)->width; }

static int64_t rae_ffmpeg_frame_height(int64_t handle) { return RAE_FFMPEG_FRAME(handle)->height; }

static int64_t rae_ffmpeg_frame_plane_bytes_per_row(int64_t handle, int64_t plane) {
  if (plane < 0 || plane >= AV_NUM_DATA_POINTERS) return 0;
  return RAE_FFMPEG_FRAME(handle)->linesize[plane];
}

static uint8_t* rae_ffmpeg_frame_plane_address(int64_t handle, int64_t plane) {
  if (plane < 0 || plane >= AV_NUM_DATA_POINTERS) return NULL;
  return RAE_FFMPEG_FRAME(handle)->data[plane];
}

/* matrix | primaries << 8 | transfer << 16 | full range << 24: FFmpeg's enums
 * are the H.273 code points the video package takes */
static int64_t rae_ffmpeg_frame_tags(int64_t handle) {
  const AVFrame* frame = RAE_FFMPEG_FRAME(handle);
  int full = frame->color_range == AVCOL_RANGE_JPEG || frame->format == AV_PIX_FMT_YUVJ420P ||
             frame->format == AV_PIX_FMT_YUVJ422P || frame->format == AV_PIX_FMT_YUVJ444P;
  return ((int64_t)(frame->colorspace & 255)) | ((int64_t)(frame->color_primaries & 255) << 8) |
         ((int64_t)(frame->color_trc & 255) << 16) | ((int64_t)full << 24);
}

static void rae_ffmpeg_frame_release(int64_t handle) {
  AVFrame* frame = RAE_FFMPEG_FRAME(handle);
  p_av_frame_free(&frame);
}

static const RaeFfmpegBackendCalls g_rae_ffmpeg_calls = {
    RAE_FFMPEG_BACKEND_CALLS_VERSION,
    rae_ffmpeg_open,
    rae_ffmpeg_decode,
    rae_ffmpeg_close,
    rae_ffmpeg_frame_layout,
    rae_ffmpeg_frame_width,
    rae_ffmpeg_frame_height,
    rae_ffmpeg_frame_plane_bytes_per_row,
    rae_ffmpeg_frame_plane_address,
    rae_ffmpeg_frame_tags,
    rae_ffmpeg_frame_release,
};

void* rae_ffmpeg_backend_calls(void) {
  return rae_ffmpeg_available() ? (void*)&g_rae_ffmpeg_calls : NULL;
}
