/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * DSC decoder: substream demultiplexing, entropy decoding, and slice and
 * picture assembly. Section numbers are DSC 1.1 unless marked otherwise;
 * clause references and unresolved ambiguities are documented in
 * RESEARCH.md. This file contains no reference-model code.
 */

#include "dsc.h"
#include "format.h"
#include "predict.h"
#include "rate_control.h"
#include <stdlib.h>
#include <string.h>

/* One substream's funnel shifter. It holds at most max_se - 1 bits plus a
 * mux word, below 160 for every supported format. */
struct reservoir {
    uint8_t bits[160];
    unsigned count, read;
};

struct syntax_state {
    struct reservoir s[4];
    unsigned predicted[4], last_level[4];
    int was_ich, flat_flag, flat_type, sg_flag;
    int prev_flag; /* flat_flag before this group read it (OQ-24) */
    size_t flat_group;
};

/* One group as parsed from the substreams. The predicted sizes are the
 * candidates for DSC 1.2's predictedSize outputs (OQ-23): the value the
 * group was coded with, its qLevel-adjusted form, and the value computed
 * from the group's residuals. */
struct group_syntax {
    unsigned level[4], idx[3], actual, ideal, mpp_units;
    unsigned pred_raw[4], pred_adjusted[4], pred_next[4];
    int res[4][3], mpp[4], ich, zero;
};

static unsigned bound(int n, unsigned hi)
{
    return n < 0 ? 0 : (unsigned)n > hi ? hi : (unsigned)n;
}

static int validate(const struct drm_dsc_config *c, struct dsc_format *f)
{
    unsigned i;
    int status;

    if (!c) {
        return DSC_INVALID;
    }
    status = dsc_format_init(f, c);
    if (status) {
        return status;
    }
    if (c->vbr_enable) {
        return DSC_UNSUPPORTED;
    }
    if (!c->slice_width || !c->slice_height || !c->pic_width || !c->pic_height ||
        !c->slice_chunk_size || !c->bits_per_pixel || c->bits_per_pixel > 384 ||
        c->line_buf_depth < 8 || c->line_buf_depth > (f->version == 2 ? 16 : 13) ||
        !c->rc_model_size ||
        c->initial_scale_value < 8 || c->initial_scale_value > 63 ||
        (c->initial_scale_value > 8 && !c->scale_decrement_interval) ||
        c->initial_offset > c->rc_model_size || c->final_offset > c->rc_model_size ||
        c->flatness_min_qp > c->flatness_max_qp || c->flatness_max_qp > f->max_qp ||
        c->rc_quant_incr_limit0 > f->max_qp || c->rc_quant_incr_limit1 > f->max_qp ||
        c->rc_edge_factor > 15 || c->rc_tgt_offset_high > 15 || c->rc_tgt_offset_low > 15) {
        return DSC_INVALID;
    }
    if ((size_t)c->slice_width * c->slice_height > DSC_MAX_PIXELS ||
        (size_t)c->pic_width * c->pic_height > DSC_MAX_PIXELS) {
        return DSC_LIMIT;
    }
    if (c->slice_chunk_size != ((unsigned)c->slice_width * c->bits_per_pixel + 127) / 128) {
        return DSC_INVALID;
    }
    for (i = 0; i < 14; i++) {
        if (c->rc_buf_thresh[i] > 255 || (i && c->rc_buf_thresh[i] <= c->rc_buf_thresh[i - 1]) ||
            c->rc_buf_thresh[i] * 64u >= c->rc_model_size) {
            return DSC_INVALID;
        }
    }
    for (i = 0; i < 15; i++) {
        if (c->rc_range_params[i].range_min_qp > c->rc_range_params[i].range_max_qp ||
            c->rc_range_params[i].range_max_qp > f->max_qp ||
            c->rc_range_params[i].range_bpg_offset > 63) {
            return DSC_INVALID;
        }
    }
    return DSC_OK;
}

/* Table 4-6: before each group, one mux word for each substream whose
 * funnel shifter holds fewer than its maximum syntax element size. */
static int refill(struct reservoir *r, unsigned max_se, unsigned word, const uint8_t *p, size_t n,
                  size_t *pos)
{
    unsigned i, left = r->count - r->read;

    if (left >= max_se) {
        return 0;
    }
    if (*pos > n || n - *pos < word / 8) {
        return -1;
    }
    memmove(r->bits, r->bits + r->read, left);
    for (i = 0; i < word; i++) {
        r->bits[left + i] = (p[*pos + i / 8] >> (7 - i % 8)) & 1;
    }
    *pos += word / 8;
    r->read = 0;
    r->count = left + word;
    return 0;
}

static int take(struct reservoir *r, unsigned n, unsigned *v)
{
    unsigned k, x = 0;

    if (n > 16 || r->count - r->read < n) {
        return -1;
    }
    for (k = 0; k < n; k++) {
        x = x * 2 + r->bits[r->read++];
    }
    *v = x;
    return 0;
}

static unsigned signed_size(int n)
{
    unsigned w;

    if (!n) {
        return 0;
    }
    for (w = 1; w <= 16; w++) {
        if (n >= -(1 << (w - 1)) && n < (1 << (w - 1))) {
            return w;
        }
    }
    return 17;
}

/* §4.5, §6.6 and §7.2 for one group: flatness bits, the luma prefix with the
 * ICH escape, then each unit's prefix and residuals or its ICH index. */
static int syntax(struct syntax_state *s, const struct dsc_format *f,
                  const struct drm_dsc_config *c, const struct dsc_options *opt, unsigned group,
                  unsigned qp, struct group_syntax *g)
{
    unsigned start = 0, u, k, v, z, max, pred, width, required[3], largest;
    /* DSC 1.2b Table 4-10: at 16 bpc and QP 0 the luma prefix has at most
     * 15 (OQ-21: or 13) bits, all zeros meaning MPP; ICH is not available and
     * the prefix does not depend on the previous group's mode. */
    int limited = f->version == 2 && f->bpc == 16 && qp == 0;

    if (qp > f->max_qp) {
        return -1;
    }
    for (u = 0; u < f->units; u++) {
        start += s->s[u].read;
    }
    s->prev_flag = s->flat_flag;
    /* A supergroup is four groups from group 1 on; its flag was read two
     * groups before it starts (§6.6.3). */
    if (group % 4 == 1) {
        s->sg_flag = s->flat_flag;
    }
    if (group % 4 == 3) {
        s->flat_flag = 0;
        if (qp >= c->flatness_min_qp && qp <= c->flatness_max_qp) {
            if (take(&s->s[0], 1, &v)) {
                return -1;
            }
            s->flat_flag = (int)v;
        }
    } else if (group % 4 == 0 && s->flat_flag) {
        s->flat_type = 0;
        /* §4.5: the type is sent at or above somewhatFlatQpThresh. */
        if (qp >= f->flat_type_qp) {
            if (take(&s->s[0], 1, &v)) {
                return -1;
            }
            s->flat_type = (int)v;
        }
        if (take(&s->s[0], 2, &v)) {
            return -1;
        }
        /* 6.6.3: supergroup starts one group after metadata. */
        s->flat_group = (size_t)group + 1 + v;
    }
    g->ich = 0;
    g->ideal = 0;
    g->mpp_units = 0;
    g->zero = 1;
    for (u = 0; u < f->units; u++) {
        g->level[u] = dsc_qlevel(f, qp, u);
        max = f->depth[u] - g->level[u];
        pred = bound((int)s->predicted[u] + (int)s->last_level[u] - (int)g->level[u], max - 1);
        g->pred_raw[u] = s->predicted[u];
        g->pred_adjusted[u] = pred;
        if (u == 0 && limited) {
            unsigned most = opt->prefix16 == DSC_PREFIX16_13 ? 13 : 15;

            for (z = 0; z < most; z++) {
                if (take(&s->s[u], 1, &v)) {
                    return -1;
                }
                if (v) {
                    break;
                }
            }
            width = z == most ? max : pred + z;
            if (width > max) {
                return -1;
            }
        } else if (u == 0 || !g->ich) {
            unsigned limit = max - pred + (u == 0);

            for (z = 0; z < limit; z++) {
                if (take(&s->s[u], 1, &v)) {
                    return -1;
                }
                if (v) {
                    break;
                }
            }
            width = pred + z;
            if (u == 0) {
                if (s->was_ich) {
                    if (z == 0) {
                        g->ich = 1;
                    } else {
                        width--;
                    }
                } else if (width == max + 1) {
                    g->ich = 1;
                }
            }
            if (!g->ich && width > max) {
                return -1;
            }
        } else {
            width = 0;
        }
        if (g->ich) {
            if (take(&s->s[u], 5, &g->idx[u])) {
                return -1;
            }
            g->mpp[u] = 0;
        } else {
            g->mpp[u] = width == max;
            g->mpp_units += (unsigned)g->mpp[u];
            largest = 0;
            for (k = 0; k < 3; k++) {
                if (take(&s->s[u], width, &v)) {
                    return -1;
                }
                g->res[u][k] = (int)v;
                if (width && (v & (1u << (width - 1)))) {
                    g->res[u][k] -= 1 << width;
                }
                required[k] = g->mpp[u] ? max : signed_size(g->res[u][k]);
                if (required[k] > largest) {
                    largest = required[k];
                }
            }
            s->predicted[u] = (required[0] + required[1] + 2 * required[2] + 2) / 4;
            g->ideal += 3 * largest + 1;
            g->zero &= !largest;
        }
        g->pred_next[u] = s->predicted[u];
        s->last_level[u] = g->level[u];
    }
    if (g->ich) {
        g->ideal = 16;
        g->zero = 0;
    }
    s->was_ich = g->ich;
    g->actual = 0;
    for (u = 0; u < f->units; u++) {
        g->actual += s->s[u].read;
    }
    g->actual -= start;
    return 0;
}

/* Section 6.6: encoders clear a partial group's padding residuals and repeat
 * its rightmost real ICH index. The padding still feeds size prediction.
 * OQ-17: whether a decoder rejects other padding. The reference model's
 * decoder does not, so by default the padding is not checked; the reject
 * reading enforces §6.6. Returns nonzero when the group is rejected. */
static int check_padding(const struct dsc_format *f, const struct dsc_options *opt,
                         const struct group_syntax *g, unsigned count)
{
    unsigned k, u;

    for (k = count; k < 3; k++) {
        int noncanonical = 0;

        if (g->ich) {
            noncanonical = g->idx[k] != g->idx[count - 1];
        } else {
            for (u = 0; u < f->units; u++) {
                if (g->res[u][k]) {
                    noncanonical = 1;
                }
            }
        }
        if (noncanonical) {
            if (opt->stats) {
                ++opt->stats->padding_nonzero;
            }
            return opt->partial_padding == DSC_PARTIAL_PADDING_REJECT;
        }
    }
    return 0;
}

/* Decodes one slice into out: f->units samples per pixel, raster order. */
static int decode_slice(const struct drm_dsc_config *c, const struct dsc_format *f,
                        const struct dsc_options *opt, unsigned slice, const uint8_t *p,
                        size_t n, uint16_t *out)
{
    struct syntax_state s;
    struct group_syntax g;
    struct dsc_rc_group report;
    struct dsc_rc rc;
    struct dsc_predict *pred;
    struct dsc_group_trace tr;
    size_t pos = 0, expected;
    unsigned x, y, gn = 0, u, k, qp, width = c->slice_width;
    int status;
    uint16_t pixel[4][3];

    expected = (size_t)c->slice_chunk_size * c->slice_height;
    if (n != expected) {
        return n < expected ? DSC_TRUNCATED : DSC_INVALID;
    }
    if (dsc_rc_init(&rc, c)) {
        return DSC_INVALID;
    }
    dsc_rc_set_options(&rc, opt);
    pred = dsc_predict_create_format(f, width, c->slice_height, c->line_buf_depth,
                                     c->block_pred_enable, c->pic_width != c->slice_width);
    if (!pred) {
        return DSC_NOMEM;
    }
    dsc_predict_set_options(pred, opt);
    memset(&s, 0, sizeof(s));
    s.flat_group = (size_t)-1;
    for (y = 0; y < c->slice_height; y++) {
        for (x = 0; x < width; x += 3, gn++) {
            unsigned count = width - x < 3 ? width - x : 3;

            for (u = 0; u < f->units; u++) {
                if (refill(&s.s[u], f->max_se[u], f->mux_word, p, n, &pos)) {
                    status = DSC_TRUNCATED;
                    goto done;
                }
            }
            if (dsc_rc_apply_flat_line(&rc, s.flat_group == gn, s.flat_type, !x && y)) {
                status = DSC_RATE_CONTROL;
                goto done;
            }
            memset(&g, 0, sizeof(g));
            qp = dsc_rc_qp(&rc);
            if (syntax(&s, f, c, opt, gn, qp, &g)) {
                status = DSC_BITSTREAM;
                goto done;
            }
            if (check_padding(f, opt, &g, count)) {
                status = DSC_BITSTREAM;
                goto done;
            }
            if (dsc_predict_group_units(pred, x, y, g.level, (const int (*)[3])g.res, g.mpp, g.ich,
                                        g.idx, pixel)) {
                status = DSC_BITSTREAM;
                goto done;
            }
            for (k = 0; k < count; k++) {
                for (u = 0; u < f->units; u++) {
                    out[((size_t)y * width + x + k) * f->units + u] = pixel[u][k];
                }
            }
            memset(&report, 0, sizeof(report));
            report.actual = g.actual;
            report.ideal = g.ideal;
            report.mpp = g.mpp_units;
            report.ich = g.ich;
            report.zero = g.zero;
            /* OQ-24: which groups a flag of 1 covers (RESEARCH.md): its
             * supergroup; the signaled group; the flag received last, read
             * in this group or before it; the groups carrying the flag and
             * the type and position; from the flag's group to the end of
             * its supergroup; the flag as it was before this group.
             * OQ-23: which predicted sizes. */
            switch (opt->bitsave_flat) {
            case DSC_BITSAVE_FLAT_GROUP:
                report.flat = s.flat_group == gn;
                break;
            case DSC_BITSAVE_FLAT_RECEIVED:
                report.flat = s.flat_flag;
                break;
            case DSC_BITSAVE_FLAT_CARRIER:
                report.flat = s.flat_flag && (gn % 4 == 3 || gn % 4 == 0);
                break;
            case DSC_BITSAVE_FLAT_SPAN:
                report.flat = s.flat_flag || s.sg_flag;
                break;
            case DSC_BITSAVE_FLAT_LAGGED:
                report.flat = s.prev_flag;
                break;
            default:
                report.flat = s.sg_flag;
                break;
            }
            for (u = 0; u < f->units; u++) {
                report.predicted[u] = opt->bitsave_pred == DSC_BITSAVE_PRED_ADJUSTED ? g.pred_adjusted[u]
                                      : opt->bitsave_pred == DSC_BITSAVE_PRED_NEXT   ? g.pred_next[u]
                                                                                     : g.pred_raw[u];
            }
            if (dsc_rc_step_group(&rc, y, gn, count, &report)) {
                status = DSC_RATE_CONTROL;
                goto done;
            }
            if (opt->trace) {
                tr.slice = slice;
                tr.group = gn;
                tr.x = x;
                tr.y = y;
                tr.qp = qp;
                tr.actual = g.actual;
                tr.ideal = g.ideal;
                tr.range = rc.range;
                tr.generated_qp = rc.last_qp;
                tr.buffer_fullness = rc.fullness;
                tr.model_fullness = rc.last_inputs.model;
                tr.ich = g.ich;
                tr.flat_override = rc.flat_override;
                opt->trace(opt->trace_context, &tr);
            }
        }
    }
    /* Residual funnel and CBR slice-tail padding must be zero (6.7.4). */
    for (u = 0; u < f->units; u++) {
        for (k = s.s[u].read; k < s.s[u].count; k++) {
            if (s.s[u].bits[k]) {
                status = DSC_BITSTREAM;
                goto done;
            }
        }
    }
    for (; pos < n; pos++) {
        if (p[pos]) {
            status = DSC_BITSTREAM;
            goto done;
        }
    }
    status = DSC_OK;
done:
    dsc_predict_destroy(pred);
    return status;
}

/* Where decoded pixels go: RGB888, or 16-bit planes. */
struct sink {
    uint8_t *rgb;
    const struct dsc_planes *planes;
    unsigned width;
};

static int half_floor(int v)
{
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

/* §7.7: YCoCg-R to RGB, each result clamped to the component range. At
 * 16 bpc the chroma was rounded to 16 bits; DSC 1.2b §7.7 restores the
 * scale with (C - 0x8000) << 1. */
static void put_pixel(const struct sink *k, const struct dsc_format *f, unsigned x, unsigned y,
                      const uint16_t *s)
{
    int co = f->bpc == 16 ? ((int)s[1] - 0x8000) * 2 : (int)s[1] - (1 << f->bpc);
    int cg = f->bpc == 16 ? ((int)s[2] - 0x8000) * 2 : (int)s[2] - (1 << f->bpc);
    int t = (int)s[0] - half_floor(cg), b = t - half_floor(co);
    unsigned top = (1u << f->bpc) - 1, rgb[3], c;

    rgb[0] = bound(co + b, top);
    rgb[1] = bound(cg + t, top);
    rgb[2] = bound(b, top);
    if (k->rgb) {
        for (c = 0; c < 3; c++) {
            k->rgb[((size_t)y * k->width + x) * 3 + c] = (uint8_t)rgb[c];
        }
    } else {
        for (c = 0; c < 3; c++) {
            k->planes->plane[c][(size_t)y * k->planes->stride[c] + x] = (uint16_t)rgb[c];
        }
    }
}

/* Writes the visible part of a decoded slice at (x0, y0) of the output. */
static void put_slice(const struct sink *k, const struct dsc_format *f, const uint16_t *decoded,
                      unsigned slice_width, unsigned slice_height, unsigned x0, unsigned y0,
                      unsigned width, unsigned height)
{
    unsigned x, y;

    for (y = 0; y < slice_height && y0 + y < height; y++) {
        for (x = 0; x < slice_width && x0 + x < width; x++) {
            put_pixel(k, f, x0 + x, y0 + y, decoded + ((size_t)y * slice_width + x) * f->units);
        }
    }
}

int dsc_plane_size(const struct drm_dsc_config *c, int slice, unsigned width[3],
                   unsigned height[3])
{
    struct dsc_format f;
    unsigned i;
    int status;

    if (!c || !width || !height) {
        return DSC_INVALID;
    }
    status = dsc_format_init(&f, c);
    if (status) {
        return status;
    }
    for (i = 0; i < 3; i++) {
        width[i] = slice ? c->slice_width : c->pic_width;
        height[i] = slice ? c->slice_height : c->pic_height;
    }
    return DSC_OK;
}

static int check_planes(const struct drm_dsc_config *c, int slice, const struct dsc_planes *o)
{
    unsigned w[3], h[3], i;
    int status = dsc_plane_size(c, slice, w, h);

    if (status) {
        return status;
    }
    if (!o) {
        return DSC_INVALID;
    }
    for (i = 0; i < 3; i++) {
        if (!o->plane[i] || o->stride[i] < w[i] ||
            o->capacity[i] < o->stride[i] * (h[i] - 1) + w[i]) {
            return DSC_LIMIT;
        }
    }
    return DSC_OK;
}

static int decode_single(const struct drm_dsc_config *c, const struct dsc_options *opt,
                         const uint8_t *p, size_t n, const struct sink *k)
{
    struct dsc_options defaults;
    struct dsc_format f;
    uint16_t *decoded;
    int status = validate(c, &f);

    if (status) {
        return status;
    }
    if (!p) {
        return DSC_INVALID;
    }
    if (!opt) {
        dsc_options_init(&defaults);
        opt = &defaults;
    }
    dsc_format_apply_options(&f, opt);
    decoded = malloc((size_t)c->slice_width * c->slice_height * f.units * sizeof(*decoded));
    if (!decoded) {
        return DSC_NOMEM;
    }
    status = decode_slice(c, &f, opt, 0, p, n, decoded);
    if (!status) {
        put_slice(k, &f, decoded, c->slice_width, c->slice_height, 0, 0, c->slice_width,
                  c->slice_height);
    }
    free(decoded);
    return status;
}

static int decode_picture(const struct drm_dsc_config *c, const struct dsc_options *opt,
                          const uint8_t *data, size_t n, const struct sink *k)
{
    struct dsc_options defaults;
    struct dsc_format f;
    unsigned nx, ny, sx, sy, y;
    size_t bytes, expected;
    uint8_t *slice;
    uint16_t *decoded;
    int status = validate(c, &f);

    if (status) {
        return status;
    }
    if (!opt) {
        dsc_options_init(&defaults);
        opt = &defaults;
    }
    dsc_format_apply_options(&f, opt);
    if (!data) {
        return DSC_INVALID;
    }
    nx = ((unsigned)c->pic_width + c->slice_width - 1) / c->slice_width;
    ny = ((unsigned)c->pic_height + c->slice_height - 1) / c->slice_height;
    bytes = (size_t)c->slice_chunk_size * c->slice_height;
    if (nx > 255 || bytes > SIZE_MAX / nx / ny) {
        return DSC_LIMIT;
    }
    expected = bytes * nx * ny;
    if (n != expected) {
        return n < expected ? DSC_TRUNCATED : DSC_INVALID;
    }
    slice = malloc(bytes);
    decoded = malloc((size_t)c->slice_width * c->slice_height * f.units * sizeof(*decoded));
    if (!slice || !decoded) {
        free(slice);
        free(decoded);
        return DSC_NOMEM;
    }
    for (sy = 0; sy < ny; sy++) {
        for (sx = 0; sx < nx; sx++) {
            for (y = 0; y < c->slice_height; y++) {
                size_t source =
                    ((size_t)sy * c->slice_height * nx + (size_t)y * nx + sx) * c->slice_chunk_size;

                memcpy(slice + (size_t)y * c->slice_chunk_size, data + source, c->slice_chunk_size);
            }
            status = decode_slice(c, &f, opt, sy * nx + sx, slice, bytes, decoded);
            if (status) {
                goto done;
            }
            put_slice(k, &f, decoded, c->slice_width, c->slice_height, sx * c->slice_width,
                      sy * c->slice_height, c->pic_width, c->pic_height);
        }
    }
done:
    free(slice);
    free(decoded);
    return status;
}

/* RGB888 output exists for 8 bpc RGB pictures only; the planes API
 * serves every format. */
static int rgb888_sink(const struct drm_dsc_config *c, int slice, uint8_t *rgb, size_t cap,
                       struct sink *k)
{
    struct dsc_format f;
    int status = validate(c, &f);

    if (status) {
        return status;
    }
    if (f.bpc != 8 || !f.rgb) {
        return DSC_UNSUPPORTED;
    }
    if (!rgb) {
        return DSC_INVALID;
    }
    k->rgb = rgb;
    k->planes = NULL;
    k->width = slice ? c->slice_width : c->pic_width;
    if (cap < (size_t)k->width * (slice ? c->slice_height : c->pic_height) * 3) {
        return DSC_LIMIT;
    }
    return DSC_OK;
}

static int planes_sink(const struct drm_dsc_config *c, int slice, const struct dsc_planes *o,
                       struct sink *k)
{
    struct dsc_format f;
    int status = validate(c, &f);

    if (status) {
        return status;
    }
    status = check_planes(c, slice, o);
    if (status) {
        return status;
    }
    k->rgb = NULL;
    k->planes = o;
    k->width = slice ? c->slice_width : c->pic_width;
    return DSC_OK;
}

int dsc_decode_slice_ex(const struct drm_dsc_config *c, const struct dsc_options *opt,
                        const uint8_t *p, size_t n, uint8_t *rgb, size_t cap)
{
    struct sink k;
    int status = rgb888_sink(c, 1, rgb, cap, &k);

    return status ? status : decode_single(c, opt, p, n, &k);
}

int dsc_decode_slice(const struct drm_dsc_config *c, const uint8_t *p, size_t n, uint8_t *rgb,
                     size_t cap)
{
    return dsc_decode_slice_ex(c, NULL, p, n, rgb, cap);
}

int dsc_decode_frame_ex(const struct drm_dsc_config *c, const struct dsc_options *opt,
                        const uint8_t *data, size_t n, uint8_t *rgb, size_t cap)
{
    struct sink k;
    int status = rgb888_sink(c, 0, rgb, cap, &k);

    return status ? status : decode_picture(c, opt, data, n, &k);
}

int dsc_decode_frame(const struct drm_dsc_config *c, const uint8_t *data, size_t n, uint8_t *rgb,
                     size_t cap)
{
    return dsc_decode_frame_ex(c, NULL, data, n, rgb, cap);
}

int dsc_decode_slice_planes(const struct drm_dsc_config *c, const struct dsc_options *opt,
                            const uint8_t *p, size_t n, const struct dsc_planes *out)
{
    struct sink k;
    int status = planes_sink(c, 1, out, &k);

    return status ? status : decode_single(c, opt, p, n, &k);
}

int dsc_decode_frame_planes(const struct drm_dsc_config *c, const struct dsc_options *opt,
                            const uint8_t *data, size_t n, const struct dsc_planes *out)
{
    struct sink k;
    int status = planes_sink(c, 0, out, &k);

    return status ? status : decode_picture(c, opt, data, n, &k);
}
