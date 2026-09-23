/* SPDX-License-Identifier: MIT
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
    for (i = 0; i < 14; ++i) c.rc_buf_thresh[i] = (u16)((i + 1) * 8);
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
    puts("RC hand-calculated traces passed (not a conformance oracle)");
    return 0;
}
