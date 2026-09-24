/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef DSC_PREDICT_H
#define DSC_PREDICT_H
#include <stdint.h>
#include "dsc.h"
struct dsc_predict;
/* block_prediction mirrors the PPS block_pred_enable flag. */
struct dsc_predict *dsc_predict_create(unsigned width, unsigned height,
    unsigned line_depth, int block_prediction, int multiple_slices);
void dsc_predict_destroy(struct dsc_predict *p);
/* BP readings (OQ-4, OQ-10, OQ-13) and optional statistics. Call before the
 * first group; without it the dsc_options_init() defaults apply. */
void dsc_predict_set_options(struct dsc_predict *p, const struct dsc_options *o);
/* Component-major arrays. Calls must follow slice raster order, x += 3.
 * Only min(3,width-x) output pixels are valid. Zero success, -1 invalid input. */
int dsc_predict_group(struct dsc_predict *p, unsigned x, unsigned y,
    const unsigned qlevel[3], const int residual[3][3], const int mpp[3],
    int ich, const unsigned index[3], uint16_t out[3][3]);
#endif
