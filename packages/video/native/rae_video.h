/* The C glue of the video package (docs/design.md §5 V1): one platform call
 * per function, handles as integers (0: none), no policy. Which backend
 * decodes which codec, the packet order, the presentation order and every
 * decision are Rae (video/VideoBackends, video/VideoDecoder, video/MediaReader).
 *
 * Apple: VideoToolbox, CoreMedia, CoreVideo and CoreFoundation are loaded at
 * run time (dlopen), like the audio package loads AudioToolbox, so a program
 * links no framework for them. Elsewhere every decoder call answers "none"
 * until that platform's backend lands (docs/design.md §11 step 6). */
#ifndef RAE_VIDEO_H
#define RAE_VIDEO_H

#include <stdint.h>

/* ---- files: a descriptor, a read at an offset ---------------------------- */
/* The file's descriptor, or a negative errno */
int64_t rae_video_file_open(uint8_t* path_z);
int64_t rae_video_file_size(int64_t file);
/* Bytes read (count unless at the end), or a negative errno */
int64_t rae_video_file_read_at(int64_t file, uint8_t* into, int64_t count, int64_t offset);
void rae_video_file_close(int64_t file);

/* ---- VideoToolbox decoding (Apple) ----------------------------------------
 * A decoder for `codec` (the sample entry's four characters as a big-endian
 * integer: 'avc1', 'hvc1', 'apco', ...) at width x height, with the sample
 * entry's configuration box payload (`config_size` bytes of avcC / hvcC,
 * under the four characters `config_kind`; 0 bytes for ProRes). It hands out
 * 8-bit 4:2:0 frames in two planes (NV12), in the stream's own range when it
 * can ('420v' limited, '420f' full). A handle, or 0 when VideoToolbox is not
 * there or refuses the stream. */
int64_t rae_video_vt_available(void);
int64_t rae_video_vt_open(int64_t codec, int64_t width, int64_t height, uint8_t* config,
                          int64_t config_size, int64_t config_kind);
/* The frames the next decoder opened on this thread hands out: 0 8-bit 4:2:0
 * (the default, as above), 1 10-bit 4:2:0 ('x420' / 'xf20'), 2 10-bit 4:2:2
 * ('x422' / 'xf22'): two planes of 16-bit samples, the code in the top 10
 * bits. Which one a stream gets is Rae's (video/VideoDecoder). */
void rae_video_vt_set_output(int64_t output);
/* The container's color tags (ITU-T H.273 code points: primaries, transfer,
 * matrix; full_range 0 or 1) for the next decoder opened on this thread, so
 * its frames carry them; -1 for a tag the container does not give. Without
 * them VideoToolbox guesses (BT.601 for a picture of SD size). */
void rae_video_vt_set_color_tags(int64_t primaries, int64_t transfer, int64_t matrix,
                                 int64_t full_range);
/* Decodes one packet (synchronously; frames come out in decode order) shown
 * at `time` / `timescale`. The frame (a handle the caller releases), 0 when
 * the packet gave none, or a negative OSStatus. */
int64_t rae_video_vt_decode(int64_t decoder, uint8_t* packet, int64_t size, int64_t time,
                            int64_t timescale);
void rae_video_vt_close(int64_t decoder);

/* ---- a decoded frame (a CVPixelBuffer) ------------------------------------ */
int64_t rae_video_frame_width(int64_t frame);
int64_t rae_video_frame_height(int64_t frame);
/* The CoreVideo pixel format's four characters as an integer ('420v', ...) */
int64_t rae_video_frame_pixel_format(int64_t frame);
int64_t rae_video_frame_plane_count(int64_t frame);
int64_t rae_video_frame_plane_width(int64_t frame, int64_t plane);
int64_t rae_video_frame_plane_height(int64_t frame, int64_t plane);
int64_t rae_video_frame_plane_bytes_per_row(int64_t frame, int64_t plane);
/* Locks the planes for reading (0 on success); the addresses are valid until
 * the unlock */
int64_t rae_video_frame_lock(int64_t frame);
uint8_t* rae_video_frame_plane_address(int64_t frame, int64_t plane);
void rae_video_frame_unlock(int64_t frame);
/* Copies plane `plane`, `row_bytes` bytes of each row, rows packed, into
 * `into` (row_bytes x the plane's height). The bytes copied, or -1. */
int64_t rae_video_frame_copy_plane(int64_t frame, int64_t plane, uint8_t* into, int64_t row_bytes);
/* The frame's color tags, as small codes (0: untagged):
 *   matrix:    1 BT.709, 2 BT.601, 3 BT.2020, 4 SMPTE 240M
 *   primaries: 1 BT.709, 2 BT.2020, 3 Display P3 / DCI-P3, 4 EBU 3213 / SMPTE-C (BT.601)
 *   transfer:  1 BT.709, 2 PQ (ST 2084), 3 HLG, 4 sRGB, 5 linear */
int64_t rae_video_frame_matrix(int64_t frame);
int64_t rae_video_frame_primaries(int64_t frame);
int64_t rae_video_frame_transfer(int64_t frame);
void rae_video_frame_release(int64_t frame);

/* ---- VideoToolbox encoding (native/rae_video_encoder.c) ------------------
 * An encoder for `codec` ('avc1', 'hvc1', 'apco', 'apcs', 'apcn', 'apch',
 * 'ap4h') at width x height, times in 1/timescale; bit_rate (0: the codec's
 * own), quality 1-100 (0: not set), keyframe_interval (0: the codec's own).
 * No frame reordering (no B-frames). Frames go in as 32-bit BGRA rows; the
 * session's color tags are BT.709. A handle, or 0. */
int64_t rae_video_vte_open(int64_t codec, int64_t width, int64_t height, int64_t timescale,
                           int64_t bit_rate, int64_t quality_percent, int64_t keyframe_interval);
/* One frame, shown at `time` (0, or a negative error) */
int64_t rae_video_vte_encode(int64_t encoder, uint8_t* bgra, int64_t bytes_per_row, int64_t time);
/* Waits until every frame given has come out */
int64_t rae_video_vte_finish(int64_t encoder);
/* The encoded packets waiting: how many; the oldest one's size, time, decode
 * time and keyframe flag into out[0..3]; its bytes copied out and dropped */
int64_t rae_video_vte_packet_count(int64_t encoder);
int64_t rae_video_vte_packet_info(int64_t encoder, int64_t* out);
int64_t rae_video_vte_take_packet(int64_t encoder, uint8_t* into, int64_t capacity);
int64_t rae_video_vte_failures(int64_t encoder);
/* The codec configuration (avcC / hvcC payload): its size, its kind in out[0] */
int64_t rae_video_vte_config_size(int64_t encoder, int64_t* out);
int64_t rae_video_vte_copy_config(int64_t encoder, uint8_t* into, int64_t capacity);
void rae_video_vte_close(int64_t encoder);

/* ---- registered encoders (another package's: packages/ffmpeg) -------------
 * Like the registered decoders below: a package hands over its encoder's
 * calls as a pointer to this struct (video/VideoBackends.registerEncoder).
 * `open` takes the encoder's name and a profile ("dnxhd" with "dnxhr_lb",
 * "ffv1" with ""); a frame goes in as 8-bit planar Y'CbCr, the planes packed
 * one after another (Y width x height, then Cb, then Cr, each row as wide as
 * the plane), laid out as `input_layout` says (2 4:2:0, 3 4:2:2, the frame
 * layout codes of rae_video.h). Packets and the codec configuration come out
 * as from rae_video_vte_*. */
#define RAE_VIDEO_ENCODER_CALLS_VERSION 1
typedef struct RaeVideoEncoderCalls {
  int64_t version;
  int64_t (*open)(uint8_t* encoder_z, uint8_t* profile_z, int64_t width, int64_t height,
                  int64_t timescale, int64_t bit_rate, int64_t keyframe_interval);
  int64_t (*input_layout)(int64_t encoder);
  int64_t (*encode)(int64_t encoder, uint8_t* planes, int64_t time);
  int64_t (*finish)(int64_t encoder);
  int64_t (*packet_count)(int64_t encoder);
  int64_t (*packet_info)(int64_t encoder, int64_t* out);
  int64_t (*take_packet)(int64_t encoder, uint8_t* into, int64_t capacity);
  int64_t (*config_size)(int64_t encoder, int64_t* out);
  int64_t (*copy_config)(int64_t encoder, uint8_t* into, int64_t capacity);
  void (*close)(int64_t encoder);
} RaeVideoEncoderCalls;

int64_t rae_video_encoder_register(void* calls);
int64_t rae_video_registered_encoder_open(int64_t backend, uint8_t* encoder_z, uint8_t* profile_z,
                                          int64_t width, int64_t height, int64_t timescale,
                                          int64_t bit_rate, int64_t keyframe_interval);
int64_t rae_video_registered_encoder_input_layout(int64_t backend, int64_t encoder);
int64_t rae_video_registered_encoder_encode(int64_t backend, int64_t encoder, uint8_t* planes,
                                            int64_t time);
int64_t rae_video_registered_encoder_finish(int64_t backend, int64_t encoder);
int64_t rae_video_registered_encoder_packet_count(int64_t backend, int64_t encoder);
int64_t rae_video_registered_encoder_packet_info(int64_t backend, int64_t encoder, int64_t* out);
int64_t rae_video_registered_encoder_take_packet(int64_t backend, int64_t encoder, uint8_t* into,
                                                 int64_t capacity);
int64_t rae_video_registered_encoder_config_size(int64_t backend, int64_t encoder, int64_t* out);
int64_t rae_video_registered_encoder_copy_config(int64_t backend, int64_t encoder, uint8_t* into,
                                                 int64_t capacity);
void rae_video_registered_encoder_close(int64_t backend, int64_t encoder);

/* ---- files for writing ------------------------------------------------------ */
/* Creates (or empties) a file for writing: its descriptor, or a negative errno */
int64_t rae_video_file_create(uint8_t* path_z);
/* Writes `count` bytes at `offset`: the count, or a negative errno */
int64_t rae_video_file_write_at(int64_t file, uint8_t* bytes, int64_t count, int64_t offset);

/* ---- registered backends (another package's decoder) ----------------------
 * A package with its own decoder (packages/ffmpeg) hands its calls over as a
 * pointer to this struct, from Rae (video/VideoBackends.registerBackend), so
 * neither package's C names the other's symbols. `version` must be
 * RAE_VIDEO_BACKEND_CALLS_VERSION. A frame of such a backend is 8-bit
 * Y'CbCr in the layout `frame_layout` says:
 *   1 two planes, CbCr interleaved, 4:2:0 (NV12)
 *   2 three planes, 4:2:0     3 three planes, 4:2:2     4 three planes, 4:4:4
 *   0 a layout the video package cannot show yet (10-bit, RGB, ...)
 * and `frame_tags` gives its color tags as ITU-T H.273 code points: matrix in
 * bits 0-7, primaries 8-15, transfer 16-23, and bit 24 for full range
 * (2, unspecified, when the stream does not say). */
#define RAE_VIDEO_BACKEND_CALLS_VERSION 1
typedef struct RaeVideoBackendCalls {
  int64_t version;
  /* a decoder for the codec the backend knows as `codec_z` ("dnxhd", "vp9") */
  int64_t (*open)(uint8_t* codec_z, int64_t width, int64_t height, uint8_t* config,
                  int64_t config_size);
  /* like rae_video_vt_decode: a frame, 0 for none yet, or a negative error */
  int64_t (*decode)(int64_t decoder, uint8_t* packet, int64_t size, int64_t time,
                    int64_t timescale);
  void (*close)(int64_t decoder);
  int64_t (*frame_layout)(int64_t frame);
  int64_t (*frame_width)(int64_t frame);
  int64_t (*frame_height)(int64_t frame);
  int64_t (*frame_plane_bytes_per_row)(int64_t frame, int64_t plane);
  uint8_t* (*frame_plane_address)(int64_t frame, int64_t plane);
  int64_t (*frame_tags)(int64_t frame);
  void (*frame_release)(int64_t frame);
} RaeVideoBackendCalls;

/* Registers a backend's calls; its number (1 and up), or 0 when the struct is
 * of another version or the table is full */
int64_t rae_video_backend_register(void* calls);
int64_t rae_video_backend_open(int64_t backend, uint8_t* codec_z, int64_t width, int64_t height,
                               uint8_t* config, int64_t config_size);
int64_t rae_video_backend_decode(int64_t backend, int64_t decoder, uint8_t* packet, int64_t size,
                                 int64_t time, int64_t timescale);
void rae_video_backend_close(int64_t backend, int64_t decoder);
int64_t rae_video_backend_frame_layout(int64_t backend, int64_t frame);
int64_t rae_video_backend_frame_width(int64_t backend, int64_t frame);
int64_t rae_video_backend_frame_height(int64_t backend, int64_t frame);
int64_t rae_video_backend_frame_plane_bytes_per_row(int64_t backend, int64_t frame,
                                                    int64_t plane);
uint8_t* rae_video_backend_frame_plane_address(int64_t backend, int64_t frame, int64_t plane);
int64_t rae_video_backend_frame_tags(int64_t backend, int64_t frame);
void rae_video_backend_frame_release(int64_t backend, int64_t frame);
/* Copies `rows` rows of `row_bytes` bytes of a plane, packed, into `into` */
int64_t rae_video_backend_frame_copy_plane(int64_t backend, int64_t frame, int64_t plane,
                                           uint8_t* into, int64_t row_bytes, int64_t rows);

#endif
