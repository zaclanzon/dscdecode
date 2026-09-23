/* SPDX-License-Identifier: MIT
 * Independent sample expectations from DSC 1.1 sections 6.3--6.5.
 */
#include "predict.h"
#include <assert.h>
#include <stdio.h>

static void line_storage(unsigned depth)
{
    struct dsc_predict *p = dsc_predict_create(9, 2, depth, 0, 0);
    const unsigned q[3] = {0,0,0};
    const int residual[3][3] = {{-28,-28,-28},{255,255,255},{1,1,1}};
    const int zero[3][3] = {{0}};
    const int mpp[3] = {1,1,1};
    const unsigned neighbors[3] = {25,26,31}, history[3] = {3,4,5};
    uint16_t pixels[3][3];
    unsigned x, j;
    assert(p);
    for (x=0; x<9; x+=3)
        assert(!dsc_predict_group(p,x,0,q,residual,mpp,0,neighbors,pixels));
    assert(!dsc_predict_group(p,0,1,q,zero,mpp,1,neighbors,pixels));
    for (j=0; j<3; ++j) {
        assert(pixels[0][j] == 100);
        /* 511 rounds to 256 in 8-bit storage, saturates to 255, then
         * restores to 510. 257 rounds to 129, then restores to 258. */
        assert(pixels[1][j] == (depth==8 ? 510 : 511));
        assert(pixels[2][j] == (depth==8 ? 258 : 257));
    }
    assert(!dsc_predict_group(p,3,1,q,zero,mpp,1,history,pixels));
    for (j=0; j<3; ++j) {
        /* Shift-register history retains original reconstructed precision. */
        assert(pixels[1][j] == 511 && pixels[2][j] == 257);
    }
    dsc_predict_destroy(p);
}

static void history_boundaries(void)
{
    const unsigned q[3] = {0}, indices[3] = {0,0,0};
    const int zero[3][3] = {{0}}, mpp[3] = {1,1,1};
    uint16_t pixels[3][3];
    struct dsc_predict *p = dsc_predict_create(3,2,9,0,0);
    assert(p);
    assert(!dsc_predict_group(p,0,0,q,zero,mpp,0,indices,pixels));
    /* A full three-pixel final group must not populate the history. */
    assert(dsc_predict_group(p,0,1,q,zero,mpp,1,indices,pixels) == -1);
    dsc_predict_destroy(p);
    p = dsc_predict_create(6,2,9,0,1);
    assert(p);
    assert(!dsc_predict_group(p,0,0,q,zero,mpp,0,indices,pixels));
    assert(!dsc_predict_group(p,3,0,q,zero,mpp,0,indices,pixels));
    /* Multiple horizontal slices invalidate the history at each line. */
    assert(dsc_predict_group(p,0,1,q,zero,mpp,1,indices,pixels) == -1);
    dsc_predict_destroy(p);
}

int main(void)
{
    line_storage(8);
    line_storage(9);
    history_boundaries();
    puts("Prediction sample traces passed (line precision and history boundaries)");
    return 0;
}
