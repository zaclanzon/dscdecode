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

int main(void)
{
    qp_trace();
    fractional_chunk();
    invalid_traces();
    puts("RC hand-calculated traces passed (not a conformance oracle)");
    return 0;
}
