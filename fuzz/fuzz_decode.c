/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include "dsc.h"
#include <stdlib.h>

static void ignore_trace(void *context, const struct dsc_group_trace *t)
{
    (void)context;
    (void)t;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct drm_dsc_config c;
    struct dsc_options opt;
    struct dsc_stats stats = {0};
    struct dsc_planes planes;
    uint8_t *out, mix = 0, sum = 0;
    uint16_t *samples;
    size_t pixels, i;

    if (size < DSC_PPS_BYTES || dsc_parse_pps(data, DSC_PPS_BYTES, &c)) {
        return 0;
    }
    pixels = (size_t)c.pic_width * c.pic_height;
    /* Bound per-input work so coverage-guided fuzzing cannot allocate enormous
     * frames. Public decoder has its own independent, larger resource limits. */
    if (pixels > 4096 || (size_t)c.slice_width * c.slice_height > 4096) {
        return 0;
    }
    out = malloc(4096 * 3);
    samples = malloc(4096 * 3 * sizeof(*samples));
    if (!out || !samples) {
        free(out);
        free(samples);
        return 0;
    }
    /* 16-bit planes serve every format (YCbCr chroma planes are smaller);
     * RGB888 only 8 bpc RGB. */
    for (i = 0; i < 3; i++) {
        planes.plane[i] = samples + 4096 * i;
        planes.stride[i] = c.pic_width > c.slice_width ? c.pic_width : c.slice_width;
        planes.capacity[i] = 4096;
    }
    dsc_decode_frame(&c, data + 128, size - 128, out, 4096 * 3);
    dsc_decode_slice(&c, data + 128, size - 128, out, 4096 * 3);
    dsc_decode_slice_planes(&c, NULL, data + 128, size - 128, &planes);
    /* Also decode under a combination of the open-question readings, taken
     * from the payload bytes so every input stays reproducible. */
    for (i = DSC_PPS_BYTES; i < size; i++) {
        mix ^= data[i];
        sum = (uint8_t)(sum + data[i]);
    }
    dsc_options_init(&opt);
    opt.flat_restart = mix & 1;
    opt.threshold_eq = (mix >> 1) & 1;
    opt.frac_reset = (mix >> 2) & 1;
    opt.delay_offset = (mix >> 3) & 1;
    opt.bp_left = (mix >> 4) & 1;
    opt.bp_edge = (mix >> 5) & 1;
    opt.bp_sad = (mix >> 6) & 1;
    opt.incr_order = (mix >> 7) & 1;
    opt.rc_pipeline = sum & 1;
    opt.scale_dec = (sum >> 1) & 1;
    opt.partial_target = (sum >> 2) & 1;
    opt.very_flat = ((sum >> 3) & 3) % 3;
    opt.partial_padding = (sum >> 5) & 1;
    opt.flat_max_qp = (sum >> 6) & 1;
    opt.delay_partial = (sum >> 7) & 1;
    /* DSC 1.2 readings, from the payload length. */
    opt.bpg_combine = size & 1;
    opt.chroma_qlevel = (size >> 1) & 1;
    opt.prefix16 = (size >> 2) & 1;
    opt.bitsave_ich = (size >> 3) & 1;
    opt.bitsave_pred = (int)((size >> 4) % 3);
    opt.bitsave_flat = (int)(((size >> 6) & 7) % 6);
    opt.line_flat = (size >> 7) & 1;
    /* OQ-26 to OQ-34, from a mix of the others. */
    {
        unsigned bits = sum ^ (mix << 4) ^ (unsigned)(size >> 8);

        opt.low_min = bits & 1;
        opt.decrement_test = (bits >> 1) & 1;
        opt.activity_qp = (bits >> 2) & 1;
        opt.bitsave_step = (bits >> 3) & 1;
        opt.target_floor = (bits >> 4) & 1;
        opt.flat_rerun = (bits >> 5) & 1;
        opt.rerun_bitsave = (bits >> 6) & 1;
        opt.mux16 = (bits >> 7) & 1;
        opt.flat_top = (bits >> 8) & 1;
        opt.prefix16_scope = (bits >> 9) & 1;
        opt.prefix16_cut = (bits >> 10) & 1;
    }
    /* OQ-37 to OQ-43: native 4:2:2 and 4:2:0, the scale decrement. */
    {
        unsigned more = ((unsigned)mix + 3u * sum) ^ (unsigned)(size >> 3);

        opt.activity420 = more & 1;
        opt.activity422 = (more >> 1) & 1;
        opt.bp420_edge = (more >> 2) & 1;
        opt.offset_adj = (more >> 3) & 1;
        opt.ich_window = (more >> 4) & 1;
        opt.scale_first = (more >> 5) & 1;
        opt.scale_line = (more >> 6) & 1;
    }
    opt.stats = &stats;
    opt.trace = ignore_trace;
    dsc_decode_frame_ex(&c, &opt, data + 128, size - 128, out, 4096 * 3);
    dsc_decode_frame_planes(&c, &opt, data + 128, size - 128, &planes);
    dsc_decode_slice_planes(&c, &opt, data + 128, size - 128, &planes);
    free(out);
    free(samples);
    return 0;
}
