/* SPDX-License-Identifier: MIT */
#ifndef DSC_PREDICT_H
#define DSC_PREDICT_H
#include <stdint.h>
struct dsc_predict;
/* BP requests are unsupported until its boundary rule is established. */
struct dsc_predict *dsc_predict_create(unsigned width, unsigned height,
    unsigned line_depth, int block_prediction, int multiple_slices);
void dsc_predict_destroy(struct dsc_predict *p);
/* Component-major arrays. Calls must follow slice raster order, x += 3.
 * Only min(3,width-x) output pixels are valid. Zero success, -1 invalid input. */
int dsc_predict_group(struct dsc_predict *p, unsigned x, unsigned y,
    const unsigned qlevel[3], const int residual[3][3], const int mpp[3],
    int ich, const unsigned index[3], uint16_t out[3][3]);
#endif
