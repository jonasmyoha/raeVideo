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

#endif
