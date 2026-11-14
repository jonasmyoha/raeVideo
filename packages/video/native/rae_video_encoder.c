/* The video package's encoder glue (rae_video.h, "VideoToolbox encoding"):
 * one VTCompressionSession per encoder, fed 32-bit BGRA frames (VideoToolbox
 * converts to the codec's Y'CbCr with the session's stated color tags), the
 * encoded packets kept in a queue the Rae side takes them from. Platform
 * reason: H.264, HEVC and ProRes are the platform's to encode (docs/design.md
 * §3.1). Which codec, which settings and what is done with the packets are Rae
 * (video/VideoEncoder). Loaded at run time, like the decoder. */
#include "rae_video.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>

#define RAE_ENCODER_FUNCTIONS(X)                       \
  X(CoreFoundation, CFRelease)                         \
  X(CoreFoundation, CFNumberCreate)                    \
  X(CoreFoundation, CFDictionaryCreateMutable)         \
  X(CoreFoundation, CFDictionarySetValue)              \
  X(CoreFoundation, CFDictionaryGetValue)              \
  X(CoreFoundation, CFArrayGetCount)                   \
  X(CoreFoundation, CFArrayGetValueAtIndex)            \
  X(CoreFoundation, CFDataGetLength)                   \
  X(CoreFoundation, CFDataGetBytePtr)                  \
  X(CoreFoundation, CFStringCreateWithCString)         \
  X(CoreMedia, CMSampleBufferGetDataBuffer)            \
  X(CoreMedia, CMSampleBufferGetPresentationTimeStamp) \
  X(CoreMedia, CMSampleBufferGetDecodeTimeStamp)       \
  X(CoreMedia, CMSampleBufferGetSampleAttachmentsArray) \
  X(CoreMedia, CMSampleBufferGetFormatDescription)     \
  X(CoreMedia, CMFormatDescriptionGetExtension)        \
  X(CoreMedia, CMBlockBufferGetDataLength)             \
  X(CoreMedia, CMBlockBufferCopyDataBytes)             \
  X(CoreVideo, CVPixelBufferPoolCreatePixelBuffer)     \
  X(CoreVideo, CVPixelBufferLockBaseAddress)           \
  X(CoreVideo, CVPixelBufferUnlockBaseAddress)         \
  X(CoreVideo, CVPixelBufferGetBaseAddress)            \
  X(CoreVideo, CVPixelBufferGetBytesPerRow)            \
  X(VideoToolbox, VTCompressionSessionCreate)          \
  X(VideoToolbox, VTCompressionSessionGetPixelBufferPool) \
  X(VideoToolbox, VTCompressionSessionEncodeFrame)     \
  X(VideoToolbox, VTCompressionSessionCompleteFrames)  \
  X(VideoToolbox, VTCompressionSessionInvalidate)      \
  X(VideoToolbox, VTSessionSetProperty)

#define RAE_ENCODER_CONSTANTS(X)                                          \
  X(CoreFoundation, CFDictionaryKeyCallBacks, kCFTypeDictionaryKeyCallBacks) \
  X(CoreFoundation, CFDictionaryValueCallBacks, kCFTypeDictionaryValueCallBacks) \
  X(CoreFoundation, CFBooleanRef, kCFBooleanTrue)                         \
  X(CoreFoundation, CFBooleanRef, kCFBooleanFalse)                        \
  X(CoreMedia, CFStringRef, kCMSampleAttachmentKey_NotSync)               \
  X(CoreMedia, CFStringRef, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms) \
  X(CoreVideo, CFStringRef, kCVPixelBufferPixelFormatTypeKey)             \
  X(CoreVideo, CFStringRef, kCVPixelBufferWidthKey)                       \
  X(CoreVideo, CFStringRef, kCVPixelBufferHeightKey)                      \
  X(CoreVideo, CFStringRef, kCVPixelBufferIOSurfacePropertiesKey)         \
  X(CoreVideo, CFStringRef, kCVImageBufferColorPrimaries_ITU_R_709_2)     \
  X(CoreVideo, CFStringRef, kCVImageBufferTransferFunction_ITU_R_709_2)   \
  X(CoreVideo, CFStringRef, kCVImageBufferYCbCrMatrix_ITU_R_709_2)        \
  X(VideoToolbox, CFStringRef, kVTCompressionPropertyKey_RealTime)        \
  X(VideoToolbox, CFStringRef, kVTCompressionPropertyKey_AllowFrameReordering) \
  X(VideoToolbox, CFStringRef, kVTCompressionPropertyKey_AverageBitRate)  \
  X(VideoToolbox, CFStringRef, kVTCompressionPropertyKey_Quality)         \
  X(VideoToolbox, CFStringRef, kVTCompressionPropertyKey_MaxKeyFrameInterval) \
  X(VideoToolbox, CFStringRef, kVTCompressionPropertyKey_ProfileLevel)    \
  X(VideoToolbox, CFStringRef, kVTCompressionPropertyKey_ColorPrimaries)  \
  X(VideoToolbox, CFStringRef, kVTCompressionPropertyKey_TransferFunction) \
  X(VideoToolbox, CFStringRef, kVTCompressionPropertyKey_YCbCrMatrix)     \
  X(VideoToolbox, CFStringRef, kVTProfileLevel_H264_High_AutoLevel)       \
  X(VideoToolbox, CFStringRef, kVTProfileLevel_HEVC_Main_AutoLevel)

#define RAE_ENCODER_DECLARE_FUNCTION(library, name) static __typeof__(&name) e_##name;
#define RAE_ENCODER_DECLARE_CONSTANT(library, type, name) static const type* k_##name;
RAE_ENCODER_FUNCTIONS(RAE_ENCODER_DECLARE_FUNCTION)
RAE_ENCODER_CONSTANTS(RAE_ENCODER_DECLARE_CONSTANT)

static pthread_once_t g_rae_encoder_once = PTHREAD_ONCE_INIT;
static int g_rae_encoder_ready = 0;

static void* rae_encoder_framework(const char* name) {
  char path[256];
  snprintf(path, sizeof(path), "/System/Library/Frameworks/%s.framework/%s", name, name);
  return dlopen(path, RTLD_LAZY);
}

static void rae_encoder_load(void) {
  void* CoreFoundation = rae_encoder_framework("CoreFoundation");
  void* CoreMedia = rae_encoder_framework("CoreMedia");
  void* CoreVideo = rae_encoder_framework("CoreVideo");
  void* VideoToolbox = rae_encoder_framework("VideoToolbox");
  if (!CoreFoundation || !CoreMedia || !CoreVideo || !VideoToolbox) return;
  int ready = 1;
#define RAE_ENCODER_LOAD_FUNCTION(library, name)           \
  e_##name = (__typeof__(&name))dlsym(library, #name);     \
  if (!e_##name) ready = 0;
#define RAE_ENCODER_LOAD_CONSTANT(library, type, name)     \
  k_##name = (const type*)dlsym(library, #name);           \
  if (!k_##name) ready = 0;
  RAE_ENCODER_FUNCTIONS(RAE_ENCODER_LOAD_FUNCTION)
  RAE_ENCODER_CONSTANTS(RAE_ENCODER_LOAD_CONSTANT)
  g_rae_encoder_ready = ready;
}

static int rae_encoder_ready(void) {
  pthread_once(&g_rae_encoder_once, rae_encoder_load);
  return g_rae_encoder_ready;
}

typedef struct {
  uint8_t* bytes;
  int64_t size;
  int64_t time;
  int64_t decode_time;
  int64_t keyframe;
} RaeEncodedPacket;

typedef struct {
  VTCompressionSessionRef session;
  int64_t width;
  int64_t height;
  int64_t timescale;
  /* the packets VideoToolbox has handed out and the caller has not taken;
   * its callback runs on VideoToolbox's thread */
  pthread_mutex_t lock;
  RaeEncodedPacket* packets;
  int64_t count;
  int64_t capacity;
  int64_t failures;
  /* the codec configuration (avcC / hvcC payload) of the first packet */
  uint8_t* config;
  int64_t config_size;
  char config_kind[5];
} RaeVideoEncoder;

static void rae_encoder_config(RaeVideoEncoder* encoder, CMSampleBufferRef sample) {
  if (encoder->config) return;
  CMFormatDescriptionRef format = e_CMSampleBufferGetFormatDescription(sample);
  if (!format) return;
  CFDictionaryRef atoms = (CFDictionaryRef)e_CMFormatDescriptionGetExtension(
      format, *k_kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
  if (!atoms) return;
  const char* kinds[2] = {"avcC", "hvcC"};
  for (int i = 0; i < 2; i++) {
    CFStringRef key = e_CFStringCreateWithCString(NULL, kinds[i], kCFStringEncodingASCII);
    CFDataRef data = (CFDataRef)e_CFDictionaryGetValue(atoms, key);
    e_CFRelease(key);
    if (data) {
      encoder->config_size = (int64_t)e_CFDataGetLength(data);
      encoder->config = (uint8_t*)malloc((size_t)encoder->config_size);
      if (encoder->config) memcpy(encoder->config, e_CFDataGetBytePtr(data), (size_t)encoder->config_size);
      memcpy(encoder->config_kind, kinds[i], 5);
      return;
    }
  }
}

static int64_t rae_encoder_time(CMTime time, int64_t timescale) {
  if (!(time.flags & kCMTimeFlags_Valid) || time.timescale <= 0) return INT64_MIN;
  if (time.timescale == timescale) return time.value;
  return time.value * timescale / time.timescale;
}

static void rae_encoder_output(void* encoder_pointer, void* source, OSStatus status,
                               VTEncodeInfoFlags flags, CMSampleBufferRef sample) {
  (void)source; (void)flags;
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)encoder_pointer;
  pthread_mutex_lock(&encoder->lock);
  if (status != noErr || !sample) {
    encoder->failures += 1;
    pthread_mutex_unlock(&encoder->lock);
    return;
  }
  rae_encoder_config(encoder, sample);
  CMBlockBufferRef block = e_CMSampleBufferGetDataBuffer(sample);
  size_t size = block ? e_CMBlockBufferGetDataLength(block) : 0;
  int64_t keyframe = 1;
  CFArrayRef attachments = e_CMSampleBufferGetSampleAttachmentsArray(sample, 0);
  if (attachments && e_CFArrayGetCount(attachments) > 0) {
    CFDictionaryRef first = (CFDictionaryRef)e_CFArrayGetValueAtIndex(attachments, 0);
    CFTypeRef notSync = e_CFDictionaryGetValue(first, *k_kCMSampleAttachmentKey_NotSync);
    if (notSync == (CFTypeRef)*k_kCFBooleanTrue) keyframe = 0;
  }
  if (encoder->count == encoder->capacity) {
    int64_t capacity = encoder->capacity ? encoder->capacity * 2 : 16;
    RaeEncodedPacket* grown =
        (RaeEncodedPacket*)realloc(encoder->packets, (size_t)capacity * sizeof(RaeEncodedPacket));
    if (!grown) {
      encoder->failures += 1;
      pthread_mutex_unlock(&encoder->lock);
      return;
    }
    encoder->packets = grown;
    encoder->capacity = capacity;
  }
  RaeEncodedPacket* packet = &encoder->packets[encoder->count];
  packet->bytes = (uint8_t*)malloc(size ? size : 1);
  packet->size = (int64_t)size;
  if (size) e_CMBlockBufferCopyDataBytes(block, 0, size, packet->bytes);
  packet->time = rae_encoder_time(e_CMSampleBufferGetPresentationTimeStamp(sample), encoder->timescale);
  int64_t decode = rae_encoder_time(e_CMSampleBufferGetDecodeTimeStamp(sample), encoder->timescale);
  packet->decode_time = decode == INT64_MIN ? packet->time : decode;
  packet->keyframe = keyframe;
  encoder->count += 1;
  pthread_mutex_unlock(&encoder->lock);
}

static void rae_encoder_set_number(RaeVideoEncoder* encoder, CFStringRef key, int32_t value) {
  CFNumberRef number = e_CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
  e_VTSessionSetProperty(encoder->session, key, number);
  e_CFRelease(number);
}

int64_t rae_video_vte_open(int64_t codec, int64_t width, int64_t height, int64_t timescale,
                           int64_t bit_rate, int64_t quality_percent, int64_t keyframe_interval) {
  if (!rae_encoder_ready()) return 0;
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)calloc(1, sizeof(RaeVideoEncoder));
  if (!encoder) return 0;
  pthread_mutex_init(&encoder->lock, NULL);
  encoder->width = width;
  encoder->height = height;
  encoder->timescale = timescale;
  /* the frames it is given: 32-bit BGRA, IOSurface-backed */
  CFMutableDictionaryRef attributes = e_CFDictionaryCreateMutable(
      NULL, 0, k_kCFTypeDictionaryKeyCallBacks, k_kCFTypeDictionaryValueCallBacks);
  int32_t format = kCVPixelFormatType_32BGRA;
  int32_t w = (int32_t)width;
  int32_t h = (int32_t)height;
  CFNumberRef formatNumber = e_CFNumberCreate(NULL, kCFNumberSInt32Type, &format);
  CFNumberRef widthNumber = e_CFNumberCreate(NULL, kCFNumberSInt32Type, &w);
  CFNumberRef heightNumber = e_CFNumberCreate(NULL, kCFNumberSInt32Type, &h);
  CFMutableDictionaryRef surface = e_CFDictionaryCreateMutable(
      NULL, 0, k_kCFTypeDictionaryKeyCallBacks, k_kCFTypeDictionaryValueCallBacks);
  e_CFDictionarySetValue(attributes, *k_kCVPixelBufferPixelFormatTypeKey, formatNumber);
  e_CFDictionarySetValue(attributes, *k_kCVPixelBufferWidthKey, widthNumber);
  e_CFDictionarySetValue(attributes, *k_kCVPixelBufferHeightKey, heightNumber);
  e_CFDictionarySetValue(attributes, *k_kCVPixelBufferIOSurfacePropertiesKey, surface);
  OSStatus status = e_VTCompressionSessionCreate(NULL, (int32_t)width, (int32_t)height,
                                                 (CMVideoCodecType)codec, NULL, attributes, NULL,
                                                 rae_encoder_output, encoder, &encoder->session);
  e_CFRelease(formatNumber);
  e_CFRelease(widthNumber);
  e_CFRelease(heightNumber);
  e_CFRelease(surface);
  e_CFRelease(attributes);
  if (status != noErr || !encoder->session) {
    pthread_mutex_destroy(&encoder->lock);
    free(encoder);
    return 0;
  }
  e_VTSessionSetProperty(encoder->session, *k_kVTCompressionPropertyKey_RealTime, *k_kCFBooleanFalse);
  e_VTSessionSetProperty(encoder->session, *k_kVTCompressionPropertyKey_AllowFrameReordering,
                         *k_kCFBooleanFalse);
  if (codec == 'avc1') {
    e_VTSessionSetProperty(encoder->session, *k_kVTCompressionPropertyKey_ProfileLevel,
                           *k_kVTProfileLevel_H264_High_AutoLevel);
  } else if (codec == 'hvc1') {
    e_VTSessionSetProperty(encoder->session, *k_kVTCompressionPropertyKey_ProfileLevel,
                           *k_kVTProfileLevel_HEVC_Main_AutoLevel);
  }
  if (bit_rate > 0) rae_encoder_set_number(encoder, *k_kVTCompressionPropertyKey_AverageBitRate, (int32_t)bit_rate);
  if (keyframe_interval > 0) {
    rae_encoder_set_number(encoder, *k_kVTCompressionPropertyKey_MaxKeyFrameInterval,
                           (int32_t)keyframe_interval);
  }
  if (quality_percent > 0) {
    float quality = (float)quality_percent / 100.0f;
    CFNumberRef number = e_CFNumberCreate(NULL, kCFNumberFloat32Type, &quality);
    e_VTSessionSetProperty(encoder->session, *k_kVTCompressionPropertyKey_Quality, number);
    e_CFRelease(number);
  }
  e_VTSessionSetProperty(encoder->session, *k_kVTCompressionPropertyKey_ColorPrimaries,
                         *k_kCVImageBufferColorPrimaries_ITU_R_709_2);
  e_VTSessionSetProperty(encoder->session, *k_kVTCompressionPropertyKey_TransferFunction,
                         *k_kCVImageBufferTransferFunction_ITU_R_709_2);
  e_VTSessionSetProperty(encoder->session, *k_kVTCompressionPropertyKey_YCbCrMatrix,
                         *k_kCVImageBufferYCbCrMatrix_ITU_R_709_2);
  return (int64_t)(intptr_t)encoder;
}

int64_t rae_video_vte_encode(int64_t handle, uint8_t* bgra, int64_t bytes_per_row, int64_t time) {
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)(intptr_t)handle;
  if (!encoder || !bgra) return -1;
  CVPixelBufferPoolRef pool = e_VTCompressionSessionGetPixelBufferPool(encoder->session);
  CVPixelBufferRef buffer = NULL;
  if (!pool || e_CVPixelBufferPoolCreatePixelBuffer(NULL, pool, &buffer) != kCVReturnSuccess || !buffer) {
    return -2;
  }
  e_CVPixelBufferLockBaseAddress(buffer, 0);
  uint8_t* base = (uint8_t*)e_CVPixelBufferGetBaseAddress(buffer);
  size_t stride = e_CVPixelBufferGetBytesPerRow(buffer);
  size_t row_bytes = (size_t)encoder->width * 4;
  for (int64_t row = 0; row < encoder->height; row++) {
    memcpy(base + (size_t)row * stride, bgra + row * bytes_per_row, row_bytes);
  }
  e_CVPixelBufferUnlockBaseAddress(buffer, 0);
  CMTime presentation = {time, (int32_t)encoder->timescale, kCMTimeFlags_Valid, 0};
  CMTime none = {0, 0, 0, 0};
  OSStatus status = e_VTCompressionSessionEncodeFrame(encoder->session, buffer, presentation, none,
                                                      NULL, NULL, NULL);
  e_CFRelease(buffer);
  return status == noErr ? 0 : (int64_t)status;
}

/* Waits until every frame given has come out */
int64_t rae_video_vte_finish(int64_t handle) {
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)(intptr_t)handle;
  if (!encoder) return -1;
  CMTime all = {0, 0, 0, 0};
  return (int64_t)e_VTCompressionSessionCompleteFrames(encoder->session, all);
}

int64_t rae_video_vte_packet_count(int64_t handle) {
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)(intptr_t)handle;
  if (!encoder) return 0;
  pthread_mutex_lock(&encoder->lock);
  int64_t count = encoder->count;
  pthread_mutex_unlock(&encoder->lock);
  return count;
}

/* The oldest packet: its size, times and keyframe flag (out[0..3]); the
 * bytes with rae_video_vte_take_packet */
int64_t rae_video_vte_packet_info(int64_t handle, int64_t* out) {
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)(intptr_t)handle;
  if (!encoder || !out) return -1;
  pthread_mutex_lock(&encoder->lock);
  int64_t result = -1;
  if (encoder->count > 0) {
    out[0] = encoder->packets[0].size;
    out[1] = encoder->packets[0].time;
    out[2] = encoder->packets[0].decode_time;
    out[3] = encoder->packets[0].keyframe;
    result = 0;
  }
  pthread_mutex_unlock(&encoder->lock);
  return result;
}

/* Copies the oldest packet's bytes into `into` and drops it */
int64_t rae_video_vte_take_packet(int64_t handle, uint8_t* into, int64_t capacity) {
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)(intptr_t)handle;
  if (!encoder || !into) return -1;
  pthread_mutex_lock(&encoder->lock);
  int64_t copied = -1;
  if (encoder->count > 0 && encoder->packets[0].size <= capacity) {
    copied = encoder->packets[0].size;
    memcpy(into, encoder->packets[0].bytes, (size_t)copied);
    free(encoder->packets[0].bytes);
    memmove(encoder->packets, encoder->packets + 1, (size_t)(encoder->count - 1) * sizeof(RaeEncodedPacket));
    encoder->count -= 1;
  }
  pthread_mutex_unlock(&encoder->lock);
  return copied;
}

int64_t rae_video_vte_failures(int64_t handle) {
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)(intptr_t)handle;
  return encoder ? encoder->failures : 0;
}

/* The codec configuration's size, its four characters as an integer in out[0]
 * ('avcC', 'hvcC'; 0 for none) */
int64_t rae_video_vte_config_size(int64_t handle, int64_t* out) {
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)(intptr_t)handle;
  if (!encoder || !encoder->config) {
    if (out) out[0] = 0;
    return 0;
  }
  if (out) {
    out[0] = ((int64_t)encoder->config_kind[0] << 24) | ((int64_t)encoder->config_kind[1] << 16) |
             ((int64_t)encoder->config_kind[2] << 8) | (int64_t)encoder->config_kind[3];
  }
  return encoder->config_size;
}

int64_t rae_video_vte_copy_config(int64_t handle, uint8_t* into, int64_t capacity) {
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)(intptr_t)handle;
  if (!encoder || !encoder->config || !into || capacity < encoder->config_size) return -1;
  memcpy(into, encoder->config, (size_t)encoder->config_size);
  return encoder->config_size;
}

void rae_video_vte_close(int64_t handle) {
  RaeVideoEncoder* encoder = (RaeVideoEncoder*)(intptr_t)handle;
  if (!encoder) return;
  if (encoder->session) {
    e_VTCompressionSessionInvalidate(encoder->session);
    e_CFRelease(encoder->session);
  }
  for (int64_t i = 0; i < encoder->count; i++) free(encoder->packets[i].bytes);
  free(encoder->packets);
  free(encoder->config);
  pthread_mutex_destroy(&encoder->lock);
  free(encoder);
}

#else
int64_t rae_video_vte_open(int64_t codec, int64_t width, int64_t height, int64_t timescale,
                           int64_t bit_rate, int64_t quality_percent, int64_t keyframe_interval) {
  (void)codec; (void)width; (void)height; (void)timescale; (void)bit_rate; (void)quality_percent;
  (void)keyframe_interval;
  return 0;
}
int64_t rae_video_vte_encode(int64_t handle, uint8_t* bgra, int64_t bytes_per_row, int64_t time) {
  (void)handle; (void)bgra; (void)bytes_per_row; (void)time;
  return -1;
}
int64_t rae_video_vte_finish(int64_t handle) { (void)handle; return -1; }
int64_t rae_video_vte_packet_count(int64_t handle) { (void)handle; return 0; }
int64_t rae_video_vte_packet_info(int64_t handle, int64_t* out) { (void)handle; (void)out; return -1; }
int64_t rae_video_vte_take_packet(int64_t handle, uint8_t* into, int64_t capacity) {
  (void)handle; (void)into; (void)capacity;
  return -1;
}
int64_t rae_video_vte_failures(int64_t handle) { (void)handle; return 0; }
int64_t rae_video_vte_config_size(int64_t handle, int64_t* out) {
  (void)handle;
  if (out) out[0] = 0;
  return 0;
}
int64_t rae_video_vte_copy_config(int64_t handle, uint8_t* into, int64_t capacity) {
  (void)handle; (void)into; (void)capacity;
  return -1;
}
void rae_video_vte_close(int64_t handle) { (void)handle; }
#endif

/* ---- files for writing ------------------------------------------------------ */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int64_t rae_video_file_create(uint8_t* path_z) {
  int file = open((const char*)path_z, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  return file >= 0 ? (int64_t)file : -(int64_t)errno;
}

int64_t rae_video_file_write_at(int64_t file, uint8_t* bytes, int64_t count, int64_t offset) {
  int64_t done = 0;
  while (done < count) {
    ssize_t wrote = pwrite((int)file, bytes + done, (size_t)(count - done), (off_t)(offset + done));
    if (wrote < 0) {
      if (errno == EINTR) continue;
      return -(int64_t)errno;
    }
    done += wrote;
  }
  return done;
}
