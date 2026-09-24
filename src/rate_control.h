/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef DSC_RATE_CONTROL_H
#define DSC_RATE_CONTROL_H

#include <stdint.h>
#include "dsc.h"
#include "format.h"

/* Inputs of one short-term RC evaluation (DSC 1.1 §6.8.4, Figure 6-12;
 * DSC 1.2b Figures 6-17 and 6-18), kept so the OQ-1 in-flight reading can
 * re-run it after a flatness override. */
struct dsc_rc_inputs {
    int64_t fullness, model, target; /* bufferFullness, +rcXformOffset, rcTgtBitsGroup */
    unsigned actual, ideal, previous_ideal, min_qp, max_qp;
    int v12, zero;                   /* DSC 1.2 rules; all residuals zero */
    unsigned bit_save, top_qp;       /* bitSaveMode; 2 * bpc - 1 */
};

/* What the entropy decoder reports for one group (DSC 1.2b Table 7-1). */
struct dsc_rc_group {
    unsigned actual, ideal;     /* codedBits, rcSizeGroup */
    unsigned mpp;               /* midpointSelected: units coded with MPP */
    unsigned predicted[4];      /* predictedSize per unit, as OQ-23 selects */
    int ich, zero, flat;        /* ichSelected; zero residuals; flatness signaled (OQ-24) */
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
    unsigned max_qp, flat_type_qp, very_flat_qp; /* scaled by bpc (dsc_qp_scale) */
    int v12;                                     /* dsc_version_minor 2 */
    unsigned bit_save, mpp_state, bit_save_thresh; /* DSC 1.2b §6.8.4 */
    /* The last step's group and bitSaveMode state before its update, for a
     * re-run that computes bitSaveMode again (OQ-32). */
    struct dsc_rc_group step_group;
    unsigned step_y, step_bit_save, step_mpp_state;
    int have_step_group;
    int64_t fullness, offset_q11;
    uint64_t pixels, groups;
    uint64_t delay_group_end; /* end of the previous group, three pixels per group (OQ-19) */
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

/* As dsc_rc_apply_flat; line_start marks the first group of a line after
 * the first, which DSC 1.2 adjusts as very flat (DSC 1.2b §6.8.5.2). */
int dsc_rc_apply_flat_line(struct dsc_rc *, int flat, int very_flat, int line_start);

int dsc_rc_step(struct dsc_rc *, unsigned y, unsigned groupnum, unsigned pixels,
                unsigned actual_bits, unsigned ideal_bits);

/* dsc_rc_step with everything DSC 1.2's rate control uses. */
int dsc_rc_step_group(struct dsc_rc *, unsigned y, unsigned groupnum, unsigned pixels,
                      const struct dsc_rc_group *g);

#endif
