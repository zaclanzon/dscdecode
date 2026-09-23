/* SPDX-License-Identifier: MIT
 * Original implementation of DSC 1.1 sections 6.8 and 7.3.
 * Prose-only timing interpretation is recorded in research/rc-ambiguities.md.
 */
#include "rate_control.h"
#include <string.h>

static int64_t floor_div(int64_t value, int64_t divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}
static unsigned umax(unsigned a, unsigned b) { return a > b ? a : b; }
static int fail(struct dsc_rc *s) { s->failed = 1; return -1; }

int dsc_rc_init(struct dsc_rc *s, const struct drm_dsc_config *c)
{
    unsigned i;
    if (!s || !c) return -1;
    memset(s, 0, sizeof(*s));
    s->cfg = c;
    if (!c->slice_width || !c->slice_height || !c->bits_per_pixel ||
        c->bits_per_pixel > 384 || c->bits_per_component != 8 ||
        !c->rc_model_size || c->initial_offset >= c->rc_model_size ||
        c->final_offset >= c->rc_model_size || c->initial_scale_value < 8 ||
        (c->initial_scale_value > 8 && !c->scale_decrement_interval) ||
        !c->slice_chunk_size || c->vbr_enable)
        return fail(s);
    for (i = 0; i < 15; ++i)
        if (c->rc_range_params[i].range_min_qp > c->rc_range_params[i].range_max_qp ||
            c->rc_range_params[i].range_max_qp > 15) return fail(s);
    for (i = 0; i < 14; ++i)
        if (c->rc_buf_thresh[i] > 255 ||
            (i && c->rc_buf_thresh[i] <= c->rc_buf_thresh[i-1])) return fail(s);
    s->offset_q11 = ((int64_t)c->initial_offset - c->rc_model_size) * 2048;
    s->scale = c->initial_scale_value;
    return 0;
}

unsigned dsc_rc_qp(const struct dsc_rc *s) { return s->qp; }
unsigned dsc_rc_primary_qp(const struct dsc_rc *s) { return s->qp; }

int dsc_rc_apply_flat(struct dsc_rc *s, int flat, int very_flat)
{
    unsigned q;
    if (!s || s->failed) return -1;
    q = s->qp;
    if (!flat || q == s->cfg->rc_range_params[14].range_max_qp) return 0;
    s->qp = (!very_flat || q < 7) ? (q > 4 ? q - 4 : 0) : 1;
    /* Section 6.8.5.2 requires the override as the next RC starting point. */
    s->last_qp = s->qp;
    return 0;
}

int dsc_rc_step(struct dsc_rc *s, unsigned y, unsigned groupnum,
                unsigned n, unsigned actual, unsigned ideal)
{
    const struct drm_dsc_config *c;
    int64_t offset, transformed, delta, target, low, high, increment;
    uint64_t delayed;
    unsigned range = 0, next, lo, hi, i, edge, cur, permit;
    int signed_bpg;
    if (!s || s->failed) return -1;
    c = s->cfg;
    if (n < 1 || n > 3 || actual > 256 || ideal > 256 ||
        groupnum != s->groups || y != s->pixels / c->slice_width ||
        s->pixels + n > (uint64_t)c->slice_width * c->slice_height ||
        s->pixels % c->slice_width + n > c->slice_width)
        return fail(s);

    s->fullness += actual;
    if (s->fullness > (int64_t)(c->initial_xmit_delay + c->initial_dec_delay) *
                      c->bits_per_pixel / 16) return fail(s);

    /* Account actual pixels, excluding residual padding in partial groups. */
    for (i = 0; i < n; ++i) {
        unsigned removed;
        ++s->pixels;
        if (s->pixels < c->initial_xmit_delay) continue;
        s->fractional_bits += c->bits_per_pixel;
        removed = s->fractional_bits / 16;
        s->fractional_bits %= 16;
        s->fullness -= removed;
        s->chunk_bits += removed;
        if (++s->chunk_pixels == c->slice_width) {
            int64_t padding = (int64_t)c->slice_chunk_size * 8 - s->chunk_bits;
            if (padding < 0 || padding > 8) return fail(s);
            s->fullness -= padding;
            s->fractional_bits = s->chunk_bits = s->chunk_pixels = 0;
        }
    }
    if (s->fullness < 0) return fail(s);

    if (s->increase_next) {
        s->scale = 9;
        s->scale_clock = 0;
        s->increasing = 1;
        s->increase_next = 0;
    } else if (s->increasing) {
        if (++s->scale_clock >= c->scale_increment_interval) {
            s->scale_clock = 0;
            ++s->scale;
        }
    } else if (s->groups && s->scale > 8) {
        if (++s->scale_clock >= c->scale_decrement_interval) {
            s->scale_clock = 0;
            --s->scale;
        }
    }
    /* Q11 retains fractional precision across every group. */
    delayed = s->pixels - n;
    delayed = delayed < c->initial_xmit_delay ? c->initial_xmit_delay - delayed : 0;
    if (delayed > n) delayed = n;
    delta = c->slice_bpg_offset + (y ? c->nfl_bpg_offset :
                                              -(int64_t)c->first_line_bpg_offset * 2048);
    delta -= (int64_t)delayed * c->bits_per_pixel * 128;
    s->offset_q11 += delta;
    if (s->offset_q11 < ((int64_t)c->final_offset - c->rc_model_size) * 2048)
        s->clamp_offset = 1;
    if (s->clamp_offset && s->offset_q11 >
                      ((int64_t)c->final_offset - c->rc_model_size) * 2048)
        s->offset_q11 = ((int64_t)c->final_offset - c->rc_model_size) * 2048;
    offset = floor_div(s->offset_q11, 2048);
    if (!s->increasing && !s->increase_next && c->scale_increment_interval &&
        y && s->pixels >= c->initial_xmit_delay && offset > -(int64_t)c->rc_model_size)
        s->increase_next = 1;

    /* Max 2^32 pixels, scale <= 2^32, offset magnitude below 2^48:
     * bound explicitly before the multiplication, without signed overflow. */
    if (s->scale > 65535 || offset < -INT64_C(1000000000000) ||
        offset > INT64_C(1000000000000)) return fail(s);
    transformed = floor_div((s->fullness + offset) * s->scale, 8);
    if (transformed > 0) return fail(s);
    while (range < 14 && transformed >
           (int64_t)c->rc_buf_thresh[range] * 64 - c->rc_model_size) ++range;
    lo = c->rc_range_params[range].range_min_qp;
    hi = c->rc_range_params[range].range_max_qp;
    signed_bpg = c->rc_range_params[range].range_bpg_offset & 63;
    if (signed_bpg & 32) signed_bpg -= 64;
    /* Section 6.8.4 specifies three samples even for a partial group. */
    target = ((int64_t)3 * c->bits_per_pixel + 8) / 16 + signed_bpg +
             (y ? -(int64_t)(c->nfl_bpg_offset / 2048) : c->first_line_bpg_offset) -
             c->slice_bpg_offset / 2048;
    low = target - c->rc_tgt_offset_low;
    high = target + c->rc_tgt_offset_high;
    next = s->last_qp;
    if (s->fullness + offset > -172) next = c->rc_range_params[14].range_max_qp;
    else if (ideal == 3) next = umax(next ? next - 1 : 0, lo / 2);
    else if ((int64_t)actual < low && (int64_t)ideal < low)
        next = umax(next ? next - 1 : 0, lo);
    else if ((int64_t)actual > high && s->fullness >= 64) {
        cur = umax(lo, next);
        edge = ideal * 2 < s->previous_ideal * c->rc_edge_factor;
        /* Follow Figure 6-13's printed '<' direction exactly. */
        permit = cur == s->penultimate_qp ? edge :
                 cur < s->penultimate_qp ? edge && cur < c->rc_quant_incr_limit0 :
                                          cur < c->rc_quant_incr_limit1;
        next = cur;
        increment = floor_div((int64_t)actual - target, 2);
        if (permit) next = increment + cur > hi ? hi : (unsigned)(increment + cur);
    }
    if (next > 15) return fail(s);
    s->penultimate_qp = s->last_qp;
    s->last_qp = next;
    s->previous_ideal = ideal;
    s->qp = s->pending_qp;
    s->pending_qp = next;
    ++s->groups;
    return 0;
}
