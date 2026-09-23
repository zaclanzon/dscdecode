/* SPDX-License-Identifier: MIT
 * Independently expressed DSC 1.1 sections 6.3--6.5 and 7.4--7.6.
 * Pixels in this module are unsigned internal Y, Co, Cg (8, 9, 9 bits).
 */
#include "predict.h"
#include <stdlib.h>
#include <string.h>

struct dsc_predict {
    unsigned width, height, depth, next_x, next_y;
    int multiple;
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
        line_depth < 8 || line_depth > 13 || block_prediction) return NULL;
    p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->previous = calloc((size_t)width * 3, sizeof(uint16_t));
    p->current = calloc((size_t)width * 3, sizeof(uint16_t));
    if (!p->previous || !p->current) { dsc_predict_destroy(p); return NULL; }
    p->width = width; p->height = height; p->depth = line_depth;
    p->multiple = multiple_slices;
    return p;
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
    if (!p || !qlevel || !residual || !mpp || !index || !out ||
        x!=p->next_x || y!=p->next_y || x>=p->width || y>=p->height) return -1;
    for (c=0; c<3; ++c) {
        if (qlevel[c] > (c ? 8U : 7U)) return -1;
        for (j=0; j<3; ++j) if (residual[c][j]<-512 || residual[c][j]>511) return -1;
    }
    n = p->width-x < 3 ? p->width-x : 3;
    capacity = y ? 25 : 32;
    if (ich) {
        for (j=0; j<3; ++j) {
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
    if (!ich) for (c=0; c<3; ++c) {
        int a = x ? p->current[(x-1)*3+c] : midpoint(c);
        int b = y ? blended(p,(int)x,c,qlevel[c]) : 0;
        int before = x ? blended(p,(int)x-1,c,qlevel[c]) : midpoint(c);
        int lo=a, hi=a, cumulative=0;
        for (j=0; j<n; ++j) {
            int pred, r=residual[c][j]*(1 << qlevel[c]);
            if (mpp[c]) pred=midpoint(c)+(p->last[c]&((1 << qlevel[c])-1));
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
