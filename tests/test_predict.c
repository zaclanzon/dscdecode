/* SPDX-License-Identifier: BSD-2-Clause-Patent
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

/* --- Block prediction, DSC 1.1 §6.4.2 and §6.4.4.1 ------------------------
 * research/bp-worked-note.md derives the pattern values used here. At QP 0
 * an MPP group reconstructs midpoint + residual, which writes exact samples.
 */
static const uint16_t pattern_abc[3][3] = {{80,416,176},{120,256,416},{80,96,176}};
static const uint16_t pattern_def[3][3] = {{180,416,336},{180,96,336},{140,256,96}};

/* Write one group of exact samples with MPP at QP 0. */
static void put(struct dsc_predict *p, unsigned x, unsigned y, const uint16_t px[3][3],
                uint16_t pixels[3][3])
{
    static const unsigned q[3] = {0,0,0};
    static const int all_mpp[3] = {1,1,1};
    static const unsigned none[3] = {0,0,0};
    int residual[3][3];
    unsigned c, j;
    for (c=0; c<3; ++c) for (j=0; j<3; ++j) residual[c][j] = px[j][c] - (c ? 256 : 128);
    assert(!dsc_predict_group(p,x,y,q,(const int (*)[3])residual,all_mpp,0,none,pixels));
}

/* Decode a zero-residual, non-MPP group and return its pixels. */
static void zero(struct dsc_predict *p, unsigned x, unsigned y, uint16_t pixels[3][3])
{
    static const unsigned q[3] = {0,0,0}, none[3] = {0,0,0};
    static const int no_mpp[3] = {0,0,0}, residual[3][3] = {{0}};
    assert(!dsc_predict_group(p,x,y,q,residual,no_mpp,0,none,pixels));
}

static int is(uint16_t pixels[3][3], unsigned j, unsigned y, unsigned co, unsigned cg)
{
    return pixels[0][j] == y && pixels[1][j] == co && pixels[2][j] == cg;
}

/* Line 0: A B C repeated. On line 1 vector -3 has SAD 0 from hPos 9, so
 * bpCount is 1, 2, 3 at hPos 9, 12, 15. Every sample pair is an edge. */
static struct dsc_predict *abc_line(unsigned width, int bp, const struct dsc_options *o)
{
    struct dsc_predict *p = dsc_predict_create(width, 2, 9, bp, 0);
    uint16_t pixels[3][3];
    unsigned x;
    assert(p);
    if (o) dsc_predict_set_options(p, o);
    for (x = 0; x < width; x += 3) put(p, x, 0, pattern_abc, pixels);
    return p;
}

static void bp_selection(void)
{
    uint16_t pixels[3][3];
    unsigned x;
    struct dsc_predict *p = abc_line(21, 1, NULL);
    for (x = 0; x < 12; x += 3) put(p, x, 1, pattern_def, pixels);
    /* hPos 12: bpCount is 2, so MMAP. a = F, b = A, c = C:
     * P0 = CLAMP(a+b-c, MIN(a,b), MAX(a,b)) = (140, 416, 96).
     * P1 adds d = B: Y 140+120-80 = 180 -> 140; Co 256+256-96 = 416; Cg
     * 96+416-176 = 336. P2 adds e = C: (140, 256, 96). */
    zero(p, 12, 1, pixels);
    assert(is(pixels,0,140,416,96) && is(pixels,1,140,416,336) && is(pixels,2,140,256,96));
    dsc_predict_destroy(p);

    /* Same, with hPos 12 written as D E F. hPos 15: bpCount 3, BP with
     * vector -3 copies pixels 12-14, so the output is D E F. MMAP would give
     * Y 140 for pixel 15 (a = F.Y 140, b = A.Y 80, c = C.Y 80). */
    p = abc_line(21, 1, NULL);
    for (x = 0; x < 15; x += 3) put(p, x, 1, pattern_def, pixels);
    zero(p, 15, 1, pixels);
    assert(is(pixels,0,180,416,336) && is(pixels,1,180,96,336) && is(pixels,2,140,256,96));
    zero(p, 18, 1, pixels);                       /* copies 15-17: D E F again */
    assert(is(pixels,0,180,416,336) && is(pixels,2,140,256,96));
    dsc_predict_destroy(p);

    /* block_pred_enable 0: never BP, so hPos 15 is MMAP, Y 140. */
    p = abc_line(21, 0, NULL);
    for (x = 0; x < 15; x += 3) put(p, x, 1, pattern_def, pixels);
    zero(p, 15, 1, pixels);
    assert(is(pixels,0,140,416,96));
    dsc_predict_destroy(p);

    /* Width 17: the group at hPos 15 has two pixels, a partial group, so
     * MMAP although bpCount is 3. */
    p = abc_line(17, 1, NULL);
    for (x = 0; x < 15; x += 3) put(p, x, 1, pattern_def, pixels);
    zero(p, 15, 1, pixels);
    assert(is(pixels,0,140,416,96));
    dsc_predict_destroy(p);
}

/* No edges: gray 100, 110, 120 repeated steps by at most 20. Vector -3
 * still has SAD 0 and bpCount reaches 3, but lastEdgeCount never drops
 * below 3, so hPos 15 is MMAP: a = 70, b = 100, c = 120 gives
 * CLAMP(50, 70, 100) = 70. BP would copy 50. */
static void bp_edge_gate(void)
{
    static const uint16_t gray[3][3] = {{100,256,256},{110,256,256},{120,256,256}};
    static const uint16_t dark[3][3] = {{50,256,256},{60,256,256},{70,256,256}};
    uint16_t pixels[3][3];
    unsigned x;
    struct dsc_predict *p = dsc_predict_create(21, 2, 9, 1, 0);
    assert(p);
    for (x = 0; x < 21; x += 3) put(p, x, 0, gray, pixels);
    for (x = 0; x < 15; x += 3) put(p, x, 1, dark, pixels);
    zero(p, 15, 1, pixels);
    assert(pixels[0][0] == 70);
    dsc_predict_destroy(p);
}

/* OQ-4. Gray line 0: 142, 128, 148, then 128 at x 3-11, then 188, 68, 188
 * repeated. Search at hPos 9 (window x 3-11, all 128):
 *   vector -1 compares x 3 with x 2: |128-148| >> 1 = 10, bpSad 10 >> 3 = 1.
 *   vector -10 compares x 3-9 with seven samples left of the slice and
 *   x 10-11 with x 0-1: 7 x |128-O| + |128-142| >> 1 = 7.
 * Midpoint (O = 128): -10 scores 7 >> 3 = 0, lowest, so bpCount becomes 1.
 * Replicate (O = x 0 = 142): -10 scores (49+7) >> 3 = 7; -1 wins with 1 and
 * resets bpCount. At hPos 12 and 15 a vector other than -1 wins under both
 * (-10 or -3 at 12, -3 at 15), and an edge lies in x 15-17. So BP starts at
 * hPos 15 with midpoint and at hPos 18 with replicate. */
static void bp_left_boundary(int reading, int bp_at_15)
{
    static const uint16_t line1[3][3] = {{50,256,256},{90,256,256},{130,256,256}};
    static const unsigned luma[21] = {142,128,148,128,128,128,128,128,128,128,128,128,
                                      188,68,188,188,68,188,188,68,188};
    uint16_t pixels[3][3], group[3][3];
    struct dsc_options o;
    struct dsc_stats st = {0};
    unsigned x, j, c;
    struct dsc_predict *p = dsc_predict_create(21, 2, 9, 1, 0);
    assert(p);
    dsc_options_init(&o);
    o.bp_left = reading;
    o.stats = &st;
    dsc_predict_set_options(p, &o);
    for (x = 0; x < 21; x += 3) {
        for (j = 0; j < 3; ++j) for (c = 0; c < 3; ++c) group[j][c] = (uint16_t)(c ? 256 : luma[x+j]);
        put(p, x, 0, (const uint16_t (*)[3])group, pixels);
    }
    for (x = 0; x < 15; x += 3) put(p, x, 1, line1, pixels);
    /* BP copies 50; MMAP gives CLAMP(130+188-188, 130, 188) = 130. */
    zero(p, 15, 1, pixels);
    assert(pixels[0][0] == (bp_at_15 ? 50 : 130));
    /* Decisions differ at hPos 9 (count 0 vs 1) and 12 (1 vs 2) without a
     * BP difference; the selection itself differs at 15. */
    assert(st.bp_left_differs == 1 && st.bp_groups == (unsigned long)bp_at_15);
    dsc_predict_destroy(p);
}

int main(void)
{
    line_storage(8);
    line_storage(9);
    history_boundaries();
    bp_selection();
    bp_edge_gate();
    bp_left_boundary(DSC_BP_LEFT_REPLICATE, 0);
    bp_left_boundary(DSC_BP_LEFT_MIDPOINT, 1);
    puts("Prediction sample traces passed (line precision, history boundaries, block prediction)");
    return 0;
}
