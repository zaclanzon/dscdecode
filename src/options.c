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
    /* DSC 1.2 readings, set by the model's decodes of the discriminators
     * (RESEARCH.md). */
    o->bpg_combine = DSC_BPG_COMBINE_ADD;
    o->chroma_qlevel = DSC_CHROMA_QLEVEL_EQUAL_DEPTH;
    o->prefix16 = DSC_PREFIX16_13;
    o->bitsave_ich = DSC_BITSAVE_ICH_NOT;
    o->bitsave_pred = DSC_BITSAVE_PRED_NEXT;
    o->bitsave_flat = DSC_BITSAVE_FLAT_LAGGED;
    o->line_flat = DSC_LINE_FLAT_SIGNALED;
    /* Found on model-encoded DSC 1.2 streams and decided by the model's
     * decodes of the discriminators (PROGRESS.md, Phase 4). */
    o->low_min = DSC_LOW_MIN_MIN_QP;
    o->decrement_test = DSC_DECREMENT_SIZE;
    o->activity_qp = DSC_ACTIVITY_PREV2;
    o->bitsave_step = DSC_BITSAVE_STEP_2;
    o->target_floor = DSC_TARGET_FLOOR_ZERO;
    o->flat_rerun = DSC_FLAT_RERUN_EVERY;
    o->rerun_bitsave = DSC_RERUN_BITSAVE_REDO;
    o->mux16 = DSC_MUX16_WORD;
    o->flat_top = DSC_FLAT_TOP_AT_OR_ABOVE;
    o->prefix16_scope = DSC_PREFIX16_SCOPE_QLEVEL;
    o->prefix16_cut = DSC_PREFIX16_CUT_LONGER;
    /* Native 4:2:2 and 4:2:0 (OQ-37 to OQ-41) and the scale decrement
     * (OQ-42, OQ-43, DSC 1.1 and 1.2 alike): set by the model's decodes of
     * the discriminators (PROGRESS.md, Phase 5). */
    o->activity420 = DSC_ACTIVITY420_LUMA;
    o->activity422 = DSC_ACTIVITY422_SIZES;
    o->bp420_edge = DSC_BP420_EDGE_LUMA;
    o->offset_adj = DSC_OFFSET_ADJ_START;
    o->ich_window = DSC_ICH_WINDOW_CONTAINER;
    o->scale_first = DSC_SCALE_FIRST_NOT;
    o->scale_line = DSC_SCALE_LINE_FIRST;
}
