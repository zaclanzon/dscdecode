/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Independently expressed DSC 1.1 sections 6.3--6.5 and 7.4--7.6.
 * Pixels in this module are unsigned internal Y, Co, Cg (8, 9, 9 bits).
 * Block prediction: DSC 1.1 §6.4.2, §6.4.4.1 and §7.5.2.1, with the bpSad
 * correction printed in DSC 1.2b §6.4.4.1. Open points are OQ-4, OQ-10 and
 * OQ-13 in RESEARCH.md; research/bp-worked-note.md works examples by hand.
 */
#include "predict.h"
#include <stdlib.h>
#include <string.h>

struct dsc_predict {
    unsigned width, height, depth, next_x, next_y;
    int multiple, block_prediction;
    struct dsc_options opt;
    unsigned bp_count, bp_count_other;   /* bpCount under opt.bp_left and the other reading */
    uint16_t *previous, *current;
    uint16_t history[32][3], last[3];
    unsigned valid;
};
static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int minimum(int a, int b) { return a < b ? a : b; }
static int maximum(int a, int b) { return a > b ? a : b; }
static int midpoint(unsigned c) { return c ? 256 : 128; }
static int upper(unsigned c) { return c ? 511 : 255; }
static int above(const struct dsc_predict *p, int x, unsigned c)
{
    x = clamp(x, 0, (int)p->width - 1);
    return p->previous[(unsigned)x * 3 + c];
}
static int blended(const struct dsc_predict *p, int x, unsigned c, unsigned q)
{
    int raw = above(p, x, c);
    int smooth = (above(p, x-1, c) + 2*raw + above(p, x+1, c) + 2)/4;
    int limit = (1 << q)/2;
    return raw + clamp(smooth-raw, -limit, limit);
}
struct dsc_predict *dsc_predict_create(unsigned width, unsigned height,
    unsigned line_depth, int block_prediction, int multiple_slices)
{
    struct dsc_predict *p;
    if (!width || width > 65535 || !height || height > 65535 ||
        line_depth < 8 || line_depth > 13) return NULL;
    p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->previous = calloc((size_t)width * 3, sizeof(uint16_t));
    p->current = calloc((size_t)width * 3, sizeof(uint16_t));
    if (!p->previous || !p->current) { dsc_predict_destroy(p); return NULL; }
    p->width = width; p->height = height; p->depth = line_depth;
    p->multiple = multiple_slices;
    p->block_prediction = block_prediction;
    dsc_options_init(&p->opt);
    return p;
}
void dsc_predict_set_options(struct dsc_predict *p, const struct dsc_options *o)
{
    if (p && o) p->opt = *o;
}

/* Previous-line sample for the BP search and edge test. The text does not
 * define samples left of the slice (OQ-4). Right of it, as for the MMAP
 * filter (§6.4.1), the last sample repeats; only partial groups reach it. */
static int bp_sample(const struct dsc_predict *p, int x, unsigned c, int left)
{
    if (x < 0) return left == DSC_BP_LEFT_MIDPOINT ? midpoint(c) : p->previous[c];
    return above(p, x, c);
}

/* §6.4.4.1: the best of the nine candidate vectors for the group at hPos x.
 * Each 9-pixel SAD is three 3x1 partial SADs, summed over all components,
 * of differences reduced to 6 bits and each partial clamped to 511. */
static int bp_search(const struct dsc_predict *p, int x, int left)
{
    static const int candidates[9] = {-1, -3, -4, -5, -6, -7, -8, -9, -10};
    unsigned k, b, j, c, best = 0;
    int vector = -1;
    for (k = 0; k < 9; ++k) {
        unsigned total = 0, sad;
        for (b = 0; b < 3; ++b) {
            unsigned partial = 0;
            for (j = 0; j < 3; ++j) for (c = 0; c < 3; ++c) {
                int pos = x - 6 + (int)(3*b + j);
                int d = bp_sample(p, pos, c, left) - bp_sample(p, pos + candidates[k], c, left);
                unsigned m = (unsigned)(d < 0 ? -d : d) >> (c ? 2 : 1); /* bitDepth - 7 */
                partial += m > 63 ? 63 : m;
            }
            total += partial > 511 ? 511 : partial;
        }
        /* OQ-10: 1.1's prose and DSC 1.2b drop three LSBs; 1.1's formula clips. */
        sad = p->opt.bp_sad == DSC_BP_SAD_CLIP ? (total > 511 ? 511 : total) : total >> 3;
        /* Strictly lower only: a tie keeps the smaller-magnitude vector. */
        if (!k || sad < best) { best = sad; vector = candidates[k]; }
    }
    return vector;
}

/* lastEdgeCount < 3: an edge, a step above 32 in any component, at one of
 * the three previous-line samples ending at last (OQ-13 chooses last). */
static int bp_recent_edge(const struct dsc_predict *p, int last, int left)
{
    int q;
    unsigned c;
    for (q = last - 2; q <= last; ++q)
        for (c = 0; c < 3; ++c) {
            int d = bp_sample(p, q, c, left) - bp_sample(p, q - 1, c, left);
            if (d > 32 || d < -32) return 1;
        }
    return 0;
}

/* §6.4.4.1 selection for one group under one OQ-4 reading. bpCount starts
 * each line at 0 and increments only from hPos 9, so BP can first be
 * selected at hPos 15. Partial groups never use BP. */
static int bp_decide(const struct dsc_predict *p, unsigned x, unsigned n, int left,
                     unsigned *count, int *vector)
{
    int last = p->opt.bp_edge == DSC_BP_EDGE_BEFORE ? (int)x - 1 : (int)x + 2;
    *vector = bp_search(p, (int)x, left);
    if (*vector == -1) *count = 0;
    else if (x >= 9 && *count < 3) ++*count;
    return *vector != -1 && *count >= 3 && n == 3 && bp_recent_edge(p, last, left);
}
void dsc_predict_destroy(struct dsc_predict *p)
{
    if (p) { free(p->previous); free(p->current); free(p); }
}

static void history_update(struct dsc_predict *p, int ich,
    const unsigned index[3], uint16_t out[3][3], unsigned capacity)
{
    uint16_t next[32][3];
    unsigned n=0, i, j, c;
    for (i=3; i>0; ) {
        int duplicate = 0;
        --i;
        if (ich) for (j=i+1; j<3; ++j) if (index[i]==index[j]) duplicate=1;
        if (!duplicate) {
            for (c=0; c<3; ++c) next[n][c]=out[c][i];
            ++n;
        }
    }
    for (i=0; i<p->valid && n<capacity; ++i) {
        int selected=0;
        if (ich) for (j=0; j<3; ++j) if (index[j]==i) selected=1;
        if (!selected) { memcpy(next[n],p->history[i],sizeof(next[n])); ++n; }
    }
    memcpy(p->history,next,n*sizeof(next[0])); p->valid=n;
}

int dsc_predict_group(struct dsc_predict *p, unsigned x, unsigned y,
    const unsigned qlevel[3], const int residual[3][3], const int mpp[3],
    int ich, const unsigned index[3], uint16_t out[3][3])
{
    unsigned c,j,n,capacity;
    int use_bp = 0, vector = 0;
    if (!p || !qlevel || !residual || !mpp || !index || !out ||
        x!=p->next_x || y!=p->next_y || x>=p->width || y>=p->height) return -1;
    for (c=0; c<3; ++c) {
        if (qlevel[c] > (c ? 8U : 7U)) return -1;
        for (j=0; j<3; ++j) if (residual[c][j]<-512 || residual[c][j]>511) return -1;
    }
    n = p->width-x < 3 ? p->width-x : 3;
    capacity = y ? 25 : 32;
    /* BP needs the previous line, so never on a slice's first line. The
     * search runs for every group, whatever the group's coding mode. */
    if (p->block_prediction && y) {
        if (!x) p->bp_count = p->bp_count_other = 0;
        use_bp = bp_decide(p, x, n, p->opt.bp_left, &p->bp_count, &vector);
        if (p->opt.stats) {
            /* The other OQ-4 reading, run alongside only for the counters. */
            int other = p->opt.bp_left == DSC_BP_LEFT_MIDPOINT ? DSC_BP_LEFT_REPLICATE
                                                               : DSC_BP_LEFT_MIDPOINT;
            int other_vector, other_bp = bp_decide(p, x, n, other, &p->bp_count_other,
                                                   &other_vector);
            if (use_bp && !ich) ++p->opt.stats->bp_groups;
            if (use_bp != other_bp || (use_bp && vector != other_vector))
                ++p->opt.stats->bp_left_differs;
        }
    }
    if (ich) {
        /* Only real pixels are looked up; a partial group's padding index
         * produces no pixel and, as a last group, updates no history. */
        for (j=0; j<n; ++j) {
            if (index[j]>=32) return -1;
            if (index[j]<capacity) {
                if (index[j]>=p->valid) return -1;
                for (c=0; c<3; ++c) out[c][j]=p->history[index[j]][c];
            } else {
                int base;
                /* Seven-neighbor semantics have no definition for width < 7. */
                if (p->width<7) return -1;
                base=clamp((int)x-2,0,(int)p->width-7);
                for (c=0; c<3; ++c) out[c][j]=(uint16_t)above(p,base+(int)index[j]-25,c);
            }
        }
    }
    /* Checked, not assumed: the reference must be left of this group. */
    if (use_bp && !ich && x < (unsigned)-vector) return -1;
    if (!ich) for (c=0; c<3; ++c) {
        int a = x ? p->current[(x-1)*3+c] : midpoint(c);
        int b = y ? blended(p,(int)x,c,qlevel[c]) : 0;
        int before = x ? blended(p,(int)x-1,c,qlevel[c]) : midpoint(c);
        int lo=a, hi=a, cumulative=0;
        for (j=0; j<n; ++j) {
            int pred, r=residual[c][j]*(1 << qlevel[c]);
            if (mpp[c]) pred=midpoint(c)+(p->last[c]&((1 << qlevel[c])-1));
            /* §6.4.2: the reconstructed sample |bpVector| to the left on this
             * line; bpVector <= -3 and hPos >= 15 keep it inside the slice. */
            else if (use_bp) pred=p->current[(x+j-(unsigned)-vector)*3+c];
            else if (!y) pred=clamp(a+cumulative,0,upper(c));
            else {
                int reference=j ? blended(p,(int)(x+j),c,qlevel[c]) : b;
                lo=minimum(lo,reference); hi=maximum(hi,reference);
                pred=clamp(a+reference-before+cumulative,lo,hi);
            }
            out[c][j]=(uint16_t)clamp(pred+r,0,upper(c));
            cumulative+=r;
        }
    }
    for (c=0; c<3; ++c) {
        for (j=0; j<n; ++j) p->current[(x+j)*3+c]=out[c][j];
        p->last[c]=out[c][n-1];
    }
    /* Section 6.5.2 excludes every last group, including a full final group. */
    if (x+n<p->width) history_update(p,ich,index,out,capacity);
    p->next_x=x+n;
    if (p->next_x==p->width) {
        uint16_t *swap;
        for (j=0; j<p->width; ++j) for (c=0; c<3; ++c) {
            unsigned bits=c ? 9 : 8;
            unsigned shift=bits>p->depth ? bits-p->depth : 0;
            int rounded=(p->current[j*3+c]+(shift ? (1 << (shift-1)) : 0)) >> shift;
            p->current[j*3+c]=(uint16_t)(minimum(rounded,(1 << p->depth)-1) << shift);
        }
        swap=p->previous; p->previous=p->current; p->current=swap;
        p->next_x=0; ++p->next_y;
        if (p->multiple) p->valid=0;
        else if (p->valid>25) p->valid=25;
    }
    return 0;
}
