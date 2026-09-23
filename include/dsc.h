/* SPDX-License-Identifier: MIT */
#ifndef DSC_DECODE_H
#define DSC_DECODE_H
#include <stddef.h>
#include <stdint.h>
#include <drm/display/drm_dsc.h>
#define DSC_PPS_BYTES 128u
#define DSC_MAX_PIXELS (16u * 1024u * 1024u)
enum dsc_status {
 DSC_OK = 0, DSC_INVALID, DSC_UNSUPPORTED, DSC_TRUNCATED,
 DSC_LIMIT, DSC_NOMEM, DSC_BITSTREAM, DSC_RATE_CONTROL
};
/* Parsing is independent of decode support; reserved bits must be zero.
 * rc_buf_thresh retains PPS units of 64 bits; offsets retain six-bit encoding.
 * Non-wire slice_count and rc_bits are derived when representable.
 */
int dsc_parse_pps(const uint8_t *pps, size_t size, struct drm_dsc_config *out);
/* Output is RGB888, tightly packed. All functions are reentrant. On failure
 * output is unspecified (and may be partially written). No pointer is retained.
 * One slice requires exactly chunk_size*slice_height bytes in CBR mode;
 * VBR and block prediction are currently rejected as unsupported.
 */
int dsc_decode_slice(const struct drm_dsc_config *cfg,
 const uint8_t *data, size_t size, uint8_t *rgb, size_t capacity);
/* CBR picture payload: slice rows, then scanlines, then horizontal chunks.
 * Edge slices are fully decoded and cropped to pic_width/pic_height.
 * VBR is currently unsupported by both decode APIs.
 */
int dsc_decode_frame(const struct drm_dsc_config *cfg,
 const uint8_t *data, size_t size, uint8_t *rgb, size_t capacity);
const char *dsc_strerror(int status);
#endif
