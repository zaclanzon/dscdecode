/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef DSC_PREDICT_H
#define DSC_PREDICT_H

#include <stdint.h>
#include "dsc.h"
#include "format.h"

struct dsc_predict;

/* block_prediction mirrors the PPS block_pred_enable flag. width is in
 * pixels of the coded format. */
struct dsc_predict *dsc_predict_create_format(const struct dsc_format *f, unsigned width,
                                              unsigned height, unsigned line_depth,
                                              int block_prediction, int multiple_slices);

/* The 8 bpc RGB format of DSC 1.1 (dsc_format_default). */
struct dsc_predict *dsc_predict_create(unsigned width, unsigned height, unsigned line_depth,
                                       int block_prediction, int multiple_slices);
void dsc_predict_destroy(struct dsc_predict *p);

/* BP readings (OQ-4, OQ-10, OQ-13) and optional statistics. Call before the
 * first group; without it the dsc_options_init() defaults apply. */
void dsc_predict_set_options(struct dsc_predict *p, const struct dsc_options *o);

/* Unit-major arrays, one row per unit of the format. Calls must follow slice
 * raster order, x += 3. Only min(3,width-x) output pixels are valid. Zero
 * success, -1 invalid input. */
int dsc_predict_group_units(struct dsc_predict *p, unsigned x, unsigned y,
                            const unsigned qlevel[4], const int residual[4][3], const int mpp[4],
                            int ich, const unsigned index[3], uint16_t out[4][3]);

/* The same for a three-unit format. */
int dsc_predict_group(struct dsc_predict *p, unsigned x, unsigned y, const unsigned qlevel[3],
                      const int residual[3][3], const int mpp[3], int ich, const unsigned index[3],
                      uint16_t out[3][3]);

#endif
