/* SPDX-License-Identifier: MIT
 * Defaults for the switches of RESEARCH.md's open questions.
 */
#include "dsc.h"
#include <string.h>

void dsc_options_init(struct dsc_options *o)
{
    if (!o) return;
    memset(o, 0, sizeof(*o));
    o->flat_restart = DSC_FLAT_RESTART_NEXT_CYCLE;
    o->threshold_eq = DSC_THRESHOLD_EQ_LOWER;
    o->frac_reset = DSC_FRAC_RESET_CHUNK;
    o->delay_offset = DSC_DELAY_OFFSET_INCLUSIVE;
    o->bp_left = DSC_BP_LEFT_REPLICATE;
    o->bp_edge = DSC_BP_EDGE_WINDOW;
    o->bp_sad = DSC_BP_SAD_SHIFT;
}
