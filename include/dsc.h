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

/* Switches for questions where the specification text supports two readings.
 * Each is listed, with both readings and section numbers, under "Open
 * questions" in RESEARCH.md. dsc_options_init() selects the defaults. */
enum dsc_flat_restart {        /* OQ-1, DSC 1.1 §6.8.5.2 and Figure 6-8 */
 DSC_FLAT_RESTART_NEXT_CYCLE = 0, /* override seeds the RC cycle after the flagged group */
 DSC_FLAT_RESTART_IN_FLIGHT = 1   /* override also re-runs the cycle whose QP is queued */
};
enum dsc_threshold_eq {        /* OQ-2, DSC 1.1 §6.8.3 and Figure 6-11 */
 DSC_THRESHOLD_EQ_LOWER = 0,      /* fullness equal to a threshold stays in the lower range */
 DSC_THRESHOLD_EQ_UPPER = 1       /* ... moves to the upper range */
};
enum dsc_frac_reset {          /* OQ-3, DSC 1.1 §6.8.1 */
 DSC_FRAC_RESET_CHUNK = 0,        /* fractional bits reset when a chunk completes */
 DSC_FRAC_RESET_LITERAL = 1       /* reset when (pixelCount-initial_xmit_delay)%slice_width==0 */
};
enum dsc_delay_offset {        /* OQ-12, DSC 1.1 §6.8.1 and §6.8.2 */
 DSC_DELAY_OFFSET_INCLUSIVE = 0,  /* initial-delay offset decrement covers initial_xmit_delay pixels */
 DSC_DELAY_OFFSET_EXCLUSIVE = 1   /* ... covers only the pixels that remove no bits (one fewer) */
};
enum dsc_bp_left {             /* OQ-4, DSC 1.1 §6.4.4.1 */
 DSC_BP_LEFT_REPLICATE = 0,       /* previous-line samples left of the slice repeat its first sample */
 DSC_BP_LEFT_MIDPOINT = 1         /* ... are the component midpoint */
};
enum dsc_bp_edge {             /* OQ-13, DSC 1.1 §6.4.4.1 */
 DSC_BP_EDGE_WINDOW = 0,          /* lastEdgeCount as of the search window's last sample, hPos+2 */
 DSC_BP_EDGE_BEFORE = 1           /* ... as of the sample left of the group, hPos-1 */
};
enum dsc_bp_sad {              /* OQ-10, DSC 1.1 §6.4.4.1, DSC 1.2b §6.4.4.1 */
 DSC_BP_SAD_SHIFT = 0,            /* sum of 3x1 SADs (each clamped to 511), three LSBs dropped */
 DSC_BP_SAD_CLIP = 1              /* DSC 1.1's printed formula: MIN(511, sum) */
};

/* Counts of events where the readings above can disagree. Accumulated. */
struct dsc_stats {
 unsigned long groups;
 unsigned long threshold_equal;   /* rcModelFullness exactly on a range threshold */
 unsigned long flat_overrides;    /* flatness overrides that changed masterQp */
 unsigned long flat_queue_differs;/* ... where the two OQ-1 readings queue different QPs */
 unsigned long frac_differs;      /* groups where the OQ-3 readings disagree on fullness */
 unsigned long bp_groups;         /* groups predicted by BP */
 unsigned long bp_left_differs;   /* groups whose BP decision depends on the OQ-4 reading */
};

/* One record per decoded group, after its rate-control step. */
struct dsc_group_trace {
 unsigned slice, group, x, y, qp, actual, ideal, range, generated_qp;
 long long buffer_fullness, model_fullness;
 int ich, flat_override;
};
typedef void (*dsc_trace_fn)(void *context, const struct dsc_group_trace *);

struct dsc_options {
 int flat_restart, threshold_eq, frac_reset, delay_offset;
 int bp_left, bp_edge, bp_sad;
 struct dsc_stats *stats;         /* optional */
 dsc_trace_fn trace;              /* optional */
 void *trace_context;
};
void dsc_options_init(struct dsc_options *);

/* Parsing is independent of decode support; reserved bits must be zero.
 * rc_buf_thresh retains PPS units of 64 bits; offsets retain six-bit encoding.
 * Non-wire slice_count and rc_bits are derived when representable.
 */
int dsc_parse_pps(const uint8_t *pps, size_t size, struct drm_dsc_config *out);
/* Output is RGB888, tightly packed. All functions are reentrant. On failure
 * output is unspecified (and may be partially written). No pointer is retained.
 * One slice requires exactly chunk_size*slice_height bytes in CBR mode;
 * VBR is rejected as unsupported.
 */
int dsc_decode_slice(const struct drm_dsc_config *cfg,
 const uint8_t *data, size_t size, uint8_t *rgb, size_t capacity);
/* CBR picture payload: slice rows, then scanlines, then horizontal chunks.
 * Edge slices are fully decoded and cropped to pic_width/pic_height.
 * VBR is currently unsupported by both decode APIs.
 */
int dsc_decode_frame(const struct drm_dsc_config *cfg,
 const uint8_t *data, size_t size, uint8_t *rgb, size_t capacity);
/* As above with explicit options; NULL selects dsc_options_init() defaults. */
int dsc_decode_slice_ex(const struct drm_dsc_config *cfg, const struct dsc_options *opt,
 const uint8_t *data, size_t size, uint8_t *rgb, size_t capacity);
int dsc_decode_frame_ex(const struct drm_dsc_config *cfg, const struct dsc_options *opt,
 const uint8_t *data, size_t size, uint8_t *rgb, size_t capacity);
const char *dsc_strerror(int status);
#endif
