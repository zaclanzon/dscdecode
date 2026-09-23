/* SPDX-License-Identifier: MIT */
#ifndef DSC_RATE_CONTROL_H
#define DSC_RATE_CONTROL_H
#include <stdint.h>
#include <drm/display/drm_dsc.h>

/* Private decoder state. All signed arithmetic is bounded, in bits or Q11. */
struct dsc_rc {
    const struct drm_dsc_config *cfg;
    int64_t fullness, offset_q11;
    uint64_t pixels, groups;
    uint32_t chunk_pixels, chunk_bits, fractional_bits;
    uint32_t scale, scale_clock;
    unsigned qp, pending_qp, last_qp, penultimate_qp, previous_ideal;
    int clamp_offset, increasing, increase_next, failed;
};
int dsc_rc_init(struct dsc_rc *, const struct drm_dsc_config *);
unsigned dsc_rc_qp(const struct dsc_rc *);
unsigned dsc_rc_primary_qp(const struct dsc_rc *);
int dsc_rc_apply_flat(struct dsc_rc *, int flat, int very_flat);
int dsc_rc_step(struct dsc_rc *, unsigned y, unsigned groupnum,
                unsigned pixels, unsigned actual_bits, unsigned ideal_bits);
#endif
