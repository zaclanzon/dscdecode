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
    /* 16-bit planes serve every format; RGB888 only 8 bpc RGB. */
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
    opt.stats = &stats;
    opt.trace = ignore_trace;
    dsc_decode_frame_ex(&c, &opt, data + 128, size - 128, out, 4096 * 3);
    dsc_decode_frame_planes(&c, &opt, data + 128, size - 128, &planes);
    free(out);
    free(samples);
    return 0;
}
