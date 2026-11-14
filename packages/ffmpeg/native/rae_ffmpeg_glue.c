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
#include <libavutil/opt.h>
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
  X(avutil, av_frame_get_buffer)     \
  X(avutil, av_opt_set)              \
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
  X(avcodec, av_new_packet)          \
  X(avcodec, avcodec_find_encoder_by_name) \
  X(avcodec, avcodec_send_frame)     \
  X(avcodec, avcodec_receive_packet)

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

/* ---- the encoder ------------------------------------------------------------
 * Frames come in as 8-bit planar Y'CbCr, the planes packed one after another
 * (video's RaeVideoEncoderCalls); packets are kept until the caller takes them.
 * Everything runs on the caller's thread. */
typedef struct {
  int64_t version;
  int64_t (*open)(uint8_t*, uint8_t*, int64_t, int64_t, int64_t, int64_t, int64_t);
  int64_t (*input_layout)(int64_t);
  int64_t (*encode)(int64_t, uint8_t*, int64_t);
  int64_t (*finish)(int64_t);
  int64_t (*packet_count)(int64_t);
  int64_t (*packet_info)(int64_t, int64_t*);
  int64_t (*take_packet)(int64_t, uint8_t*, int64_t);
  int64_t (*config_size)(int64_t, int64_t*);
  int64_t (*copy_config)(int64_t, uint8_t*, int64_t);
  void (*close)(int64_t);
} RaeFfmpegEncoderCalls;

typedef struct {
  uint8_t* bytes;
  int64_t size;
  int64_t time;
  int64_t decode_time;
  int64_t keyframe;
} RaeFfmpegPacket;

typedef struct {
  AVCodecContext* context;
  AVPacket* packet;
  int64_t layout;
  RaeFfmpegPacket* packets;
  int64_t count;
  int64_t capacity;
} RaeFfmpegEncoder;

static void rae_ffmpeg_encoder_close(int64_t handle) {
  RaeFfmpegEncoder* encoder = (RaeFfmpegEncoder*)(intptr_t)handle;
  if (!encoder) return;
  for (int64_t i = 0; i < encoder->count; i++) free(encoder->packets[i].bytes);
  free(encoder->packets);
  if (encoder->packet) p_av_packet_free(&encoder->packet);
  if (encoder->context) p_avcodec_free_context(&encoder->context);
  free(encoder);
}

static int64_t rae_ffmpeg_encoder_open(uint8_t* encoder_z, uint8_t* profile_z, int64_t width,
                                       int64_t height, int64_t timescale, int64_t bit_rate,
                                       int64_t keyframe_interval) {
  if (!rae_ffmpeg_available() || !encoder_z) return 0;
  const AVCodec* codec = p_avcodec_find_encoder_by_name((const char*)encoder_z);
  if (!codec) return 0;
  RaeFfmpegEncoder* encoder = (RaeFfmpegEncoder*)calloc(1, sizeof(RaeFfmpegEncoder));
  if (!encoder) return 0;
  encoder->context = p_avcodec_alloc_context3(codec);
  encoder->packet = p_av_packet_alloc();
  if (!encoder->context || !encoder->packet) {
    rae_ffmpeg_encoder_close((int64_t)(intptr_t)encoder);
    return 0;
  }
  AVCodecContext* context = encoder->context;
  int dnxhd = strcmp((const char*)encoder_z, "dnxhd") == 0;
  /* DNxHR takes 4:2:2, FFV1 is given 4:2:0 */
  encoder->layout = dnxhd ? 3 : 2;
  context->pix_fmt = dnxhd ? AV_PIX_FMT_YUV422P : AV_PIX_FMT_YUV420P;
  context->width = (int)width;
  context->height = (int)height;
  context->time_base = (AVRational){1, (int)timescale};
  context->color_primaries = AVCOL_PRI_BT709;
  context->color_trc = AVCOL_TRC_BT709;
  context->colorspace = AVCOL_SPC_BT709;
  context->color_range = AVCOL_RANGE_MPEG;
  if (bit_rate > 0) context->bit_rate = bit_rate;
  if (keyframe_interval > 0) context->gop_size = (int)keyframe_interval;
  /* FFV1 keeps its configuration in extradata (MOV's glbl box) when asked to */
  if (!dnxhd) context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  if (profile_z && profile_z[0]) {
    p_av_opt_set(context->priv_data, "profile", (const char*)profile_z, 0);
  }
  if (p_avcodec_open2(context, codec, NULL) < 0) {
    rae_ffmpeg_encoder_close((int64_t)(intptr_t)encoder);
    return 0;
  }
  return (int64_t)(intptr_t)encoder;
}

static int64_t rae_ffmpeg_encoder_input_layout(int64_t handle) {
  RaeFfmpegEncoder* encoder = (RaeFfmpegEncoder*)(intptr_t)handle;
  return encoder ? encoder->layout : 0;
}

/* Moves every packet the encoder has into the queue */
static int64_t rae_ffmpeg_encoder_drain(RaeFfmpegEncoder* encoder) {
  for (;;) {
    int status = p_avcodec_receive_packet(encoder->context, encoder->packet);
    if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return 0;
    if (status < 0) return status;
    if (encoder->count == encoder->capacity) {
      int64_t capacity = encoder->capacity ? encoder->capacity * 2 : 16;
      RaeFfmpegPacket* grown =
          (RaeFfmpegPacket*)realloc(encoder->packets, (size_t)capacity * sizeof(RaeFfmpegPacket));
      if (!grown) return -1;
      encoder->packets = grown;
      encoder->capacity = capacity;
    }
    RaeFfmpegPacket* packet = &encoder->packets[encoder->count];
    packet->size = encoder->packet->size;
    packet->bytes = (uint8_t*)malloc(packet->size ? (size_t)packet->size : 1);
    memcpy(packet->bytes, encoder->packet->data, (size_t)packet->size);
    packet->time = encoder->packet->pts;
    packet->decode_time = encoder->packet->dts == AV_NOPTS_VALUE ? packet->time : encoder->packet->dts;
    packet->keyframe = (encoder->packet->flags & AV_PKT_FLAG_KEY) ? 1 : 0;
    encoder->count += 1;
    p_av_packet_unref(encoder->packet);
  }
}

static int64_t rae_ffmpeg_encoder_encode(int64_t handle, uint8_t* planes, int64_t time) {
  RaeFfmpegEncoder* encoder = (RaeFfmpegEncoder*)(intptr_t)handle;
  if (!encoder || !planes) return -1;
  AVCodecContext* context = encoder->context;
  AVFrame* frame = p_av_frame_alloc();
  if (!frame) return -1;
  frame->format = context->pix_fmt;
  frame->width = context->width;
  frame->height = context->height;
  frame->pts = time;
  frame->color_primaries = AVCOL_PRI_BT709;
  frame->color_trc = AVCOL_TRC_BT709;
  frame->colorspace = AVCOL_SPC_BT709;
  frame->color_range = AVCOL_RANGE_MPEG;
  int status = p_av_frame_get_buffer(frame, 0);
  if (status < 0) {
    p_av_frame_free(&frame);
    return status;
  }
  int chroma_width = (context->width + 1) / 2;
  int chroma_rows = encoder->layout == 2 ? 2 : 1;
  int chroma_height = (context->height + chroma_rows - 1) / chroma_rows;
  int widths[3] = {context->width, chroma_width, chroma_width};
  int heights[3] = {context->height, chroma_height, chroma_height};
  const uint8_t* source = planes;
  for (int plane = 0; plane < 3; plane++) {
    for (int row = 0; row < heights[plane]; row++) {
      memcpy(frame->data[plane] + (size_t)row * (size_t)frame->linesize[plane], source, (size_t)widths[plane]);
      source += widths[plane];
    }
  }
  status = p_avcodec_send_frame(context, frame);
  p_av_frame_free(&frame);
  if (status < 0) return status;
  return rae_ffmpeg_encoder_drain(encoder);
}

static int64_t rae_ffmpeg_encoder_finish(int64_t handle) {
  RaeFfmpegEncoder* encoder = (RaeFfmpegEncoder*)(intptr_t)handle;
  if (!encoder) return -1;
  int status = p_avcodec_send_frame(encoder->context, NULL);
  if (status < 0 && status != AVERROR_EOF) return status;
  return rae_ffmpeg_encoder_drain(encoder);
}

static int64_t rae_ffmpeg_encoder_packet_count(int64_t handle) {
  RaeFfmpegEncoder* encoder = (RaeFfmpegEncoder*)(intptr_t)handle;
  return encoder ? encoder->count : 0;
}

static int64_t rae_ffmpeg_encoder_packet_info(int64_t handle, int64_t* out) {
  RaeFfmpegEncoder* encoder = (RaeFfmpegEncoder*)(intptr_t)handle;
  if (!encoder || !out || encoder->count == 0) return -1;
  out[0] = encoder->packets[0].size;
  out[1] = encoder->packets[0].time;
  out[2] = encoder->packets[0].decode_time;
  out[3] = encoder->packets[0].keyframe;
  return 0;
}

static int64_t rae_ffmpeg_encoder_take_packet(int64_t handle, uint8_t* into, int64_t capacity) {
  RaeFfmpegEncoder* encoder = (RaeFfmpegEncoder*)(intptr_t)handle;
  if (!encoder || !into || encoder->count == 0 || encoder->packets[0].size > capacity) return -1;
  int64_t size = encoder->packets[0].size;
  memcpy(into, encoder->packets[0].bytes, (size_t)size);
  free(encoder->packets[0].bytes);
  memmove(encoder->packets, encoder->packets + 1, (size_t)(encoder->count - 1) * sizeof(RaeFfmpegPacket));
  encoder->count -= 1;
  return size;
}

/* The extradata (FFV1's configuration), as MOV's 'glbl' box carries it */
static int64_t rae_ffmpeg_encoder_config_size(int64_t handle, int64_t* out) {
  RaeFfmpegEncoder* encoder = (RaeFfmpegEncoder*)(intptr_t)handle;
  if (out) out[0] = ((int64_t)'g' << 24) | ((int64_t)'l' << 16) | ((int64_t)'b' << 8) | (int64_t)'l';
  return encoder && encoder->context->extradata ? encoder->context->extradata_size : 0;
}

static int64_t rae_ffmpeg_encoder_copy_config(int64_t handle, uint8_t* into, int64_t capacity) {
  RaeFfmpegEncoder* encoder = (RaeFfmpegEncoder*)(intptr_t)handle;
  if (!encoder || !into || !encoder->context->extradata) return -1;
  int64_t size = encoder->context->extradata_size;
  if (size > capacity) return -1;
  memcpy(into, encoder->context->extradata, (size_t)size);
  return size;
}

static const RaeFfmpegEncoderCalls g_rae_ffmpeg_encoder_calls = {
    1,
    rae_ffmpeg_encoder_open,
    rae_ffmpeg_encoder_input_layout,
    rae_ffmpeg_encoder_encode,
    rae_ffmpeg_encoder_finish,
    rae_ffmpeg_encoder_packet_count,
    rae_ffmpeg_encoder_packet_info,
    rae_ffmpeg_encoder_take_packet,
    rae_ffmpeg_encoder_config_size,
    rae_ffmpeg_encoder_copy_config,
    rae_ffmpeg_encoder_close,
};

void* rae_ffmpeg_encoder_calls(void) {
  return rae_ffmpeg_available() ? (void*)&g_rae_ffmpeg_encoder_calls : NULL;
}
