/* The video package's C (rae_video.h): files, and VideoToolbox decoding on
 * Apple. Platform reason: H.264, HEVC and ProRes are the platform's to decode
 * (docs/design.md §3.1: the platform carries the patent licenses, and no
 * permissive decoder exists). Each function makes one platform call or one
 * small group of them that only makes sense together; the policy is Rae. */
#include "rae_video.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- files ---------------------------------------------------------------- */
int64_t rae_video_file_open(uint8_t* path_z) {
  int file = open((const char*)path_z, O_RDONLY);
  return file >= 0 ? (int64_t)file : -(int64_t)errno;
}

int64_t rae_video_file_size(int64_t file) {
  struct stat info;
  if (fstat((int)file, &info) != 0) return -(int64_t)errno;
  return (int64_t)info.st_size;
}

int64_t rae_video_file_read_at(int64_t file, uint8_t* into, int64_t count, int64_t offset) {
  int64_t done = 0;
  while (done < count) {
    ssize_t got = pread((int)file, into + done, (size_t)(count - done), (off_t)(offset + done));
    if (got < 0) {
      if (errno == EINTR) continue;
      return -(int64_t)errno;
    }
    if (got == 0) break;
    done += got;
  }
  return done;
}

void rae_video_file_close(int64_t file) {
  if (file >= 0) close((int)file);
}

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <dlfcn.h>
#include <pthread.h>

/* Every function and constant the glue uses, looked up once. The headers give
 * the types; nothing here names a symbol the linker would have to find. */
#define RAE_VIDEO_FUNCTIONS(X)                    \
  X(CoreFoundation, CFRelease)                    \
  X(CoreFoundation, CFRetain)                     \
  X(CoreFoundation, CFEqual)                      \
  X(CoreFoundation, CFDataCreate)                 \
  X(CoreFoundation, CFNumberCreate)               \
  X(CoreFoundation, CFArrayCreate)                \
  X(CoreFoundation, CFStringCreateWithCString)    \
  X(CoreFoundation, CFDictionaryCreateMutable)    \
  X(CoreFoundation, CFDictionarySetValue)         \
  X(CoreMedia, CMVideoFormatDescriptionCreate)    \
  X(CoreMedia, CMBlockBufferCreateWithMemoryBlock) \
  X(CoreMedia, CMSampleBufferCreateReady)         \
  X(VideoToolbox, VTDecompressionSessionCreate)   \
  X(VideoToolbox, VTDecompressionSessionDecodeFrame) \
  X(VideoToolbox, VTDecompressionSessionInvalidate) \
  X(CoreVideo, CVPixelBufferGetWidth)             \
  X(CoreVideo, CVPixelBufferGetHeight)            \
  X(CoreVideo, CVPixelBufferGetPixelFormatType)   \
  X(CoreVideo, CVPixelBufferGetPlaneCount)        \
  X(CoreVideo, CVPixelBufferGetWidthOfPlane)      \
  X(CoreVideo, CVPixelBufferGetHeightOfPlane)     \
  X(CoreVideo, CVPixelBufferGetBytesPerRowOfPlane) \
  X(CoreVideo, CVPixelBufferGetBaseAddressOfPlane) \
  X(CoreVideo, CVPixelBufferLockBaseAddress)      \
  X(CoreVideo, CVPixelBufferUnlockBaseAddress)    \
  X(CoreVideo, CVBufferCopyAttachment)

#define RAE_VIDEO_CONSTANTS(X)                                      \
  X(CoreFoundation, CFDictionaryKeyCallBacks, kCFTypeDictionaryKeyCallBacks) \
  X(CoreFoundation, CFDictionaryValueCallBacks, kCFTypeDictionaryValueCallBacks) \
  X(CoreFoundation, CFArrayCallBacks, kCFTypeArrayCallBacks)        \
  X(CoreFoundation, CFAllocatorRef, kCFAllocatorNull)               \
  X(CoreFoundation, CFBooleanRef, kCFBooleanTrue)                  \
  X(CoreFoundation, CFBooleanRef, kCFBooleanFalse)                 \
  X(CoreMedia, CFStringRef, kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms) \
  X(CoreMedia, CFStringRef, kCMFormatDescriptionExtension_FullRangeVideo) \
  X(CoreVideo, CFStringRef, kCVPixelBufferPixelFormatTypeKey)       \
  X(CoreVideo, CFStringRef, kCVImageBufferYCbCrMatrixKey)           \
  X(CoreVideo, CFStringRef, kCVImageBufferYCbCrMatrix_ITU_R_709_2)  \
  X(CoreVideo, CFStringRef, kCVImageBufferYCbCrMatrix_ITU_R_601_4)  \
  X(CoreVideo, CFStringRef, kCVImageBufferYCbCrMatrix_ITU_R_2020)   \
  X(CoreVideo, CFStringRef, kCVImageBufferYCbCrMatrix_SMPTE_240M_1995) \
  X(CoreVideo, CFStringRef, kCVImageBufferColorPrimariesKey)        \
  X(CoreVideo, CFStringRef, kCVImageBufferColorPrimaries_ITU_R_709_2) \
  X(CoreVideo, CFStringRef, kCVImageBufferColorPrimaries_ITU_R_2020) \
  X(CoreVideo, CFStringRef, kCVImageBufferColorPrimaries_P3_D65)    \
  X(CoreVideo, CFStringRef, kCVImageBufferColorPrimaries_DCI_P3)    \
  X(CoreVideo, CFStringRef, kCVImageBufferColorPrimaries_EBU_3213)  \
  X(CoreVideo, CFStringRef, kCVImageBufferColorPrimaries_SMPTE_C)   \
  X(CoreVideo, CFStringRef, kCVImageBufferTransferFunctionKey)      \
  X(CoreVideo, CFStringRef, kCVImageBufferTransferFunction_ITU_R_709_2) \
  X(CoreVideo, CFStringRef, kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ) \
  X(CoreVideo, CFStringRef, kCVImageBufferTransferFunction_ITU_R_2100_HLG) \
  X(CoreVideo, CFStringRef, kCVImageBufferTransferFunction_sRGB)    \
  X(CoreVideo, CFStringRef, kCVImageBufferTransferFunction_Linear)

#define RAE_VIDEO_DECLARE_FUNCTION(library, name) static __typeof__(&name) p_##name;
#define RAE_VIDEO_DECLARE_CONSTANT(library, type, name) static const type* c_##name;
RAE_VIDEO_FUNCTIONS(RAE_VIDEO_DECLARE_FUNCTION)
RAE_VIDEO_CONSTANTS(RAE_VIDEO_DECLARE_CONSTANT)

static pthread_once_t g_rae_video_once = PTHREAD_ONCE_INIT;
static int g_rae_video_ready = 0;

static void* rae_video_framework(const char* name) {
  char path[256];
  snprintf(path, sizeof(path), "/System/Library/Frameworks/%s.framework/%s", name, name);
  return dlopen(path, RTLD_LAZY);
}

static void rae_video_load(void) {
  void* CoreFoundation = rae_video_framework("CoreFoundation");
  void* CoreMedia = rae_video_framework("CoreMedia");
  void* CoreVideo = rae_video_framework("CoreVideo");
  void* VideoToolbox = rae_video_framework("VideoToolbox");
  if (!CoreFoundation || !CoreMedia || !CoreVideo || !VideoToolbox) return;
  int ready = 1;
#define RAE_VIDEO_LOAD_FUNCTION(library, name)                 \
  p_##name = (__typeof__(&name))dlsym(library, #name);         \
  if (!p_##name) ready = 0;
#define RAE_VIDEO_LOAD_CONSTANT(library, type, name)           \
  c_##name = (const type*)dlsym(library, #name);
  RAE_VIDEO_FUNCTIONS(RAE_VIDEO_LOAD_FUNCTION)
  RAE_VIDEO_CONSTANTS(RAE_VIDEO_LOAD_CONSTANT)
  /* the constants every decoder needs; the tag values may be missing on an
   * older system and then simply never match */
  if (!c_kCFTypeDictionaryKeyCallBacks || !c_kCFTypeDictionaryValueCallBacks ||
      !c_kCFTypeArrayCallBacks || !c_kCFAllocatorNull ||
      !c_kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms ||
      !c_kCVPixelBufferPixelFormatTypeKey) {
    ready = 0;
  }
  g_rae_video_ready = ready;
}

static int rae_video_ready(void) {
  pthread_once(&g_rae_video_once, rae_video_load);
  return g_rae_video_ready;
}

int64_t rae_video_vt_available(void) { return rae_video_ready() ? 1 : 0; }

/* The tags for the next rae_video_vt_open on this thread (-1: not given) */
static _Thread_local int64_t g_rae_video_tags[4] = {-1, -1, -1, -1};

void rae_video_vt_set_color_tags(int64_t primaries, int64_t transfer, int64_t matrix,
                                 int64_t full_range) {
  g_rae_video_tags[0] = primaries;
  g_rae_video_tags[1] = transfer;
  g_rae_video_tags[2] = matrix;
  g_rae_video_tags[3] = full_range;
}

/* H.273 code points to CoreVideo's tag values (the same strings CoreMedia's
 * format description extensions take) */
static const CFStringRef* rae_video_primaries_value(int64_t code) {
  switch (code) {
    case 1: return c_kCVImageBufferColorPrimaries_ITU_R_709_2;
    case 5: return c_kCVImageBufferColorPrimaries_EBU_3213;
    case 6: return c_kCVImageBufferColorPrimaries_SMPTE_C;
    case 9: return c_kCVImageBufferColorPrimaries_ITU_R_2020;
    case 11: return c_kCVImageBufferColorPrimaries_DCI_P3;
    case 12: return c_kCVImageBufferColorPrimaries_P3_D65;
    default: return NULL;
  }
}

static const CFStringRef* rae_video_transfer_value(int64_t code) {
  switch (code) {
    case 1: case 6: case 14: case 15: return c_kCVImageBufferTransferFunction_ITU_R_709_2;
    case 8: return c_kCVImageBufferTransferFunction_Linear;
    case 13: return c_kCVImageBufferTransferFunction_sRGB;
    case 16: return c_kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ;
    case 18: return c_kCVImageBufferTransferFunction_ITU_R_2100_HLG;
    default: return NULL;
  }
}

static const CFStringRef* rae_video_matrix_value(int64_t code) {
  switch (code) {
    case 1: return c_kCVImageBufferYCbCrMatrix_ITU_R_709_2;
    case 5: case 6: return c_kCVImageBufferYCbCrMatrix_ITU_R_601_4;
    case 7: return c_kCVImageBufferYCbCrMatrix_SMPTE_240M_1995;
    case 9: return c_kCVImageBufferYCbCrMatrix_ITU_R_2020;
    default: return NULL;
  }
}

static void rae_video_set_tag(CFMutableDictionaryRef extensions, const CFStringRef* key,
                              const CFStringRef* value) {
  if (key && value && *value) p_CFDictionarySetValue(extensions, *key, *value);
}

typedef struct {
  VTDecompressionSessionRef session;
  CMVideoFormatDescriptionRef format;
  /* the frame the last decode call gave (retained), handed to the caller */
  CVImageBufferRef frame;
  OSStatus status;
} RaeVideoDecoder;

static void rae_video_decoded(void* decoder_pointer, void* source, OSStatus status,
                              VTDecodeInfoFlags flags, CVImageBufferRef image, CMTime time,
                              CMTime duration) {
  (void)source; (void)flags; (void)time; (void)duration;
  RaeVideoDecoder* decoder = (RaeVideoDecoder*)decoder_pointer;
  decoder->status = status;
  if (status != noErr || !image) return;
  if (decoder->frame) p_CFRelease(decoder->frame);
  decoder->frame = (CVImageBufferRef)p_CFRetain(image);
}

static CFNumberRef rae_video_number(int32_t value) {
  return p_CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
}

static void rae_video_fourcc_text(int64_t code, char text[5]) {
  text[0] = (char)((code >> 24) & 255);
  text[1] = (char)((code >> 16) & 255);
  text[2] = (char)((code >> 8) & 255);
  text[3] = (char)(code & 255);
  text[4] = 0;
}

int64_t rae_video_vt_open(int64_t codec, int64_t width, int64_t height, uint8_t* config,
                          int64_t config_size, int64_t config_kind) {
  if (!rae_video_ready()) return 0;
  CFMutableDictionaryRef extensions = p_CFDictionaryCreateMutable(
      NULL, 0, c_kCFTypeDictionaryKeyCallBacks, c_kCFTypeDictionaryValueCallBacks);
  if (config_size > 0 && config) {
    CFMutableDictionaryRef atoms = p_CFDictionaryCreateMutable(
        NULL, 0, c_kCFTypeDictionaryKeyCallBacks, c_kCFTypeDictionaryValueCallBacks);
    char kind[5];
    rae_video_fourcc_text(config_kind, kind);
    CFStringRef key = p_CFStringCreateWithCString(NULL, kind, kCFStringEncodingASCII);
    CFDataRef data = p_CFDataCreate(NULL, config, (CFIndex)config_size);
    p_CFDictionarySetValue(atoms, key, data);
    p_CFDictionarySetValue(extensions, *c_kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms,
                           atoms);
    p_CFRelease(key);
    p_CFRelease(data);
    p_CFRelease(atoms);
  }
  rae_video_set_tag(extensions, c_kCVImageBufferColorPrimariesKey,
                    rae_video_primaries_value(g_rae_video_tags[0]));
  rae_video_set_tag(extensions, c_kCVImageBufferTransferFunctionKey,
                    rae_video_transfer_value(g_rae_video_tags[1]));
  rae_video_set_tag(extensions, c_kCVImageBufferYCbCrMatrixKey,
                    rae_video_matrix_value(g_rae_video_tags[2]));
  if (g_rae_video_tags[3] >= 0 && c_kCMFormatDescriptionExtension_FullRangeVideo &&
      c_kCFBooleanTrue && c_kCFBooleanFalse) {
    p_CFDictionarySetValue(extensions, *c_kCMFormatDescriptionExtension_FullRangeVideo,
                           g_rae_video_tags[3] ? *c_kCFBooleanTrue : *c_kCFBooleanFalse);
  }
  g_rae_video_tags[0] = g_rae_video_tags[1] = g_rae_video_tags[2] = g_rae_video_tags[3] = -1;
  CMVideoFormatDescriptionRef format = NULL;
  OSStatus status = p_CMVideoFormatDescriptionCreate(NULL, (CMVideoCodecType)codec, (int32_t)width,
                                                     (int32_t)height, extensions, &format);
  p_CFRelease(extensions);
  if (status != noErr || !format) return 0;

  /* NV12, limited or full range as the stream is: VideoToolbox takes the
   * first of the list it can give without converting the range */
  CFNumberRef formats[2] = {
      rae_video_number(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
      rae_video_number(kCVPixelFormatType_420YpCbCr8BiPlanarFullRange),
  };
  CFArrayRef formatList = p_CFArrayCreate(NULL, (const void**)formats, 2, c_kCFTypeArrayCallBacks);
  CFMutableDictionaryRef attributes = p_CFDictionaryCreateMutable(
      NULL, 0, c_kCFTypeDictionaryKeyCallBacks, c_kCFTypeDictionaryValueCallBacks);
  p_CFDictionarySetValue(attributes, *c_kCVPixelBufferPixelFormatTypeKey, formatList);
  p_CFRelease(formats[0]);
  p_CFRelease(formats[1]);
  p_CFRelease(formatList);

  RaeVideoDecoder* decoder = (RaeVideoDecoder*)calloc(1, sizeof(RaeVideoDecoder));
  if (!decoder) {
    p_CFRelease(attributes);
    p_CFRelease(format);
    return 0;
  }
  VTDecompressionOutputCallbackRecord callback = {rae_video_decoded, decoder};
  status = p_VTDecompressionSessionCreate(NULL, format, NULL, attributes, &callback,
                                          &decoder->session);
  p_CFRelease(attributes);
  if (status != noErr || !decoder->session) {
    p_CFRelease(format);
    free(decoder);
    return 0;
  }
  decoder->format = format;
  return (int64_t)(intptr_t)decoder;
}

int64_t rae_video_vt_decode(int64_t decoder_handle, uint8_t* packet, int64_t size, int64_t time,
                            int64_t timescale) {
  RaeVideoDecoder* decoder = (RaeVideoDecoder*)(intptr_t)decoder_handle;
  if (!decoder || !packet || size <= 0) return -1;
  CMBlockBufferRef block = NULL;
  /* the packet's memory is only borrowed: the decode is synchronous */
  OSStatus status = p_CMBlockBufferCreateWithMemoryBlock(NULL, packet, (size_t)size,
                                                         *c_kCFAllocatorNull, NULL, 0,
                                                         (size_t)size, 0, &block);
  if (status != noErr) return (int64_t)status;
  CMSampleTimingInfo timing;
  memset(&timing, 0, sizeof(timing));
  timing.presentationTimeStamp.value = time;
  timing.presentationTimeStamp.timescale = (int32_t)timescale;
  timing.presentationTimeStamp.flags = kCMTimeFlags_Valid;
  size_t sampleSize = (size_t)size;
  CMSampleBufferRef sample = NULL;
  status = p_CMSampleBufferCreateReady(NULL, block, decoder->format, 1, 1, &timing, 1, &sampleSize,
                                       &sample);
  p_CFRelease(block);
  if (status != noErr) return (int64_t)status;
  decoder->status = noErr;
  VTDecodeInfoFlags info = 0;
  status = p_VTDecompressionSessionDecodeFrame(decoder->session, sample, 0, NULL, &info);
  p_CFRelease(sample);
  if (status != noErr) return (int64_t)status;
  if (decoder->status != noErr) return (int64_t)decoder->status;
  CVImageBufferRef frame = decoder->frame;
  decoder->frame = NULL;
  return (int64_t)(intptr_t)frame;
}

void rae_video_vt_close(int64_t decoder_handle) {
  RaeVideoDecoder* decoder = (RaeVideoDecoder*)(intptr_t)decoder_handle;
  if (!decoder) return;
  if (decoder->session) {
    p_VTDecompressionSessionInvalidate(decoder->session);
    p_CFRelease(decoder->session);
  }
  if (decoder->frame) p_CFRelease(decoder->frame);
  if (decoder->format) p_CFRelease(decoder->format);
  free(decoder);
}

/* ---- frames ----------------------------------------------------------------- */
#define RAE_VIDEO_FRAME(handle) ((CVPixelBufferRef)(intptr_t)(handle))

int64_t rae_video_frame_width(int64_t frame) {
  return frame ? (int64_t)p_CVPixelBufferGetWidth(RAE_VIDEO_FRAME(frame)) : 0;
}

int64_t rae_video_frame_height(int64_t frame) {
  return frame ? (int64_t)p_CVPixelBufferGetHeight(RAE_VIDEO_FRAME(frame)) : 0;
}

int64_t rae_video_frame_pixel_format(int64_t frame) {
  return frame ? (int64_t)p_CVPixelBufferGetPixelFormatType(RAE_VIDEO_FRAME(frame)) : 0;
}

int64_t rae_video_frame_plane_count(int64_t frame) {
  return frame ? (int64_t)p_CVPixelBufferGetPlaneCount(RAE_VIDEO_FRAME(frame)) : 0;
}

int64_t rae_video_frame_plane_width(int64_t frame, int64_t plane) {
  return frame ? (int64_t)p_CVPixelBufferGetWidthOfPlane(RAE_VIDEO_FRAME(frame), (size_t)plane) : 0;
}

int64_t rae_video_frame_plane_height(int64_t frame, int64_t plane) {
  return frame ? (int64_t)p_CVPixelBufferGetHeightOfPlane(RAE_VIDEO_FRAME(frame), (size_t)plane) : 0;
}

int64_t rae_video_frame_plane_bytes_per_row(int64_t frame, int64_t plane) {
  return frame ? (int64_t)p_CVPixelBufferGetBytesPerRowOfPlane(RAE_VIDEO_FRAME(frame), (size_t)plane)
               : 0;
}

int64_t rae_video_frame_lock(int64_t frame) {
  if (!frame) return -1;
  return (int64_t)p_CVPixelBufferLockBaseAddress(RAE_VIDEO_FRAME(frame), kCVPixelBufferLock_ReadOnly);
}

uint8_t* rae_video_frame_plane_address(int64_t frame, int64_t plane) {
  if (!frame) return NULL;
  return (uint8_t*)p_CVPixelBufferGetBaseAddressOfPlane(RAE_VIDEO_FRAME(frame), (size_t)plane);
}

void rae_video_frame_unlock(int64_t frame) {
  if (frame) p_CVPixelBufferUnlockBaseAddress(RAE_VIDEO_FRAME(frame), kCVPixelBufferLock_ReadOnly);
}

int64_t rae_video_frame_copy_plane(int64_t frame, int64_t plane, uint8_t* into, int64_t row_bytes) {
  if (!frame || !into || row_bytes <= 0) return -1;
  CVPixelBufferRef buffer = RAE_VIDEO_FRAME(frame);
  if (p_CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return -1;
  const uint8_t* base = (const uint8_t*)p_CVPixelBufferGetBaseAddressOfPlane(buffer, (size_t)plane);
  size_t stride = p_CVPixelBufferGetBytesPerRowOfPlane(buffer, (size_t)plane);
  size_t rows = p_CVPixelBufferGetHeightOfPlane(buffer, (size_t)plane);
  int64_t copied = -1;
  if (base && (size_t)row_bytes <= stride) {
    for (size_t row = 0; row < rows; row++) {
      memcpy(into + row * (size_t)row_bytes, base + row * stride, (size_t)row_bytes);
    }
    copied = (int64_t)(rows * (size_t)row_bytes);
  }
  p_CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
  return copied;
}

/* The code of the first constant the attachment equals, or 0 */
static int64_t rae_video_tag(int64_t frame, const CFStringRef* key, const CFStringRef* const* values,
                             const int64_t* codes, int count) {
  if (!frame || !key) return 0;
  CFTypeRef value = p_CVBufferCopyAttachment(RAE_VIDEO_FRAME(frame), *key, NULL);
  if (!value) return 0;
  int64_t code = 0;
  for (int i = 0; i < count && code == 0; i++) {
    if (values[i] && p_CFEqual(value, *values[i])) code = codes[i];
  }
  p_CFRelease(value);
  return code;
}

int64_t rae_video_frame_matrix(int64_t frame) {
  const CFStringRef* const values[] = {
      c_kCVImageBufferYCbCrMatrix_ITU_R_709_2, c_kCVImageBufferYCbCrMatrix_ITU_R_601_4,
      c_kCVImageBufferYCbCrMatrix_ITU_R_2020, c_kCVImageBufferYCbCrMatrix_SMPTE_240M_1995};
  const int64_t codes[] = {1, 2, 3, 4};
  return rae_video_tag(frame, c_kCVImageBufferYCbCrMatrixKey, values, codes, 4);
}

int64_t rae_video_frame_primaries(int64_t frame) {
  const CFStringRef* const values[] = {
      c_kCVImageBufferColorPrimaries_ITU_R_709_2, c_kCVImageBufferColorPrimaries_ITU_R_2020,
      c_kCVImageBufferColorPrimaries_P3_D65,      c_kCVImageBufferColorPrimaries_DCI_P3,
      c_kCVImageBufferColorPrimaries_EBU_3213,    c_kCVImageBufferColorPrimaries_SMPTE_C};
  const int64_t codes[] = {1, 2, 3, 3, 4, 4};
  return rae_video_tag(frame, c_kCVImageBufferColorPrimariesKey, values, codes, 6);
}

int64_t rae_video_frame_transfer(int64_t frame) {
  const CFStringRef* const values[] = {
      c_kCVImageBufferTransferFunction_ITU_R_709_2,
      c_kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ,
      c_kCVImageBufferTransferFunction_ITU_R_2100_HLG, c_kCVImageBufferTransferFunction_sRGB,
      c_kCVImageBufferTransferFunction_Linear};
  const int64_t codes[] = {1, 2, 3, 4, 5};
  return rae_video_tag(frame, c_kCVImageBufferTransferFunctionKey, values, codes, 5);
}

void rae_video_frame_release(int64_t frame) {
  if (frame) p_CFRelease((CFTypeRef)(intptr_t)frame);
}

#else
/* No decoder on this platform yet (docs/design.md §11 step 6) */
int64_t rae_video_vt_available(void) { return 0; }
int64_t rae_video_vt_open(int64_t codec, int64_t width, int64_t height, uint8_t* config,
                          int64_t config_size, int64_t config_kind) {
  (void)codec; (void)width; (void)height; (void)config; (void)config_size; (void)config_kind;
  return 0;
}
int64_t rae_video_vt_decode(int64_t decoder, uint8_t* packet, int64_t size, int64_t time,
                            int64_t timescale) {
  (void)decoder; (void)packet; (void)size; (void)time; (void)timescale;
  return -1;
}
void rae_video_vt_close(int64_t decoder) { (void)decoder; }
void rae_video_vt_set_color_tags(int64_t primaries, int64_t transfer, int64_t matrix,
                                 int64_t full_range) {
  (void)primaries; (void)transfer; (void)matrix; (void)full_range;
}
int64_t rae_video_frame_width(int64_t frame) { (void)frame; return 0; }
int64_t rae_video_frame_height(int64_t frame) { (void)frame; return 0; }
int64_t rae_video_frame_pixel_format(int64_t frame) { (void)frame; return 0; }
int64_t rae_video_frame_plane_count(int64_t frame) { (void)frame; return 0; }
int64_t rae_video_frame_plane_width(int64_t frame, int64_t plane) { (void)frame; (void)plane; return 0; }
int64_t rae_video_frame_plane_height(int64_t frame, int64_t plane) { (void)frame; (void)plane; return 0; }
int64_t rae_video_frame_plane_bytes_per_row(int64_t frame, int64_t plane) {
  (void)frame; (void)plane;
  return 0;
}
int64_t rae_video_frame_lock(int64_t frame) { (void)frame; return -1; }
uint8_t* rae_video_frame_plane_address(int64_t frame, int64_t plane) {
  (void)frame; (void)plane;
  return 0;
}
void rae_video_frame_unlock(int64_t frame) { (void)frame; }
int64_t rae_video_frame_copy_plane(int64_t frame, int64_t plane, uint8_t* into, int64_t row_bytes) {
  (void)frame; (void)plane; (void)into; (void)row_bytes;
  return -1;
}
int64_t rae_video_frame_matrix(int64_t frame) { (void)frame; return 0; }
int64_t rae_video_frame_primaries(int64_t frame) { (void)frame; return 0; }
int64_t rae_video_frame_transfer(int64_t frame) { (void)frame; return 0; }
void rae_video_frame_release(int64_t frame) { (void)frame; }
#endif
