/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Defaults for the switches of RESEARCH.md's open questions. Where the VESA
 * reference model, used as a black box, supports one reading, that reading
 * is the default; PROGRESS.md (Phase 5) records the evidence.
 */

#include "dsc.h"
#include <string.h>

void dsc_options_init(struct dsc_options *o)
{
    if (!o) {
        return;
    }
    memset(o, 0, sizeof(*o));
    o->flat_restart = DSC_FLAT_RESTART_IN_FLIGHT;
    o->threshold_eq = DSC_THRESHOLD_EQ_LOWER;
    o->frac_reset = DSC_FRAC_RESET_CHUNK;
    o->delay_offset = DSC_DELAY_OFFSET_INCLUSIVE;
    o->bp_left = DSC_BP_LEFT_MIDPOINT;
    o->bp_edge = DSC_BP_EDGE_WINDOW;
    o->bp_sad = DSC_BP_SAD_SHIFT;
    o->incr_order = DSC_INCR_ORDER_SWAPPED;
    o->rc_pipeline = DSC_RC_PIPELINE_RANGE_LAG;
    o->scale_dec = DSC_SCALE_DEC_FROM_GROUP_0;
    o->partial_target = DSC_PARTIAL_TARGET_PIXELS;
    o->very_flat = DSC_VERY_FLAT_PREVIOUS_QP;
    o->partial_padding = DSC_PARTIAL_PADDING_ACCEPT;
    o->flat_max_qp = DSC_FLAT_MAX_QP_PREVIOUS;
    o->delay_partial = DSC_DELAY_PARTIAL_GROUP_END;
}
