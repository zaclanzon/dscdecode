/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Original implementation of DSC 1.1 sections 6.8 and 7.3, with the DSC 1.2
 * changes of DSC 1.2b §6.8.2, §6.8.4 and §6.8.5.2 (dsc_version_minor 2).
 * Prose-only timing interpretation is recorded in research/rc-ambiguities.md.
 * Where the text supports two readings, both are implemented and selected by
 * struct dsc_options (RESEARCH.md, open questions OQ-1 to OQ-3, OQ-5, OQ-11,
 * OQ-12, OQ-14 to OQ-16).
 */

#include "rate_control.h"
#include <string.h>

static int64_t floor_div(int64_t value, int64_t divisor)
{
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

static unsigned umax(unsigned a, unsigned b)
{
    return a > b ? a : b;
}

static int fail(struct dsc_rc *s)
{
    s->failed = 1;
    return -1;
}

int dsc_rc_init(struct dsc_rc *s, const struct drm_dsc_config *c)
{
    unsigned i;

    if (!s || !c) {
        return -1;
    }
    memset(s, 0, sizeof(*s));
    s->cfg = c;
    dsc_options_init(&s->opt);
    if (dsc_qp_scale(c->bits_per_component, &s->max_qp, &s->flat_type_qp, &s->very_flat_qp)) {
        return fail(s);
    }
    s->v12 = c->dsc_version_minor == 2;
    if (s->v12) {
        struct dsc_format f;

        if (dsc_format_init(&f, c)) {
            return fail(s);
        }
        /* DSC 1.2b §6.8.4: cpntBitDepth[0] + cpntBitDepth[1] - 2. */
        s->bit_save_thresh = f.depth[0] + f.depth[1] - 2;
    }
    if (!c->slice_width || !c->slice_height || !c->bits_per_pixel || c->bits_per_pixel > 384 ||
        !c->rc_model_size || c->initial_offset >= c->rc_model_size ||
        c->final_offset >= c->rc_model_size || c->initial_scale_value < 8 ||
        (c->initial_scale_value > 8 && !c->scale_decrement_interval) || !c->slice_chunk_size ||
        c->vbr_enable) {
        return fail(s);
    }
    for (i = 0; i < 15; ++i) {
        if (c->rc_range_params[i].range_min_qp > c->rc_range_params[i].range_max_qp ||
            c->rc_range_params[i].range_max_qp > s->max_qp) {
            return fail(s);
        }
    }
    for (i = 0; i < 14; ++i) {
        if (c->rc_buf_thresh[i] > 255 || (i && c->rc_buf_thresh[i] <= c->rc_buf_thresh[i - 1])) {
            return fail(s);
        }
    }
    s->offset_q11 = ((int64_t)c->initial_offset - c->rc_model_size) * 2048;
    s->scale = c->initial_scale_value;
    return 0;
}

void dsc_rc_set_options(struct dsc_rc *s, const struct dsc_options *o)
{
    if (s && o) {
        s->opt = *o;
    }
}

unsigned dsc_rc_qp(const struct dsc_rc *s)
{
    return s->qp;
}

unsigned dsc_rc_primary_qp(const struct dsc_rc *s)
{
    return s->qp;
}

/* Figure 6-13's permission test. OQ-5: the figure prints the rc_quant_incr_limit0
 * branch for curQp < prev2Qp; the swapped reading takes it for curQp > prev2Qp. */
static int permit_increment(const struct drm_dsc_config *c, unsigned cur, unsigned prev2, int edge,
                            int swapped)
{
    if (cur == prev2) {
        return edge;
    }
    if (swapped ? cur > prev2 : cur < prev2) {
        return edge && cur < c->rc_quant_incr_limit0;
    }
    return cur < c->rc_quant_incr_limit1;
}

/* Figure 6-12 and Figure 6-13 for one set of inputs. prev and prev2 are the
 * prevQp and prev2Qp of §6.8.4. */
/* The increment branch shared by Figure 6-12 (DSC 1.1) and Figure 6-18
 * (DSC 1.2b): MIN(maxQp, curQp + incrAmount) when permitted, else curQp. */
static unsigned increment_qp(const struct drm_dsc_config *c, const struct dsc_rc_inputs *in,
                             unsigned min_qp, unsigned max_qp, unsigned prev, unsigned prev2,
                             const struct dsc_options *o)
{
    unsigned cur = umax(min_qp, prev), edge, permit;
    int64_t increment;

    edge = in->ideal * 2 < in->previous_ideal * c->rc_edge_factor;
    permit = (unsigned)permit_increment(c, cur, prev2, (int)edge,
                                        o->incr_order == DSC_INCR_ORDER_SWAPPED);
    if (o->stats && (int)permit != permit_increment(c, cur, prev2, (int)edge,
                                                    o->incr_order != DSC_INCR_ORDER_SWAPPED)) {
        ++o->stats->incr_order_differs;
    }
    increment = floor_div((int64_t)in->actual - in->target, 2);
    if (!permit) {
        return cur;
    }
    return increment + cur > max_qp ? max_qp : (unsigned)(increment + cur);
}

/* DSC 1.2b Figure 6-17: the branches in order, then stQp clamped to the
 * (possibly adjusted) minQp and maxQp. */
static unsigned short_term_v12(const struct drm_dsc_config *c, const struct dsc_rc_inputs *in,
                               unsigned prev, unsigned prev2, const struct dsc_options *o)
{
    int64_t low = in->target - c->rc_tgt_offset_low;
    int64_t high = in->target + c->rc_tgt_offset_high, st;
    unsigned min_qp = in->min_qp, max_qp = in->max_qp;
    unsigned adjusted_max = in->max_qp + 1 < in->top_qp ? in->max_qp + 1 : in->top_qp;
    /* OQ-26: lowMinQp from maxQp as printed, or from minQp. */
    unsigned from = o->low_min == DSC_LOW_MIN_MIN_QP ? in->min_qp : in->max_qp;
    unsigned low_min = from > 4 ? from - 4 : 0;
    /* OQ-27: rcSizeGroup never exceeds codedGroupSize, so the size reading
     * is the test on rcSizeGroup alone. */
    int decrement = o->decrement_test == DSC_DECREMENT_SIZE
                        ? (int64_t)in->ideal < low
                        : (int64_t)in->actual < low && (int64_t)in->ideal < low;

    if (in->model > -172) {
        st = max_qp = c->rc_range_params[14].range_max_qp;
    } else if (in->fullness < 192) {
        st = min_qp;
    } else if (in->bit_save == 2) {
        /* OQ-29: prevQp + 1 as printed, or + 2. */
        st = (int64_t)prev + (o->bitsave_step == DSC_BITSAVE_STEP_2 ? 2 : 1);
        max_qp = adjusted_max;
    } else if (in->bit_save == 1) {
        st = prev;
        max_qp = adjusted_max;
    } else if (in->zero) {
        st = (int64_t)prev - 1;
        min_qp = low_min;
    } else if (decrement) {
        st = (int64_t)prev - 1;
    } else if ((int64_t)in->actual > high && in->fullness >= 64) {
        st = increment_qp(c, in, min_qp, max_qp, prev, prev2, o);
    } else {
        st = prev;
    }
    return st < min_qp ? min_qp : st > max_qp ? max_qp : (unsigned)st;
}

static unsigned short_term(const struct drm_dsc_config *c, const struct dsc_rc_inputs *in,
                           unsigned prev, unsigned prev2, const struct dsc_options *o)
{
    int64_t low = in->target - c->rc_tgt_offset_low;
    int64_t high = in->target + c->rc_tgt_offset_high, increment;
    unsigned cur, edge, permit, next = prev;

    if (in->v12) {
        return short_term_v12(c, in, prev, prev2, o);
    }
    if (in->model > -172) {
        return c->rc_range_params[14].range_max_qp;
    }
    if (in->ideal == 3) {
        return umax(prev ? prev - 1 : 0, in->min_qp / 2);
    }
    if ((int64_t)in->actual < low && (int64_t)in->ideal < low) {
        return umax(prev ? prev - 1 : 0, in->min_qp);
    }
    if ((int64_t)in->actual > high && in->fullness >= 64) {
        cur = umax(in->min_qp, prev);
        edge = in->ideal * 2 < in->previous_ideal * c->rc_edge_factor;
        permit = (unsigned)permit_increment(c, cur, prev2, (int)edge,
                                            o->incr_order == DSC_INCR_ORDER_SWAPPED);
        if (o->stats && (int)permit != permit_increment(c, cur, prev2, (int)edge,
                                                        o->incr_order != DSC_INCR_ORDER_SWAPPED)) {
            ++o->stats->incr_order_differs;
        }
        next = cur;
        increment = floor_div((int64_t)in->actual - in->target, 2);
        if (permit) {
            next = increment + cur > in->max_qp ? in->max_qp : (unsigned)(increment + cur);
        }
    }
    return next;
}

static void bit_save_update(struct dsc_rc *s, unsigned y, const struct dsc_rc_group *g,
                            unsigned prev, unsigned prev2);

int dsc_rc_apply_flat(struct dsc_rc *s, int flat, int very_flat)
{
    return dsc_rc_apply_flat_line(s, flat, very_flat, 0);
}

int dsc_rc_apply_flat_line(struct dsc_rc *s, int flat, int very_flat, int line_start)
{
    unsigned q, master, redo, top, below, prev2, current;
    int demote, always_very = 0;

    if (!s || s->failed) {
        return -1;
    }
    s->flat_override = 0;
    q = s->qp;
    /* DSC 1.2b §6.8.5.2: the first group of every line after the first is
     * adjusted as very flat, never signaled. OQ-25: whether the note that
     * demotes very flat below somewhatFlatQpThresh applies to it. */
    if (s->v12 && line_start) {
        flat = very_flat = 1;
        always_very = s->opt.line_flat == DSC_LINE_FLAT_VERY;
    }
    if (!flat) {
        return 0;
    }
    /* OQ-18: §6.8.5.2 skips the override when the current QP is range
     * 14's maximum, the same wording as the OQ-16 note below. */
    top = s->cfg->rc_range_params[14].range_max_qp;
    if (s->opt.stats && (q == top) != (s->used_qp == top)) {
        ++s->opt.stats->flat_max_qp_differs;
    }
    current = s->opt.flat_max_qp == DSC_FLAT_MAX_QP_PREVIOUS ? s->used_qp : q;
    /* DSC 1.2b §6.8.5.2 adjusts a line start only while the QP is below
     * range 14's maximum; DSC 1.2's bitSaveMode can raise the QP above it.
     * OQ-34: for signaled flatness the text skips only at that QP. */
    if (s->v12 && (line_start || s->opt.flat_top == DSC_FLAT_TOP_AT_OR_ABOVE)
            ? current >= top
            : current == top) {
        return 0;
    }
    /* OQ-16: the type bit is only sent when the signaling group's QP is at
     * least somewhatFlatQpThresh, 7 + 2 * (bpc - 8) (§4.5). §6.8.5.2 demotes
     * very flat to somewhat flat when the current QP is below it without
     * saying which group's QP that is. At apply time used_qp is the QP that
     * decoded the previous group. */
    below = s->flat_type_qp;
    if (s->opt.stats && very_flat && (q < below || s->used_qp < below)) {
        ++s->opt.stats->very_flat_low_qp;
    }
    demote = always_very                                     ? 0
             : s->opt.very_flat == DSC_VERY_FLAT_GROUP_QP      ? q < below
             : s->opt.very_flat == DSC_VERY_FLAT_PREVIOUS_QP ? s->used_qp < below
                                                             : 0;
    if (s->v12 && line_start && s->opt.stats) {
        ++s->opt.stats->line_flat;
    }
    /* Somewhat flat: MAX(stQp - 4, 0). Very flat: 1 + 2 * (bpc - 8). */
    master = (!very_flat || demote) ? (q > 4 ? q - 4 : 0) : s->very_flat_qp;
    /* Section 6.8.5.2 restarts short-term RC only when the override
     * actually changes masterQp. OQ-31: in DSC 1.2 the model re-runs it at
     * every adjusted group. */
    if (master == q && !(s->v12 && s->opt.flat_rerun == DSC_FLAT_RERUN_EVERY)) {
        return 0;
    }
    s->qp = master;
    s->flat_override = master != q;
    /* OQ-1: the cycle already run for the next group, re-run from the
     * overridden QP. prev2Qp is the QP that decoded the previous group; DSC
     * 1.2b §6.8.4 adjusts it for flatness like the current group. */
    prev2 = s->used_qp;
    if (s->v12) {
        prev2 = (!very_flat || demote) ? (prev2 > 4 ? prev2 - 4 : 0) : s->very_flat_qp;
        /* OQ-32: compute that step's bitSaveMode again with these QPs. */
        if (s->opt.rerun_bitsave == DSC_RERUN_BITSAVE_REDO && s->have_step_group && s->have_inputs) {
            s->bit_save = s->step_bit_save;
            s->mpp_state = s->step_mpp_state;
            bit_save_update(s, s->step_y, &s->step_group, master, prev2);
            s->last_inputs.bit_save = s->bit_save;
        }
    }
    redo = s->have_inputs ? short_term(s->cfg, &s->last_inputs, master, prev2, &s->opt)
                          : s->pending_qp;
    if (s->opt.stats) {
        s->opt.stats->flat_overrides += (unsigned long)s->flat_override;
        if (redo != s->pending_qp) {
            ++s->opt.stats->flat_queue_differs;
        }
    }
    if (s->opt.flat_restart == DSC_FLAT_RESTART_IN_FLIGHT) {
        if (redo > s->max_qp) {
            return fail(s);
        }
        s->pending_qp = redo;
        s->penultimate_qp = master;
        s->last_qp = redo;
    } else {
        /* Next-cycle reading: the queued QP stands; the cycle after this
         * group starts from the override. */
        s->last_qp = master;
    }
    return 0;
}

/* One pixel time of §6.8.1 bit removal under one OQ-3 reading. Returns the
 * bits removed, including chunk padding, or -1 for impossible padding. */
static int64_t drain_pixel(const struct drm_dsc_config *c, uint64_t pixel, int literal,
                           uint32_t *fraction, uint32_t *chunk_bits, uint32_t *chunk_pixels)
{
    int64_t removed, padding;

    if (pixel < c->initial_xmit_delay) {
        return 0;
    }
    *fraction += c->bits_per_pixel;
    removed = *fraction / 16;
    *fraction %= 16;
    *chunk_bits += (uint32_t)removed;
    /* Literal reading: the printed per-pixel reset condition. */
    if (literal && (pixel - c->initial_xmit_delay) % c->slice_width == 0) {
        *fraction = 0;
    }
    if (++*chunk_pixels == c->slice_width) {
        /* The chunk-completion reading bounds padding by 8 (§6.8.1). The
         * literal reading can drop one fractional bit from the first chunk. */
        padding = (int64_t)c->slice_chunk_size * 8 - *chunk_bits;
        if (padding < 0 || padding > (literal ? 9 : 8)) {
            return -1;
        }
        removed += padding;
        *chunk_bits = *chunk_pixels = 0;
        if (!literal) {
            *fraction = 0;
        }
    }
    return removed;
}

int dsc_rc_step(struct dsc_rc *s, unsigned y, unsigned groupnum, unsigned n, unsigned actual,
                unsigned ideal)
{
    struct dsc_rc_group g;

    memset(&g, 0, sizeof(g));
    g.actual = actual;
    g.ideal = ideal;
    g.zero = ideal == 3;
    return dsc_rc_step_group(s, y, groupnum, n, &g);
}

/* DSC 1.2b §6.8.4: bitSaveMode and mppState after the group just decoded.
 * OQ-22: DSC 1.2a prints ichSelected where DSC 1.2b has !ichSelected. */
static void bit_save_update(struct dsc_rc *s, unsigned y, const struct dsc_rc_group *g,
                            unsigned prev, unsigned prev2)
{
    /* OQ-28: prevQp as printed, or prev2Qp. */
    unsigned activity = (s->opt.activity_qp == DSC_ACTIVITY_PREV2 ? prev2 : prev) +
                        g->predicted[0] + umax(g->predicted[1], g->predicted[2]);
    int p_mode = s->opt.bitsave_ich == DSC_BITSAVE_ICH_SET ? g->ich : !g->ich;

    if (!y || g->flat) {
        s->bit_save = s->mpp_state = 0;
    } else if (p_mode && g->mpp >= 3) {
        s->mpp_state = s->mpp_state + 1 < 2 ? s->mpp_state + 1 : 2;
        if (s->mpp_state >= 2) {
            s->bit_save = 2;
        }
    } else if (p_mode && activity >= s->bit_save_thresh) {
        /* bitSaveMode is kept */
    } else if (g->ich) {
        s->bit_save = umax(1, s->bit_save);
    } else {
        s->bit_save = s->mpp_state = 0;
    }
}

int dsc_rc_step_group(struct dsc_rc *s, unsigned y, unsigned groupnum, unsigned n,
                      const struct dsc_rc_group *g)
{
    unsigned actual = g->actual, ideal = g->ideal;
    const struct drm_dsc_config *c;
    struct dsc_rc_inputs in;
    int64_t offset, transformed, delta, target, xform_bpg;
    uint64_t delayed, delay_end, group_end, reached, counted;
    unsigned range = 0, next, i, k;
    int signed_bpg, literal;

    if (!s || s->failed) {
        return -1;
    }
    c = s->cfg;
    if (n < 1 || n > 3 || actual > 256 || ideal > 256 || groupnum != s->groups ||
        y != s->pixels / c->slice_width ||
        s->pixels + n > (uint64_t)c->slice_width * c->slice_height ||
        s->pixels % c->slice_width + n > c->slice_width) {
        return fail(s);
    }

    s->fullness += actual;
    if (s->fullness >
        (int64_t)(c->initial_xmit_delay + c->initial_dec_delay) * c->bits_per_pixel / 16) {
        return fail(s);
    }

    /* Account actual pixels, excluding residual padding in partial groups.
     * The alternative OQ-3 reading runs alongside for the statistics. */
    literal = s->opt.frac_reset == DSC_FRAC_RESET_LITERAL;
    for (i = 0; i < n; ++i) {
        int64_t removed, other;

        ++s->pixels;
        removed = drain_pixel(c, s->pixels, literal, &s->fractional_bits, &s->chunk_bits,
                              &s->chunk_pixels);
        if (removed < 0) {
            return fail(s);
        }
        s->fullness -= removed;
        s->removed += removed;
        other = drain_pixel(c, s->pixels, !literal, &s->shadow.fractional_bits,
                            &s->shadow.chunk_bits, &s->shadow.chunk_pixels);
        if (other < 0) {
            s->shadow.failed = 1;
        } else {
            s->shadow.removed += other;
        }
    }
    if (s->fullness < 0) {
        return fail(s);
    }
    if (s->opt.stats && (s->shadow.failed || s->shadow.removed != s->removed)) {
        ++s->opt.stats->frac_differs;
    }

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
    } else if ((s->groups || s->opt.scale_dec == DSC_SCALE_DEC_FROM_GROUP_0) && s->scale > 8) {
        /* OQ-14: whether the first decrement interval counts group 0. */
        if (++s->scale_clock >= c->scale_decrement_interval) {
            s->scale_clock = 0;
            --s->scale;
        }
    }
    /* Q11 retains fractional precision across every group. OQ-12: §6.8.1
     * starts removing bits at pixelCount == initial_xmit_delay; whether that
     * pixel is still "during the initial delay" for §6.8.2 is not stated. */
    delay_end = c->initial_xmit_delay;
    if (s->opt.delay_offset == DSC_DELAY_OFFSET_EXCLUSIVE && delay_end) {
        --delay_end;
    }
    delayed = s->pixels - n;
    delayed = delayed < delay_end ? delay_end - delayed : 0;
    if (delayed > n) {
        delayed = n;
    }
    /* OQ-19: §6.8.2 lowers the offset by bits_per_pixel * 3 per group during
     * the initial delay; §6.8.1 counts the pixels actually in a group. The
     * group-end reading counts to each group's end as if every group had
     * three pixels, from the group's real position: a partial group at the
     * end of a line counts three pixels, and the next line's first group
     * only the rest. The total over the delay is the same. */
    group_end = s->pixels - n + 3;
    reached = group_end < delay_end ? group_end : delay_end;
    counted = s->delay_group_end < delay_end ? s->delay_group_end : delay_end;
    counted = reached > counted ? reached - counted : 0;
    if (s->opt.stats && counted != delayed) {
        ++s->opt.stats->delay_partial_differs;
    }
    if (s->opt.delay_partial == DSC_DELAY_PARTIAL_GROUP_END) {
        delayed = counted;
    }
    s->delay_group_end = group_end;
    delta = c->slice_bpg_offset +
            (y ? c->nfl_bpg_offset : -(int64_t)c->first_line_bpg_offset * 2048);
    delta -= (int64_t)delayed * c->bits_per_pixel * 128;
    if (s->v12) {
        /* DSC 1.2b §6.8.2 adjustments 5 and 6, and second_line_offset_adj
         * subtracted once the first line is done. All three are zero
         * unless native 4:2:0 is used. */
        delta += y == 1 ? -(int64_t)c->second_line_bpg_offset * 2048 : c->nsl_bpg_offset;
        if (y == 1 && s->pixels - n == c->slice_width) {
            delta -= (int64_t)c->second_line_offset_adj * 2048;
        }
    }
    s->offset_q11 += delta;
    if (s->offset_q11 < ((int64_t)c->final_offset - c->rc_model_size) * 2048) {
        s->clamp_offset = 1;
    }
    if (s->clamp_offset && s->offset_q11 > ((int64_t)c->final_offset - c->rc_model_size) * 2048) {
        s->offset_q11 = ((int64_t)c->final_offset - c->rc_model_size) * 2048;
    }
    offset = floor_div(s->offset_q11, 2048);
    if (!s->increasing && !s->increase_next && c->scale_increment_interval && y &&
        s->pixels >= c->initial_xmit_delay && offset > -(int64_t)c->rc_model_size) {
        s->increase_next = 1;
    }

    /* Max 2^32 pixels, scale <= 2^32, offset magnitude below 2^48:
     * bound explicitly before the multiplication, without signed overflow. */
    if (s->scale > 65535 || offset < -INT64_C(1000000000000) || offset > INT64_C(1000000000000)) {
        return fail(s);
    }
    transformed = floor_div((s->fullness + offset) * s->scale, 8);
    if (transformed > 0) {
        return fail(s);
    }
    /* OQ-2: Figure 6-11 does not say which range owns a threshold value. */
    if (s->opt.threshold_eq == DSC_THRESHOLD_EQ_UPPER) {
        while (range < 14 &&
               transformed >= (int64_t)c->rc_buf_thresh[range] * 64 - c->rc_model_size) {
            ++range;
        }
    } else {
        while (range < 14 &&
               transformed > (int64_t)c->rc_buf_thresh[range] * 64 - c->rc_model_size) {
            ++range;
        }
    }
    if (s->opt.stats) {
        for (k = 0; k < 14; ++k) {
            if (transformed == (int64_t)c->rc_buf_thresh[k] * 64 - c->rc_model_size) {
                ++s->opt.stats->threshold_equal;
                break;
            }
        }
    }
    /* OQ-11: Figure 6-8 runs the long-term RC (range selection) one group
     * behind. In the range-lag reading the short-term RC uses the range from
     * the previous step. Before any group there is no previous step; the
     * reference model behaves as if that range were range 0 (PROGRESS.md). */
    if (s->opt.rc_pipeline == DSC_RC_PIPELINE_RANGE_LAG) {
        unsigned current = range;

        range = s->have_lag ? s->lag_range : 0;
        if (s->opt.stats && range != current) {
            ++s->opt.stats->range_lag_differs;
        }
        s->lag_range = current;
        s->have_lag = 1;
    }
    s->range = range;
    signed_bpg = c->rc_range_params[range].range_bpg_offset & 63;
    if (signed_bpg & 32) {
        signed_bpg -= 64;
    }
    /* OQ-15: §6.8.4 prints three samples; §6.8.1 removes bits for the pixels
     * actually present. The pixels reading uses the latter for partial groups. */
    if (s->opt.stats && n < 3) {
        ++s->opt.stats->partial_groups;
    }
    xform_bpg = y ? -(int64_t)(c->nfl_bpg_offset / 2048) : c->first_line_bpg_offset;
    if (s->v12) {
        /* DSC 1.2b §6.8.4. OQ-7: DSC 1.2b adds the second-line terms to
         * rcXformBpgOffset ("+="); DSC 1.2a replaces it with them ("="). */
        int64_t second = y == 1 ? c->second_line_bpg_offset : -(int64_t)(c->nsl_bpg_offset / 2048);

        xform_bpg = s->opt.bpg_combine == DSC_BPG_COMBINE_ADD ? xform_bpg + second : second;
    }
    target = ((int64_t)(s->opt.partial_target == DSC_PARTIAL_TARGET_PIXELS ? n : 3) *
                  c->bits_per_pixel + 8) / 16 +
             signed_bpg + xform_bpg - c->slice_bpg_offset / 2048;
    /* OQ-30: a negative target (a short group at a line end with a large
     * negative range offset) is used as computed, or raised to 0. */
    if (s->v12 && s->opt.target_floor == DSC_TARGET_FLOOR_ZERO && target < 0) {
        target = 0;
    }
    in.fullness = s->fullness;
    in.model = s->fullness + offset;
    in.target = target;
    in.actual = actual;
    in.ideal = ideal;
    in.previous_ideal = s->previous_ideal;
    in.min_qp = c->rc_range_params[range].range_min_qp;
    in.max_qp = c->rc_range_params[range].range_max_qp;
    in.v12 = s->v12;
    in.zero = g->zero;
    in.top_qp = 2u * c->bits_per_component - 1;
    if (s->v12) {
        s->step_group = *g;
        s->step_y = y;
        s->step_bit_save = s->bit_save;
        s->step_mpp_state = s->mpp_state;
        s->have_step_group = 1;
        /* prevQp is the QP generated last; prev2Qp decoded this group. */
        bit_save_update(s, y, g, s->last_qp, s->qp);
        if (s->bit_save && s->opt.stats) {
            ++s->opt.stats->bit_save_groups;
        }
    }
    in.bit_save = s->bit_save;
    next = short_term(c, &in, s->last_qp, s->penultimate_qp, &s->opt);
    if (next > s->max_qp) {
        return fail(s);
    }
    s->last_inputs = in;
    s->have_inputs = 1;
    s->used_qp = s->qp;
    s->penultimate_qp = s->last_qp;
    s->last_qp = next;
    s->previous_ideal = ideal;
    s->qp = s->pending_qp;
    s->pending_qp = next;
    ++s->groups;
    if (s->opt.stats) {
        ++s->opt.stats->groups;
    }
    return 0;
}
