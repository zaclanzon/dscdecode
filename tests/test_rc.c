/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Hand-calculated RC traces from DSC 1.1 section 6.8. These are unit tests,
 * not compressed-stream interoperability vectors. See rc-ambiguities.md.
 */

#include "rate_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct drm_dsc_config settings(void)
{
    struct drm_dsc_config c;
    unsigned i;

    memset(&c, 0, sizeof(c));
    c.slice_width = 30;
    c.slice_height = 3;
    c.bits_per_component = 8;
    c.bits_per_pixel = 128;
    c.rc_model_size = 8192;
    c.initial_offset = 6144;
    c.final_offset = 4336;
    c.initial_scale_value = 8;
    c.initial_xmit_delay = 512;
    c.initial_dec_delay = 512;
    c.slice_chunk_size = 30;
    c.rc_edge_factor = 6;
    c.rc_quant_incr_limit0 = 11;
    c.rc_quant_incr_limit1 = 11;
    c.rc_tgt_offset_low = 3;
    c.rc_tgt_offset_high = 3;
    for (i = 0; i < 14; ++i) {
        c.rc_buf_thresh[i] = (u16)((i + 1) * 8);
    }
    for (i = 0; i < 15; ++i) {
        c.rc_range_params[i].range_min_qp = 8;
        c.rc_range_params[i].range_max_qp = 8;
    }
    c.rc_range_params[14].range_max_qp = 15;
    return c;
}

static void qp_trace(void)
{
    struct drm_dsc_config c = settings();
    struct dsc_rc r;
    unsigned i;
    /* No drain occurs before pixel 512. Initial offset is -2048 bits and
     * decreases by 24/group. All encountered ranges have min=max=8.
     * First group's 100 bits drive QP to8; 24 is inside [21,27], preserving
     * 8. Zero-residual groups then produce7 and6 (minimum is minQP/2=4).
     * Figure6-8 makes the generated values visible two groups later. */
    static const unsigned coded[] = {100, 24, 3, 3};
    static const unsigned visible_qp[] = {0, 8, 8, 7};
    static const int64_t fullness[] = {100, 124, 127, 130};
    static const int64_t offset[] = {-2072, -2096, -2120, -2144};

    assert(dsc_rc_init(&r, &c) == 0);
    assert(dsc_rc_qp(&r) == 0);
    for (i = 0; i < 4; ++i) {
        assert(dsc_rc_step(&r, 0, i, 3, coded[i], coded[i]) == 0);
        assert(dsc_rc_qp(&r) == visible_qp[i]);
        assert(r.fullness == fullness[i]);
        assert(r.offset_q11 == offset[i] * 2048);
    }
    /* Section6.8.5.2: somewhat-flat reduces7 to3. This does not assert
     * the unresolved interaction of flatness with later pending QPs. */
    assert(dsc_rc_apply_flat(&r, 1, 0) == 0);
    assert(dsc_rc_qp(&r) == 3);
}

static void fractional_chunk(void)
{
    struct drm_dsc_config c = settings();
    struct dsc_rc r;

    c.slice_width = 7;
    c.bits_per_pixel = 129; /* 8 + 1/16 bits per pixel. */
    c.slice_chunk_size = 8;
    c.initial_xmit_delay = 1;
    assert(dsc_rc_init(&r, &c) == 0);
    /* Three pixels remove24 bits, retaining3/16. Three more remove24,
     * retaining6/16. Last pixel removes8: floor(7*129/16)=56 bits.
     * Complete the eight-byte chunk with8 more zero bits; reset fraction.
     * 100-bit coded groups therefore leave76,152,236 bits in the model. */
    assert(dsc_rc_step(&r, 0, 0, 3, 100, 100) == 0);
    assert(r.fullness == 76 && r.fractional_bits == 3 && r.chunk_bits == 24);
    assert(dsc_rc_step(&r, 0, 1, 3, 100, 100) == 0);
    assert(r.fullness == 152 && r.fractional_bits == 6 && r.chunk_bits == 48);
    assert(dsc_rc_step(&r, 0, 2, 1, 100, 100) == 0);
    assert(r.fullness == 236 && r.fractional_bits == 0 && r.chunk_bits == 0);
    assert(r.chunk_pixels == 0);
    assert(dsc_rc_step(&r, 1, 3, 3, 100, 100) == 0);
    assert(r.fullness == 312 && r.fractional_bits == 3);
}

static void invalid_traces(void)
{
    struct drm_dsc_config c = settings();
    struct dsc_rc r;

    c.initial_xmit_delay = 1;
    assert(dsc_rc_init(&r, &c) == 0);
    /* CBR supplies3 bits while24 leave: model fullness would be -21. */
    assert(dsc_rc_step(&r, 0, 0, 3, 3, 3) == -1);
    assert(dsc_rc_apply_flat(&r, 1, 1) == -1);
    c.initial_dec_delay = 1;
    assert(dsc_rc_init(&r, &c) == 0);
    /* Two delay-pixel times at8bpp permit16 bits, less than100 input. */
    assert(dsc_rc_step(&r, 0, 0, 3, 100, 100) == -1);
    c = settings();
    assert(dsc_rc_init(&r, &c) == 0);
    assert(dsc_rc_step(&r, 0, 1, 3, 24, 24) == -1);
    assert(dsc_rc_init(&r, &c) == 0);
    assert(dsc_rc_step(&r, 0, 0, 0, 24, 24) == -1);
    c.vbr_enable = 1;
    assert(dsc_rc_init(&r, &c) == -1);
}

/* Section 6.8.5.2: an override that leaves masterQp unchanged (QP 0,
 * somewhat flat) must not restart short-term RC. */
static void flat_unmodified(void)
{
    struct drm_dsc_config c = settings();
    struct dsc_rc r;

    assert(dsc_rc_init(&r, &c) == 0);
    /* 100 bits generate QP 8 for group 2; group 1 still decodes at QP 0. */
    assert(dsc_rc_step(&r, 0, 0, 3, 100, 100) == 0);
    assert(dsc_rc_qp(&r) == 0 && r.last_qp == 8);
    assert(dsc_rc_apply_flat(&r, 1, 0) == 0);
    assert(dsc_rc_qp(&r) == 0 && r.last_qp == 8 && !r.flat_override);
}

/* OQ-1. Groups 0 and 1 give QP 8 queued for groups 2 and 3 (see qp_trace).
 * Group 2 is somewhat flat: masterQp 8 becomes 4. Group 1's 24 bits were in
 * target, so re-running its cycle from prevQp 4 gives 4. The next-cycle
 * reading keeps the queued 8 for group 3; the in-flight reading replaces it
 * with 4. Group 2's own 24 bits are in target, so both generate 4 next. */
static void flat_restart(int reading, unsigned group3_qp)
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_stats st = {0};
    struct dsc_rc r;

    dsc_options_init(&o);
    o.flat_restart = reading;
    o.stats = &st;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    assert(dsc_rc_step(&r, 0, 0, 3, 100, 100) == 0);
    assert(dsc_rc_step(&r, 0, 1, 3, 24, 24) == 0);
    assert(dsc_rc_qp(&r) == 8 && r.pending_qp == 8);
    assert(dsc_rc_apply_flat(&r, 1, 0) == 0);
    assert(dsc_rc_qp(&r) == 4 && r.flat_override);
    assert(st.flat_overrides == 1 && st.flat_queue_differs == 1);
    assert(dsc_rc_step(&r, 0, 2, 3, 24, 24) == 0);
    assert(dsc_rc_qp(&r) == group3_qp && r.pending_qp == 4);
}

/* OQ-2. Thresholds are 512(i+1)-8192. Starting fullness+offset is
 * 6144-8192 = -2048. A 24-bit group adds 24 bits and, inside the initial
 * delay, lowers the offset by 24: rcModelFullness stays -2048, exactly
 * threshold 11. */
static void threshold_equality(int reading, unsigned range)
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_stats st = {0};
    struct dsc_rc r;

    dsc_options_init(&o);
    o.threshold_eq = reading;
    o.rc_pipeline = DSC_RC_PIPELINE_SAME_GROUP; /* range of this step itself */
    o.stats = &st;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    assert(dsc_rc_step(&r, 0, 0, 3, 24, 24) == 0);
    assert(r.last_inputs.model == -2048 && r.range == range);
    assert(st.threshold_equal == 1);
}

/* OQ-3, literal reading, same geometry as fractional_chunk: 8+1/16 bpp,
 * width 7, delay 1. Pixel 1 is (pixelCount-delay)%width==0, so its 1/16
 * is dropped. Pixels 2-3 keep 2/16; pixels 4-6 keep 5/16; pixel 7 keeps
 * 6/16 and ends the chunk (removed 56, padding 8) without a reset. Pixel 8
 * is 7 after the delay, a multiple of the width: 7/16 then reset; pixels
 * 9-10 leave 2/16. Whole-bit removal matches the chunk reading here. */
static void fractional_literal(void)
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_rc r;

    c.slice_width = 7;
    c.bits_per_pixel = 129;
    c.slice_chunk_size = 8;
    c.initial_xmit_delay = 1;
    dsc_options_init(&o);
    o.frac_reset = DSC_FRAC_RESET_LITERAL;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    assert(dsc_rc_step(&r, 0, 0, 3, 100, 100) == 0);
    assert(r.fullness == 76 && r.fractional_bits == 2 && r.chunk_bits == 24);
    assert(dsc_rc_step(&r, 0, 1, 3, 100, 100) == 0);
    assert(r.fullness == 152 && r.fractional_bits == 5 && r.chunk_bits == 48);
    assert(dsc_rc_step(&r, 0, 2, 1, 100, 100) == 0);
    assert(r.fullness == 236 && r.fractional_bits == 6 && r.chunk_bits == 0);
    assert(dsc_rc_step(&r, 1, 3, 3, 100, 100) == 0);
    assert(r.fullness == 312 && r.fractional_bits == 2);
}

/* OQ-12. Delay 4 with 8 bpp, three-pixel groups. Pixel 4 is the first to
 * remove bits. Inclusive: group 1 (pixels 4-6) lowers the offset for pixel 4
 * too, so after two groups the offset has fallen by 4*8=32 bits. Exclusive:
 * by 3*8=24 bits. Initial offset is 6144-8192 = -2048. */
static void delay_boundary(int reading, int64_t offset)
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_rc r;

    c.initial_xmit_delay = 4;
    dsc_options_init(&o);
    o.delay_offset = reading;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    assert(dsc_rc_step(&r, 0, 0, 3, 40, 40) == 0);
    assert(dsc_rc_step(&r, 0, 1, 3, 40, 40) == 0);
    assert(r.offset_q11 == offset * 2048);
    /* Pixels 4-6 removed 24 bits from the 80 received. */
    assert(r.fullness == 56);
}

/* OQ-19. Slice 4 pixels wide: a full group and a one-pixel group per line;
 * 8 bpp, offset -2048 at the start, no BPG offsets. Pixels: the offset
 * falls 24, 8, 24, 8 bits per group while the delay lasts. Group-end: each
 * group counts to its end as if it had three pixels, from its real
 * position (ends 3, 6, 7, 10): 24, 24, 8, 24. With initial_xmit_delay 5
 * both readings stop after five pixels' worth, 40 bits. */
static void delay_partial(int reading, unsigned delay, const int64_t offset[4])
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_rc r;
    unsigned g;

    c.slice_width = 4;
    c.slice_chunk_size = 4;
    c.initial_xmit_delay = (u16)delay;
    dsc_options_init(&o);
    o.delay_partial = reading;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    for (g = 0; g < 4; ++g) {
        assert(dsc_rc_step(&r, g / 2, g, g % 2 ? 1 : 3, 12, 12) == 0);
        assert(r.offset_q11 == offset[g] * 2048);
    }
}

/* DSC 1.2b §6.8.4 (Figure 6-17) with every range min 4, max 8. One group of
 * a second line reports its sizes; the step's result is the QP queued two
 * groups ahead. prevQp starts at 6 (set directly). */
static unsigned v12_step(unsigned fullness_bits, unsigned actual, unsigned ideal, int zero,
                         unsigned mpp, unsigned bit_save)
{
    struct drm_dsc_config c = settings();
    struct dsc_rc_group g;
    struct dsc_rc r;
    unsigned i;

    c.dsc_version_major = 1;
    c.dsc_version_minor = 2;
    c.convert_rgb = 1;
    c.slice_height = 2;
    for (i = 0; i < 15; ++i) {
        c.rc_range_params[i].range_min_qp = 4;
        c.rc_range_params[i].range_max_qp = 8;
    }
    assert(dsc_rc_init(&r, &c) == 0);
    /* Line 1, pixel 30: skip line 0's accounting by setting the state. */
    r.pixels = 30;
    r.groups = 10;
    r.fullness = fullness_bits - actual;
    r.last_qp = r.penultimate_qp = 6;
    r.bit_save = bit_save;
    r.mpp_state = bit_save ? 2 : 0;
    memset(&g, 0, sizeof(g));
    g.actual = actual;
    g.ideal = ideal;
    g.zero = zero;
    g.mpp = mpp;
    assert(dsc_rc_step_group(&r, 1, 10, 3, &g) == 0);
    return r.pending_qp;
}

static void v12_short_term(void)
{
    /* Target 24; tgtMinus 21, tgtPlus 27. Buffer below 192: minQp 4. */
    assert(v12_step(150, 60, 50, 0, 0, 0) == 4);
    /* Same group with 300 bits buffered: increment from curQp 6 by
     * (60 - 24) >> 1 = 18, edge test fails (no previous rcSizeGroup), and
     * curQp 6 equals prev2Qp: stays 6. */
    assert(v12_step(300, 60, 50, 0, 0, 0) == 6);
    /* bitSaveMode 2 kept by an MPP group: prevQp + 1 = 7, below
     * adjustedMaxQp MIN(15, 9). */
    assert(v12_step(300, 60, 50, 0, 3, 2) == 7);
    /* Zero residuals: prevQp - 1 = 5 with minQp lowered to MAX(8 - 4, 0) = 4. */
    assert(v12_step(300, 3, 3, 1, 0, 0) == 5);
    /* A small group: prevQp - 1 = 5, clamped to minQp 4 only below it. */
    assert(v12_step(300, 10, 10, 0, 0, 0) == 5);
}

/* OQ-7 at DSC 1.2: first line, first_line_bpg_offset 15, no second-line
 * terms. rcTgtBitsGroup is 24 + 15 = 39 (add) or 24 (replace). */
static void bpg_combine(int reading, int64_t target)
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_rc r;

    c.dsc_version_major = 1;
    c.dsc_version_minor = 2;
    c.convert_rgb = 1;
    c.first_line_bpg_offset = 15;
    dsc_options_init(&o);
    o.bpg_combine = reading;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    assert(dsc_rc_step(&r, 0, 0, 3, 40, 40) == 0);
    assert(r.last_inputs.target == target);
}

/* OQ-5. Every range allows QP 8..15. Group 0 codes 100 bits: target 24,
 * so the increment branch runs with curQp = MAX(8, 0) = 8 against
 * prev2Qp 0, and the edge test fails (no previous group). Printed: curQp is
 * not below prev2Qp, so only curQp < limit1 (11) is needed: 8 + 38 capped at
 * 15. Swapped: curQp above prev2Qp needs the edge test: stays 8. */
static void increment_order(int reading, unsigned queued)
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_rc r;
    unsigned i;

    for (i = 0; i < 15; ++i) {
        c.rc_range_params[i].range_max_qp = 15;
    }
    dsc_options_init(&o);
    o.incr_order = reading;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    assert(dsc_rc_step(&r, 0, 0, 3, 100, 100) == 0);
    assert(r.pending_qp == queued);
}

/* OQ-11. Range i pins QP i. Group 0 codes 250 bits: rcModelFullness
 * -2048 + 250 - 24 = -1822, range 12. Same-group: the increment branch starts
 * from minQp 12 and range 12 caps it at 12. Range-lag: there is no earlier
 * step, so range 0 applies and the result is QP 0. */
static void range_pipeline(int reading, unsigned queued)
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_stats st = {0};
    struct dsc_rc r;
    unsigned i;

    for (i = 0; i < 15; ++i) {
        c.rc_range_params[i].range_min_qp = (u8)i;
        c.rc_range_params[i].range_max_qp = (u8)(i < 14 ? i : 15);
    }
    dsc_options_init(&o);
    o.rc_pipeline = reading;
    o.stats = &st;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    assert(dsc_rc_step(&r, 0, 0, 3, 250, 250) == 0);
    assert(r.last_inputs.model == -1822 && r.pending_qp == queued);
    assert(st.range_lag_differs == (reading == DSC_RC_PIPELINE_RANGE_LAG));
}

/* OQ-14. Initial scale 12 (1.5), decrement interval 2. Counting from group 1
 * the scale drops after groups 2 and 4; counting from group 0, after groups
 * 1 and 3. */
static void scale_decrement(int reading, const unsigned expect[5])
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_rc r;
    unsigned g;

    c.initial_scale_value = 12;
    c.scale_decrement_interval = 2;
    dsc_options_init(&o);
    o.scale_dec = reading;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    for (g = 0; g < 5; ++g) {
        assert(dsc_rc_step(&r, 0, g, 3, 24, 24) == 0);
        assert(r.scale == expect[g]);
    }
}

/* OQ-15. Width 7: the third group of a line has one pixel. At 8 bpp and zero
 * BPG offsets, its target is 24 (three samples) or 8 (one pixel). */
static void partial_target(int reading, int64_t target)
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_rc r;

    c.slice_width = 7;
    c.slice_chunk_size = 7;
    dsc_options_init(&o);
    o.partial_target = reading;
    o.rc_pipeline = DSC_RC_PIPELINE_SAME_GROUP;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    assert(dsc_rc_step(&r, 0, 0, 3, 24, 24) == 0);
    assert(dsc_rc_step(&r, 0, 1, 3, 24, 24) == 0);
    assert(r.last_inputs.target == 24);
    assert(dsc_rc_step(&r, 0, 2, 1, 8, 8) == 0);
    assert(r.last_inputs.target == target);
}

/* OQ-16. A very-flat signal on a group whose own QP is `own`, after a group
 * decoded at `previous`. Demoted to somewhat flat it gives own - 4; kept very
 * flat it gives QP 1. Group-qp tests own < 7, previous-qp tests previous < 7,
 * as-signaled never demotes. */
static void very_flat_type(int reading, unsigned own, unsigned previous, unsigned qp)
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_stats st = {0};
    struct dsc_rc r;

    dsc_options_init(&o);
    o.very_flat = reading;
    o.stats = &st;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    r.qp = own;
    r.used_qp = previous;
    assert(dsc_rc_apply_flat(&r, 1, 1) == 0);
    assert(dsc_rc_qp(&r) == qp && st.very_flat_low_qp == 1);
}

/* OQ-18. A somewhat-flat signal where one of the flagged group's own QP and
 * the previous group's QP is range 14's maximum (15 in settings()) and the
 * other is not. The override applies (QP - 4) only when the QP the reading
 * checks is below that maximum. */
static void flat_max_qp(int reading, unsigned own, unsigned previous, unsigned qp)
{
    struct drm_dsc_config c = settings();
    struct dsc_options o;
    struct dsc_stats st = {0};
    struct dsc_rc r;

    assert(c.rc_range_params[14].range_max_qp == 15);
    dsc_options_init(&o);
    o.flat_max_qp = reading;
    o.stats = &st;
    assert(dsc_rc_init(&r, &c) == 0);
    dsc_rc_set_options(&r, &o);
    r.qp = own;
    r.used_qp = previous;
    assert(dsc_rc_apply_flat(&r, 1, 0) == 0);
    assert(dsc_rc_qp(&r) == qp && st.flat_max_qp_differs == 1);
}

int main(void)
{
    qp_trace();
    fractional_chunk();
    invalid_traces();
    flat_unmodified();
    flat_restart(DSC_FLAT_RESTART_NEXT_CYCLE, 8);
    flat_restart(DSC_FLAT_RESTART_IN_FLIGHT, 4);
    threshold_equality(DSC_THRESHOLD_EQ_LOWER, 11);
    threshold_equality(DSC_THRESHOLD_EQ_UPPER, 12);
    fractional_literal();
    delay_boundary(DSC_DELAY_OFFSET_INCLUSIVE, -2048 - 32);
    delay_boundary(DSC_DELAY_OFFSET_EXCLUSIVE, -2048 - 24);
    {
        static const int64_t pixels[4] = {-2072, -2080, -2104, -2112};
        static const int64_t group_end[4] = {-2072, -2096, -2104, -2128};
        static const int64_t pixels5[4] = {-2072, -2080, -2088, -2088};
        static const int64_t group_end5[4] = {-2072, -2088, -2088, -2088};

        delay_partial(DSC_DELAY_PARTIAL_PIXELS, 512, pixels);
        delay_partial(DSC_DELAY_PARTIAL_GROUP_END, 512, group_end);
        delay_partial(DSC_DELAY_PARTIAL_PIXELS, 5, pixels5);
        delay_partial(DSC_DELAY_PARTIAL_GROUP_END, 5, group_end5);
    }
    increment_order(DSC_INCR_ORDER_PRINTED, 15);
    increment_order(DSC_INCR_ORDER_SWAPPED, 8);
    range_pipeline(DSC_RC_PIPELINE_SAME_GROUP, 12);
    range_pipeline(DSC_RC_PIPELINE_RANGE_LAG, 0);
    {
        static const unsigned from1[5] = {12, 12, 11, 11, 10}, from0[5] = {12, 11, 11, 10, 10};

        scale_decrement(DSC_SCALE_DEC_FROM_GROUP_1, from1);
        scale_decrement(DSC_SCALE_DEC_FROM_GROUP_0, from0);
    }
    partial_target(DSC_PARTIAL_TARGET_THREE, 24);
    partial_target(DSC_PARTIAL_TARGET_PIXELS, 8);
    very_flat_type(DSC_VERY_FLAT_GROUP_QP, 6, 7, 2);
    very_flat_type(DSC_VERY_FLAT_AS_SIGNALED, 6, 7, 1);
    very_flat_type(DSC_VERY_FLAT_PREVIOUS_QP, 6, 7, 1);
    very_flat_type(DSC_VERY_FLAT_GROUP_QP, 8, 6, 1);
    very_flat_type(DSC_VERY_FLAT_AS_SIGNALED, 8, 6, 1);
    very_flat_type(DSC_VERY_FLAT_PREVIOUS_QP, 8, 6, 4);
    flat_max_qp(DSC_FLAT_MAX_QP_OWN, 15, 11, 15);
    flat_max_qp(DSC_FLAT_MAX_QP_PREVIOUS, 15, 11, 11);
    flat_max_qp(DSC_FLAT_MAX_QP_OWN, 12, 15, 8);
    flat_max_qp(DSC_FLAT_MAX_QP_PREVIOUS, 12, 15, 12);
    v12_short_term();
    bpg_combine(DSC_BPG_COMBINE_ADD, 39);
    bpg_combine(DSC_BPG_COMBINE_REPLACE, 24);
    puts("RC hand-calculated traces passed (not a conformance oracle)");
    return 0;
}
