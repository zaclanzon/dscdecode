/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef DSC_DECODE_H
#define DSC_DECODE_H

#include <stddef.h>
#include <stdint.h>
#include <drm/display/drm_dsc.h>

#define DSC_PPS_BYTES 128u
#define DSC_MAX_PIXELS (16u * 1024u * 1024u)

enum dsc_status {
    DSC_OK = 0,
    DSC_INVALID,
    DSC_UNSUPPORTED,
    DSC_TRUNCATED,
    DSC_LIMIT,
    DSC_NOMEM,
    DSC_BITSTREAM,
    DSC_RATE_CONTROL
};

/* Switches for questions where the specification text supports two readings.
 * Each is listed, with both readings and section numbers, under "Open
 * questions" in RESEARCH.md. dsc_options_init() selects the defaults. */
enum dsc_flat_restart {              /* OQ-1, DSC 1.1 §6.8.5.2 and Figure 6-8 */
    DSC_FLAT_RESTART_NEXT_CYCLE = 0, /* override seeds the RC cycle after the flagged group */
    DSC_FLAT_RESTART_IN_FLIGHT = 1   /* override also re-runs the cycle whose QP is queued */
};

enum dsc_threshold_eq {         /* OQ-2, DSC 1.1 §6.8.3 and Figure 6-11 */
    DSC_THRESHOLD_EQ_LOWER = 0, /* fullness equal to a threshold stays in the lower range */
    DSC_THRESHOLD_EQ_UPPER = 1  /* ... moves to the upper range */
};

enum dsc_frac_reset {          /* OQ-3, DSC 1.1 §6.8.1 */
    DSC_FRAC_RESET_CHUNK = 0,  /* fractional bits reset when a chunk completes */
    DSC_FRAC_RESET_LITERAL = 1 /* reset when (pixelCount-initial_xmit_delay)%slice_width==0 */
};

enum dsc_delay_offset {             /* OQ-12, DSC 1.1 §6.8.1 and §6.8.2 */
    DSC_DELAY_OFFSET_INCLUSIVE = 0, /* initial-delay offset decrement covers initial_xmit_delay pixels */
    DSC_DELAY_OFFSET_EXCLUSIVE = 1  /* ... covers only the pixels that remove no bits (one fewer) */
};

enum dsc_delay_partial {             /* OQ-19, DSC 1.1 §6.8.2 and §6.8.1 */
    DSC_DELAY_PARTIAL_PIXELS = 0,    /* initial-delay offset decrement counts the pixels in each group */
    DSC_DELAY_PARTIAL_GROUP_END = 1  /* ... counts to each group's end as if it had three pixels */
};

/* DSC 1.2 questions. Section numbers are DSC 1.2b. */
enum dsc_bpg_combine {             /* OQ-7, §6.8.4 rcXformBpgOffset */
    DSC_BPG_COMBINE_REPLACE = 0,   /* second-line terms replace the first-line terms ("=", DSC 1.2a) */
    DSC_BPG_COMBINE_ADD = 1        /* ... are added to them ("+=", DSC 1.2b) */
};

enum dsc_chroma_qlevel {                /* OQ-20, §6.8.6, 16 bpc RGB */
    DSC_CHROMA_QLEVEL_TABLE = 0,        /* convert_rgb = 1: Table 6-3 as printed */
    DSC_CHROMA_QLEVEL_EQUAL_DEPTH = 1   /* equal luma and chroma depths: qLevelC - 1 */
};

enum dsc_prefix16 {           /* OQ-21, Table 4-10 and §3.10.2, 16 bpc at QP 0 */
    DSC_PREFIX16_15 = 0,      /* luma prefix of at most 15 bits (DSC 1.2b Table 4-10) */
    DSC_PREFIX16_13 = 1       /* ... 13 bits (DSC 1.2b §3.10.2, DSC 1.2a Table 4-10) */
};

enum dsc_bitsave_ich {         /* OQ-22, §6.8.4 bitSaveMode */
    DSC_BITSAVE_ICH_NOT = 0,   /* MPP and activity branches need !ichSelected (DSC 1.2b) */
    DSC_BITSAVE_ICH_SET = 1    /* ... need ichSelected, as DSC 1.2a prints */
};

enum dsc_bitsave_pred {             /* OQ-23, §6.8.4 predActivity, Tables 6-2 and 7-1 */
    DSC_BITSAVE_PRED_RAW = 0,       /* predictedSize the group was coded with, before qLevel adjustment */
    DSC_BITSAVE_PRED_ADJUSTED = 1,  /* ... adjPredictedSize */
    DSC_BITSAVE_PRED_NEXT = 2       /* predictedSize computed from the group's own residuals */
};

enum dsc_bitsave_flat {              /* OQ-24, §6.8.4: which flatness the bitSaveMode test sees */
    DSC_BITSAVE_FLAT_SUPERGROUP = 0, /* the flag of the supergroup that holds the group */
    DSC_BITSAVE_FLAT_GROUP = 1,      /* the group is the supergroup's signaled flat group */
    DSC_BITSAVE_FLAT_RECEIVED = 2,   /* the flag received last, from the group that carries it on */
    DSC_BITSAVE_FLAT_CARRIER = 3,    /* the group carries a flag of 1, or the type and position after it */
    DSC_BITSAVE_FLAT_SPAN = 4,       /* from the group carrying the flag to its supergroup's last group */
    DSC_BITSAVE_FLAT_LAGGED = 5      /* the flag received last before this group */
};

enum dsc_line_flat {              /* OQ-25, §6.8.5.2 first group of a non-first line */
    DSC_LINE_FLAT_VERY = 0,       /* always the very-flat adjustment */
    DSC_LINE_FLAT_SIGNALED = 1    /* as a signaled very-flat group, demoted below somewhatFlatQpThresh (OQ-16) */
};

/* DSC 1.2 short-term RC (§6.8.4, Figures 6-17 and 6-18) where the reference
 * model's output departs from the printed text; the first value of each is
 * the text (RESEARCH.md, OQ-26 to OQ-34). */
enum dsc_low_min {              /* OQ-26, lowMinQp of the zero-residual branch */
    DSC_LOW_MIN_MAX_QP = 0,     /* MAX(maxQp - 4, 0), as printed */
    DSC_LOW_MIN_MIN_QP = 1      /* MAX(minQp - 4, 0) */
};

enum dsc_decrement_test {       /* OQ-27, the decrement branch */
    DSC_DECREMENT_BOTH = 0,     /* codedGroupSize and rcSizeGroup below tgtMinusOffset, as printed */
    DSC_DECREMENT_SIZE = 1      /* rcSizeGroup below tgtMinusOffset */
};

enum dsc_activity_qp {          /* OQ-28, the QP in predActivity */
    DSC_ACTIVITY_PREV = 0,      /* prevQp, as printed */
    DSC_ACTIVITY_PREV2 = 1      /* prev2Qp: the QP of the group whose sizes are used */
};

enum dsc_bitsave_step {         /* OQ-29, stQp in bitSaveMode 2 */
    DSC_BITSAVE_STEP_1 = 0,     /* prevQp + 1, as printed */
    DSC_BITSAVE_STEP_2 = 1      /* prevQp + 2 */
};

enum dsc_target_floor {         /* OQ-30, a negative rcTgtBitsGroup */
    DSC_TARGET_FLOOR_NONE = 0,  /* used as computed */
    DSC_TARGET_FLOOR_ZERO = 1   /* raised to 0 */
};

enum dsc_flat_rerun {           /* OQ-31, re-running the step after a flatness adjustment */
    DSC_FLAT_RERUN_CHANGED = 0, /* only when the adjustment changes the QP (§6.8.5.2, as in DSC 1.1) */
    DSC_FLAT_RERUN_EVERY = 1    /* whenever a flat group or line start is adjusted, changed or not */
};

enum dsc_rerun_bitsave {        /* OQ-32, bitSaveMode in that re-run */
    DSC_RERUN_BITSAVE_KEEP = 0, /* as the step computed it */
    DSC_RERUN_BITSAVE_REDO = 1  /* computed again with the re-run's prevQp and prev2Qp */
};

enum dsc_mux16 {                /* OQ-33, luma refill threshold at 16 bpc (§4.4) */
    DSC_MUX16_ELEMENT = 0,      /* 4 * bpc + 4 = 68, the longest luma unit */
    DSC_MUX16_WORD = 1          /* 64, the mux word */
};

enum dsc_prefix16_scope {       /* OQ-35, which 16 bpc groups have a cut luma prefix (Table 4-10) */
    DSC_PREFIX16_SCOPE_QP0 = 0, /* primaryQp 0, as printed */
    DSC_PREFIX16_SCOPE_QLEVEL = 1 /* luma qLevel 0 (cut as OQ-21) and qLevel 1 (cut at 15) */
};

enum dsc_prefix16_cut {         /* OQ-36, when Table 4-10's cut and its rules apply */
    DSC_PREFIX16_CUT_ALWAYS = 0, /* in every such group: at most N bits, no ICH, no adjustment after ICH */
    DSC_PREFIX16_CUT_LONGER = 1  /* only where the uncut prefix could be longer than the cut */
};

enum dsc_flat_top {             /* OQ-34, §6.8.5.2 no adjustment at range 14's maximum QP */
    DSC_FLAT_TOP_EQUAL = 0,     /* only at that QP, as printed */
    DSC_FLAT_TOP_AT_OR_ABOVE = 1 /* also above it, which DSC 1.2's bitSaveMode can reach */
};

/* DSC 1.2 native 4:2:2 and 4:2:0 (DSC 1.2b §3.10.1, §6.4, §6.8.4). */
enum dsc_activity420 {            /* OQ-37, §6.8.4 predActivity in native 4:2:0, printed unbalanced */
    DSC_ACTIVITY420_LUMA = 0,     /* prevQp + MAX(predictedSize[0], predictedSize[1]) + predictedSize[2] */
    DSC_ACTIVITY420_SUM = 1       /* prevQp + MAX(predictedSize[0], predictedSize[1] + predictedSize[2]) */
};

enum dsc_activity422 {            /* OQ-38, §6.8.4 predActivity in native 4:2:2 */
    DSC_ACTIVITY422_SIZES = 0,    /* prevQp + (sum of the four predictedSizes >> 1) */
    DSC_ACTIVITY422_TOTAL = 1     /* (prevQp + sum of the four) >> 1, the C reading of the text */
};

enum dsc_bp420_edge {             /* OQ-39, §6.4.4.1 lastEdgeCount in native 4:2:0 */
    DSC_BP420_EDGE_LUMA = 0,      /* edges in the luma units only, as BP and its search (§3.10.1) */
    DSC_BP420_EDGE_ALL = 1        /* edges in any container component, chroma included */
};

enum dsc_offset_adj {             /* OQ-40, second_line_offset_adj (native 4:2:0) */
    DSC_OFFSET_ADJ_SUBTRACT = 0,  /* subtracted once the first line is done (§6.8.2) */
    DSC_OFFSET_ADJ_START = 1      /* also added at the slice start (Annex E, Table E-2) */
};

enum dsc_ich_window {            /* OQ-41, §6.5.1 previous-line ICH pairs at the slice edges (native) */
    DSC_ICH_WINDOW_PIXELS = 0,    /* the eight luma samples shifted to lie in the slice */
    DSC_ICH_WINDOW_CONTAINER = 1  /* the container pixels x - 1 to x + 3 shifted to lie in the slice */
};

enum dsc_bp_left {             /* OQ-4, DSC 1.1 §6.4.4.1 */
    DSC_BP_LEFT_REPLICATE = 0, /* previous-line samples left of the slice repeat its first sample */
    DSC_BP_LEFT_MIDPOINT = 1   /* ... are the component midpoint */
};

enum dsc_bp_edge {          /* OQ-13, DSC 1.1 §6.4.4.1 */
    DSC_BP_EDGE_WINDOW = 0, /* lastEdgeCount as of the search window's last sample, hPos+2 */
    DSC_BP_EDGE_BEFORE = 1  /* ... as of the sample left of the group, hPos-1 */
};

enum dsc_bp_sad {         /* OQ-10, DSC 1.1 §6.4.4.1, DSC 1.2b §6.4.4.1 */
    DSC_BP_SAD_SHIFT = 0, /* sum of 3x1 SADs (each clamped to 511), three LSBs dropped */
    DSC_BP_SAD_CLIP = 1   /* DSC 1.1's printed formula: MIN(511, sum) */
};

enum dsc_incr_order {           /* OQ-5, DSC 1.1 §6.8.4, Figure 6-13 */
    DSC_INCR_ORDER_PRINTED = 0, /* limit0 branch when curQp < prev2Qp, as printed */
    DSC_INCR_ORDER_SWAPPED = 1  /* limit0 branch when curQp > prev2Qp */
};

enum dsc_rc_pipeline {              /* OQ-11, DSC 1.1 Figure 6-8, §7.3 */
    DSC_RC_PIPELINE_SAME_GROUP = 0, /* range and short-term RC both use the group just decoded */
    DSC_RC_PIPELINE_RANGE_LAG = 1   /* range (minQp, maxQp, bpgOffset) is one group older */
};

enum dsc_scale_dec {                /* OQ-14, DSC 1.1 §6.8.2 */
    DSC_SCALE_DEC_FROM_GROUP_1 = 0, /* decrement interval counted from the second group */
    DSC_SCALE_DEC_FROM_GROUP_0 = 1  /* ... counted from the first group of the slice */
};

enum dsc_scale_first {             /* OQ-42, DSC 1.1 and DSC 1.2b §6.8.2 */
    DSC_SCALE_FIRST_GROUP = 0,     /* with scale_decrement_interval 1 the first group decrements */
    DSC_SCALE_FIRST_NOT = 1        /* the first group keeps initial_scale_value; the count is unchanged */
};

enum dsc_scale_line {              /* OQ-43, DSC 1.1 and DSC 1.2b §6.8.2, Annex E */
    DSC_SCALE_LINE_UNTIL_UNITY = 0, /* decrements continue until unity, past the first line if need be */
    DSC_SCALE_LINE_FIRST = 1        /* decrements only during the first line of the slice */
};

enum dsc_partial_target {         /* OQ-15, DSC 1.1 §6.8.1, §6.8.4 */
    DSC_PARTIAL_TARGET_THREE = 0, /* rcTgtBitsGroup uses 3 x bits_per_pixel for every group */
    DSC_PARTIAL_TARGET_PIXELS = 1 /* ... uses the pixels actually in the group */
};

enum dsc_very_flat {               /* OQ-16, DSC 1.1 §6.8.5.2, §6.6.3 */
    DSC_VERY_FLAT_GROUP_QP = 0,    /* very flat demoted when the flagged group's own QP is below 7 */
    DSC_VERY_FLAT_AS_SIGNALED = 1, /* the type decoded with the flag applies unchanged */
    DSC_VERY_FLAT_PREVIOUS_QP = 2  /* ... demoted when the group before the flagged one used QP below 7 */
};

enum dsc_flat_max_qp {           /* OQ-18, DSC 1.1 §6.8.5.2 */
    DSC_FLAT_MAX_QP_OWN = 0,     /* no override when the flagged group's own QP is range 14's maximum */
    DSC_FLAT_MAX_QP_PREVIOUS = 1 /* ... when the group before the flagged one used that maximum */
};

enum dsc_partial_padding {          /* OQ-17, DSC 1.1 §6.6, §7.8 */
    DSC_PARTIAL_PADDING_REJECT = 0, /* nonzero padding residuals or unreplicated indices are errors */
    DSC_PARTIAL_PADDING_ACCEPT = 1  /* padding is parsed (and feeds size prediction) but not checked */
};

/* Counts of events where the readings above can disagree. Accumulated. */
struct dsc_stats {
    unsigned long groups;
    unsigned long threshold_equal;     /* rcModelFullness exactly on a range threshold */
    unsigned long flat_overrides;      /* flatness overrides that changed masterQp */
    unsigned long flat_queue_differs;  /* ... where the two OQ-1 readings queue different QPs */
    unsigned long frac_differs;        /* groups where the OQ-3 readings disagree on fullness */
    unsigned long bp_groups;           /* groups predicted by BP */
    unsigned long bp_left_differs;     /* groups whose BP decision depends on the OQ-4 reading */
    unsigned long incr_order_differs;  /* increments the two OQ-5 readings permit differently */
    unsigned long range_lag_differs;   /* groups whose lagged and current ranges differ (OQ-11) */
    unsigned long partial_groups;      /* partial groups, where the OQ-15 targets differ */
    unsigned long very_flat_low_qp;    /* very-flat overrides where the OQ-16 readings can differ */
    unsigned long padding_nonzero;     /* partial groups with noncanonical padding (OQ-17) */
    unsigned long flat_max_qp_differs; /* flat signals the two OQ-18 readings treat differently */
    unsigned long delay_partial_differs; /* groups whose initial-delay offsets differ (OQ-19) */
    unsigned long bit_save_groups;       /* DSC 1.2 RC steps taken in bit-saving mode 1 or 2 */
    unsigned long line_flat;             /* DSC 1.2 first-group-of-line very-flat adjustments */
    unsigned long activity_differs;      /* native RC steps whose bitSaveMode the OQ-37/OQ-38 readings change */
    unsigned long bp420_edge_differs;    /* native 4:2:0 groups whose BP decision depends on OQ-39 */
    unsigned long ich_window_differs;    /* native previous-line ICH lookups the OQ-41 readings place differently */
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
    int incr_order, rc_pipeline, scale_dec, partial_target, very_flat, partial_padding, flat_max_qp;
    int delay_partial;
    int bpg_combine, chroma_qlevel, prefix16, bitsave_ich, bitsave_pred, bitsave_flat, line_flat;
    int low_min, decrement_test, activity_qp, bitsave_step, target_floor, flat_rerun, rerun_bitsave,
        mux16, flat_top, prefix16_scope, prefix16_cut;
    int activity420, activity422, bp420_edge, offset_adj, ich_window;
    int scale_first, scale_line;
    struct dsc_stats *stats; /* optional */
    dsc_trace_fn trace;      /* optional */
    void *trace_context;
};

void dsc_options_init(struct dsc_options *);

/* Parsing is independent of decode support; reserved bits must be zero.
 * rc_buf_thresh retains PPS units of 64 bits; offsets retain six-bit encoding.
 * Non-wire slice_count and rc_bits are derived when representable.
 */
int dsc_parse_pps(const uint8_t *pps, size_t size, struct drm_dsc_config *out);

/* Output is RGB888, tightly packed, for 8 bpc RGB pictures; other formats
 * return DSC_UNSUPPORTED here and use the planes functions below. All
 * functions are reentrant. On failure output is unspecified (and may be
 * partially written). No pointer is retained. One slice requires exactly
 * chunk_size*slice_height bytes in CBR mode; VBR is rejected as unsupported.
 */
int dsc_decode_slice(const struct drm_dsc_config *cfg, const uint8_t *data, size_t size,
                     uint8_t *rgb, size_t capacity);

/* CBR picture payload: slice rows, then scanlines, then horizontal chunks.
 * Edge slices are fully decoded and cropped to pic_width/pic_height.
 * VBR is currently unsupported by both decode APIs.
 */
int dsc_decode_frame(const struct drm_dsc_config *cfg, const uint8_t *data, size_t size,
                     uint8_t *rgb, size_t capacity);

/* As above with explicit options; NULL selects dsc_options_init() defaults. */
int dsc_decode_slice_ex(const struct drm_dsc_config *cfg, const struct dsc_options *opt,
                        const uint8_t *data, size_t size, uint8_t *rgb, size_t capacity);
int dsc_decode_frame_ex(const struct drm_dsc_config *cfg, const struct dsc_options *opt,
                        const uint8_t *data, size_t size, uint8_t *rgb, size_t capacity);

/* Output with 16-bit samples, one plane per component: R, G, B when
 * convert_rgb is set, else Y, Cb, Cr (no color conversion, DSC 1.2b §7.7).
 * The chroma planes of simple 4:2:2 and native 4:2:2 have half the width
 * (rounded up), those of native 4:2:0 half the width and half the height;
 * chroma is co-sited with the even-position luma samples. Samples are in the
 * low bits_per_component bits. stride and capacity count samples; each plane
 * needs capacity of at least stride * (height - 1) + width, stride at least
 * width. dsc_plane_size gives each plane's size. */
struct dsc_planes {
    uint16_t *plane[3];
    size_t stride[3], capacity[3];
};

/* Size of each output plane for the picture, or for one slice (slice != 0). */
int dsc_plane_size(const struct drm_dsc_config *cfg, int slice, unsigned width[3],
                   unsigned height[3]);

/* As dsc_decode_slice_ex and dsc_decode_frame_ex, for every supported format. */
int dsc_decode_slice_planes(const struct drm_dsc_config *cfg, const struct dsc_options *opt,
                            const uint8_t *data, size_t size, const struct dsc_planes *out);
int dsc_decode_frame_planes(const struct drm_dsc_config *cfg, const struct dsc_options *opt,
                            const uint8_t *data, size_t size, const struct dsc_planes *out);

const char *dsc_strerror(int status);

#endif
