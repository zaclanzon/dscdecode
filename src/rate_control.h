/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef DSC_RATE_CONTROL_H
#define DSC_RATE_CONTROL_H

#include <stdint.h>
#include "dsc.h"

/* Inputs of one short-term RC evaluation (DSC 1.1 §6.8.4, Figure 6-12),
 * kept so the OQ-1 in-flight reading can re-run it after a flatness override. */
struct dsc_rc_inputs {
    int64_t fullness, model, target; /* bufferFullness, +rcXformOffset, rcTgtBitsGroup */
    unsigned actual, ideal, previous_ideal, min_qp, max_qp;
};

/* Bit-removal accumulators of §6.8.1 for one OQ-3 reading. */
struct dsc_rc_drain {
    uint32_t fractional_bits, chunk_bits, chunk_pixels;
    int64_t removed;
    int failed;
};

/* Private decoder state. All signed arithmetic is bounded, in bits or Q11. */
struct dsc_rc {
    const struct drm_dsc_config *cfg;
    struct dsc_options opt;
    int64_t fullness, offset_q11;
    uint64_t pixels, groups;
    uint32_t fractional_bits, chunk_bits, chunk_pixels; /* selected OQ-3 reading */
    int64_t removed;
    struct dsc_rc_drain shadow; /* the other OQ-3 reading */
    uint32_t scale, scale_clock;
    unsigned qp, pending_qp, last_qp, penultimate_qp, previous_ideal, used_qp;
    unsigned range, lag_range; /* range used; range computed last step (OQ-11) */
    struct dsc_rc_inputs last_inputs;
    int have_inputs, have_lag, clamp_offset, increasing, increase_next, failed, flat_override;
};

/* Defaults for every open-question switch; see dsc_options_init(). */
int dsc_rc_init(struct dsc_rc *, const struct drm_dsc_config *);

/* Replace the defaults. Call after dsc_rc_init and before the first group. */
void dsc_rc_set_options(struct dsc_rc *, const struct dsc_options *);

unsigned dsc_rc_qp(const struct dsc_rc *);
unsigned dsc_rc_primary_qp(const struct dsc_rc *);
int dsc_rc_apply_flat(struct dsc_rc *, int flat, int very_flat);
int dsc_rc_step(struct dsc_rc *, unsigned y, unsigned groupnum, unsigned pixels,
                unsigned actual_bits, unsigned ideal_bits);

#endif
