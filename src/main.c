/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include "dsc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Input files need not be seekable. Limit both allocation and read work. */
static uint8_t *read_file(const char *name, size_t limit, size_t *length)
{
    FILE *f = fopen(name, "rb");
    size_t n = 0, cap = limit < 4096 ? limit : 4096;
    uint8_t *p;

    if (!f) {
        perror(name);
        return NULL;
    }
    p = malloc(cap ? cap : 1);
    if (!p) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        size_t got;

        if (n == cap) {
            size_t next = cap > limit / 2 ? limit : cap * 2;
            uint8_t *q;

            if (cap == limit) {
                if (fgetc(f) != EOF || ferror(f)) {
                    fprintf(stderr, "%s: input too large or unreadable\n", name);
                    free(p);
                    fclose(f);
                    return NULL;
                }
                break;
            }
            q = realloc(p, next);
            if (!q) {
                free(p);
                fclose(f);
                return NULL;
            }
            p = q;
            cap = next;
        }
        got = fread(p + n, 1, cap - n, f);
        n += got;
        if (!got) {
            if (ferror(f)) {
                perror(name);
                free(p);
                fclose(f);
                return NULL;
            }
            break;
        }
    }
    if (fclose(f)) {
        free(p);
        return NULL;
    }
    *length = n;
    return p;
}

/* Interpretation switches: RESEARCH.md, open questions. */
struct reading {
    const char *name, *value;
    int *(*field)(struct dsc_options *);
    int code;
};

static int *flat_restart(struct dsc_options *o)
{
    return &o->flat_restart;
}

static int *threshold_eq(struct dsc_options *o)
{
    return &o->threshold_eq;
}

static int *frac_reset(struct dsc_options *o)
{
    return &o->frac_reset;
}

static int *delay_offset(struct dsc_options *o)
{
    return &o->delay_offset;
}

static int *bp_left(struct dsc_options *o)
{
    return &o->bp_left;
}

static int *bp_edge(struct dsc_options *o)
{
    return &o->bp_edge;
}

static int *bp_sad(struct dsc_options *o)
{
    return &o->bp_sad;
}

static int *incr_order(struct dsc_options *o)
{
    return &o->incr_order;
}

static int *rc_pipeline(struct dsc_options *o)
{
    return &o->rc_pipeline;
}

static int *scale_dec(struct dsc_options *o)
{
    return &o->scale_dec;
}

static int *partial_target(struct dsc_options *o)
{
    return &o->partial_target;
}

static int *very_flat(struct dsc_options *o)
{
    return &o->very_flat;
}

static int *partial_padding(struct dsc_options *o)
{
    return &o->partial_padding;
}

static int *flat_max_qp(struct dsc_options *o)
{
    return &o->flat_max_qp;
}

static int *delay_partial(struct dsc_options *o)
{
    return &o->delay_partial;
}

static int *bpg_combine(struct dsc_options *o)
{
    return &o->bpg_combine;
}

static int *chroma_qlevel(struct dsc_options *o)
{
    return &o->chroma_qlevel;
}

static int *prefix16(struct dsc_options *o)
{
    return &o->prefix16;
}

static int *bitsave_ich(struct dsc_options *o)
{
    return &o->bitsave_ich;
}

static int *bitsave_pred(struct dsc_options *o)
{
    return &o->bitsave_pred;
}

static int *bitsave_flat(struct dsc_options *o)
{
    return &o->bitsave_flat;
}

static int *line_flat(struct dsc_options *o)
{
    return &o->line_flat;
}

static int *low_min(struct dsc_options *o)
{
    return &o->low_min;
}

static int *decrement_test(struct dsc_options *o)
{
    return &o->decrement_test;
}

static int *activity_qp(struct dsc_options *o)
{
    return &o->activity_qp;
}

static int *bitsave_step(struct dsc_options *o)
{
    return &o->bitsave_step;
}

static int *target_floor(struct dsc_options *o)
{
    return &o->target_floor;
}

static int *flat_rerun(struct dsc_options *o)
{
    return &o->flat_rerun;
}

static int *rerun_bitsave(struct dsc_options *o)
{
    return &o->rerun_bitsave;
}

static int *mux16(struct dsc_options *o)
{
    return &o->mux16;
}

static int *flat_top(struct dsc_options *o)
{
    return &o->flat_top;
}

static int *prefix16_scope(struct dsc_options *o)
{
    return &o->prefix16_scope;
}

static int *prefix16_cut(struct dsc_options *o)
{
    return &o->prefix16_cut;
}

static int *activity420(struct dsc_options *o)
{
    return &o->activity420;
}

static int *activity422(struct dsc_options *o)
{
    return &o->activity422;
}

static int *bp420_edge(struct dsc_options *o)
{
    return &o->bp420_edge;
}

static int *offset_adj(struct dsc_options *o)
{
    return &o->offset_adj;
}

static int *ich_window(struct dsc_options *o)
{
    return &o->ich_window;
}

static int *scale_first(struct dsc_options *o)
{
    return &o->scale_first;
}

static int *scale_line(struct dsc_options *o)
{
    return &o->scale_line;
}

static const struct reading readings[] = {
    {"flat_restart",    "next-cycle",   flat_restart,    DSC_FLAT_RESTART_NEXT_CYCLE},
    {"flat_restart",    "in-flight",    flat_restart,    DSC_FLAT_RESTART_IN_FLIGHT},
    {"threshold_eq",    "lower",        threshold_eq,    DSC_THRESHOLD_EQ_LOWER},
    {"threshold_eq",    "upper",        threshold_eq,    DSC_THRESHOLD_EQ_UPPER},
    {"frac_reset",      "chunk",        frac_reset,      DSC_FRAC_RESET_CHUNK},
    {"frac_reset",      "literal",      frac_reset,      DSC_FRAC_RESET_LITERAL},
    {"delay_offset",    "inclusive",    delay_offset,    DSC_DELAY_OFFSET_INCLUSIVE},
    {"delay_offset",    "exclusive",    delay_offset,    DSC_DELAY_OFFSET_EXCLUSIVE},
    {"bp_left",         "replicate",    bp_left,         DSC_BP_LEFT_REPLICATE},
    {"bp_left",         "midpoint",     bp_left,         DSC_BP_LEFT_MIDPOINT},
    {"bp_edge",         "window",       bp_edge,         DSC_BP_EDGE_WINDOW},
    {"bp_edge",         "before",       bp_edge,         DSC_BP_EDGE_BEFORE},
    {"bp_sad",          "shift",        bp_sad,          DSC_BP_SAD_SHIFT},
    {"bp_sad",          "clip",         bp_sad,          DSC_BP_SAD_CLIP},
    {"incr_order",      "printed",      incr_order,      DSC_INCR_ORDER_PRINTED},
    {"incr_order",      "swapped",      incr_order,      DSC_INCR_ORDER_SWAPPED},
    {"rc_pipeline",     "same-group",   rc_pipeline,     DSC_RC_PIPELINE_SAME_GROUP},
    {"rc_pipeline",     "range-lag",    rc_pipeline,     DSC_RC_PIPELINE_RANGE_LAG},
    {"scale_dec",       "from-group-1", scale_dec,       DSC_SCALE_DEC_FROM_GROUP_1},
    {"scale_dec",       "from-group-0", scale_dec,       DSC_SCALE_DEC_FROM_GROUP_0},
    {"partial_target",  "three",        partial_target,  DSC_PARTIAL_TARGET_THREE},
    {"partial_target",  "pixels",       partial_target,  DSC_PARTIAL_TARGET_PIXELS},
    {"very_flat",       "group-qp",     very_flat,       DSC_VERY_FLAT_GROUP_QP},
    {"very_flat",       "as-signaled",  very_flat,       DSC_VERY_FLAT_AS_SIGNALED},
    {"very_flat",       "previous-qp",  very_flat,       DSC_VERY_FLAT_PREVIOUS_QP},
    {"partial_padding", "reject",       partial_padding, DSC_PARTIAL_PADDING_REJECT},
    {"partial_padding", "accept",       partial_padding, DSC_PARTIAL_PADDING_ACCEPT},
    {"flat_max_qp",     "own",          flat_max_qp,     DSC_FLAT_MAX_QP_OWN},
    {"flat_max_qp",     "previous",     flat_max_qp,     DSC_FLAT_MAX_QP_PREVIOUS},
    {"delay_partial",   "pixels",       delay_partial,   DSC_DELAY_PARTIAL_PIXELS},
    {"delay_partial",   "group-end",    delay_partial,   DSC_DELAY_PARTIAL_GROUP_END},
    {"bpg_combine",     "replace",      bpg_combine,     DSC_BPG_COMBINE_REPLACE},
    {"bpg_combine",     "add",          bpg_combine,     DSC_BPG_COMBINE_ADD},
    {"chroma_qlevel",   "table",        chroma_qlevel,   DSC_CHROMA_QLEVEL_TABLE},
    {"chroma_qlevel",   "equal-depth",  chroma_qlevel,   DSC_CHROMA_QLEVEL_EQUAL_DEPTH},
    {"prefix16",        "15",           prefix16,        DSC_PREFIX16_15},
    {"prefix16",        "13",           prefix16,        DSC_PREFIX16_13},
    {"bitsave_ich",     "not",          bitsave_ich,     DSC_BITSAVE_ICH_NOT},
    {"bitsave_ich",     "set",          bitsave_ich,     DSC_BITSAVE_ICH_SET},
    {"bitsave_pred",    "raw",          bitsave_pred,    DSC_BITSAVE_PRED_RAW},
    {"bitsave_pred",    "adjusted",     bitsave_pred,    DSC_BITSAVE_PRED_ADJUSTED},
    {"bitsave_pred",    "next",         bitsave_pred,    DSC_BITSAVE_PRED_NEXT},
    {"bitsave_flat",    "supergroup",   bitsave_flat,    DSC_BITSAVE_FLAT_SUPERGROUP},
    {"bitsave_flat",    "group",        bitsave_flat,    DSC_BITSAVE_FLAT_GROUP},
    {"bitsave_flat",    "received",     bitsave_flat,    DSC_BITSAVE_FLAT_RECEIVED},
    {"bitsave_flat",    "carrier",      bitsave_flat,    DSC_BITSAVE_FLAT_CARRIER},
    {"bitsave_flat",    "span",         bitsave_flat,    DSC_BITSAVE_FLAT_SPAN},
    {"bitsave_flat",    "lagged",       bitsave_flat,    DSC_BITSAVE_FLAT_LAGGED},
    {"line_flat",       "very",         line_flat,       DSC_LINE_FLAT_VERY},
    {"line_flat",       "signaled",     line_flat,       DSC_LINE_FLAT_SIGNALED},
    {"low_min",         "max-qp",       low_min,         DSC_LOW_MIN_MAX_QP},
    {"low_min",         "min-qp",       low_min,         DSC_LOW_MIN_MIN_QP},
    {"decrement_test",  "both",         decrement_test,  DSC_DECREMENT_BOTH},
    {"decrement_test",  "size",         decrement_test,  DSC_DECREMENT_SIZE},
    {"activity_qp",     "prev",         activity_qp,     DSC_ACTIVITY_PREV},
    {"activity_qp",     "prev2",        activity_qp,     DSC_ACTIVITY_PREV2},
    {"bitsave_step",    "1",            bitsave_step,    DSC_BITSAVE_STEP_1},
    {"bitsave_step",    "2",            bitsave_step,    DSC_BITSAVE_STEP_2},
    {"target_floor",    "none",         target_floor,    DSC_TARGET_FLOOR_NONE},
    {"target_floor",    "zero",         target_floor,    DSC_TARGET_FLOOR_ZERO},
    {"flat_rerun",      "changed",      flat_rerun,      DSC_FLAT_RERUN_CHANGED},
    {"flat_rerun",      "every",        flat_rerun,      DSC_FLAT_RERUN_EVERY},
    {"rerun_bitsave",   "keep",         rerun_bitsave,   DSC_RERUN_BITSAVE_KEEP},
    {"rerun_bitsave",   "redo",         rerun_bitsave,   DSC_RERUN_BITSAVE_REDO},
    {"mux16",           "68",           mux16,           DSC_MUX16_ELEMENT},
    {"mux16",           "64",           mux16,           DSC_MUX16_WORD},
    {"prefix16_scope",  "qp0",          prefix16_scope,  DSC_PREFIX16_SCOPE_QP0},
    {"prefix16_scope",  "qlevel",       prefix16_scope,  DSC_PREFIX16_SCOPE_QLEVEL},
    {"prefix16_cut",    "always",       prefix16_cut,    DSC_PREFIX16_CUT_ALWAYS},
    {"prefix16_cut",    "longer",       prefix16_cut,    DSC_PREFIX16_CUT_LONGER},
    {"flat_top",        "equal",        flat_top,        DSC_FLAT_TOP_EQUAL},
    {"flat_top",        "at-or-above",  flat_top,        DSC_FLAT_TOP_AT_OR_ABOVE},
    {"activity420",     "luma",         activity420,     DSC_ACTIVITY420_LUMA},
    {"activity420",     "sum",          activity420,     DSC_ACTIVITY420_SUM},
    {"activity422",     "sizes",        activity422,     DSC_ACTIVITY422_SIZES},
    {"activity422",     "total",        activity422,     DSC_ACTIVITY422_TOTAL},
    {"bp420_edge",      "luma",         bp420_edge,      DSC_BP420_EDGE_LUMA},
    {"bp420_edge",      "all",          bp420_edge,      DSC_BP420_EDGE_ALL},
    {"offset_adj",      "subtract",     offset_adj,      DSC_OFFSET_ADJ_SUBTRACT},
    {"offset_adj",      "start",        offset_adj,      DSC_OFFSET_ADJ_START},
    {"ich_window",      "pixels",       ich_window,      DSC_ICH_WINDOW_PIXELS},
    {"ich_window",      "container",    ich_window,      DSC_ICH_WINDOW_CONTAINER},
    {"scale_first",     "group",        scale_first,     DSC_SCALE_FIRST_GROUP},
    {"scale_first",     "not",          scale_first,     DSC_SCALE_FIRST_NOT},
    {"scale_line",      "until-unity",  scale_line,      DSC_SCALE_LINE_UNTIL_UNITY},
    {"scale_line",      "first",        scale_line,      DSC_SCALE_LINE_FIRST},
};

static const struct reading *find_reading(const char *name, const char *value)
{
    size_t i;

    for (i = 0; i < sizeof(readings) / sizeof(readings[0]); i++) {
        if (!strcmp(readings[i].name, name) && !strcmp(readings[i].value, value)) {
            return &readings[i];
        }
    }
    return NULL;
}

static int set_reading(struct dsc_options *o, const char *arg)
{
    char name[32];
    const char *eq = strchr(arg, '=');
    const struct reading *r;
    size_t len = eq ? (size_t)(eq - arg) : 0;

    if (!eq || len >= sizeof(name)) {
        return -1;
    }
    memcpy(name, arg, len);
    name[len] = 0;
    r = find_reading(name, eq + 1);
    if (!r) {
        return -1;
    }
    *r->field(o) = r->code;
    return 0;
}

static void trace_csv(void *context, const struct dsc_group_trace *t)
{
    fprintf((FILE *)context, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%lld,%lld,%d,%d\n", t->slice, t->group,
            t->x, t->y, t->qp, t->actual, t->ideal, t->range, t->generated_qp, t->buffer_fullness,
            t->model_fullness, t->ich, t->flat_override);
}

static int usage(const char *self)
{
    size_t i;
    struct dsc_options defaults;

    dsc_options_init(&defaults);
    fprintf(stderr,
            "Usage: %s [--slice] [--stats] [--trace FILE.csv] [--reading NAME=VALUE]... PPS.bin slices.bin OUTPUT\n"
            "OUTPUT is a binary PPM for RGB (convert_rgb 1) and raw YCbCr, NAME.yuv, otherwise:\n"
            "planar 4:2:0 for native 4:2:0, UYVY for 4:2:2, planar 4:4:4; above 8 bits,\n"
            "two bytes per sample, least significant first; UYVY samples in the top bits.\n"
            "Readings (see RESEARCH.md, open questions):\n",
            self);
    for (i = 0; i < sizeof(readings) / sizeof(readings[0]); i++) {
        fprintf(stderr, "  %s=%s%s\n", readings[i].name, readings[i].value,
                *readings[i].field(&defaults) == readings[i].code ? " (default)" : "");
    }
    return 2;
}

/* Binary PPM. Samples of up to 8 bits take one byte, deeper samples two,
 * most significant first, with maxval 2^bits - 1. */
static int write_ppm(FILE *out, const struct dsc_planes *o, unsigned w, unsigned h, unsigned bits)
{
    unsigned x, y, c, bytes = bits > 8 ? 2 : 1;
    uint8_t *row = malloc((size_t)w * 3 * bytes);
    int status = 0;

    if (!row) {
        return -1;
    }
    if (fprintf(out, "P6\n%u %u\n%u\n", w, h, (1u << bits) - 1) < 0) {
        status = -1;
    }
    for (y = 0; y < h && !status; y++) {
        for (x = 0; x < w; x++) {
            for (c = 0; c < 3; c++) {
                unsigned v = o->plane[c][(size_t)y * o->stride[c] + x];
                uint8_t *at = row + ((size_t)x * 3 + c) * bytes;

                if (bytes == 2) {
                    at[0] = (uint8_t)(v >> 8);
                    at[1] = (uint8_t)v;
                } else {
                    at[0] = (uint8_t)v;
                }
            }
        }
        if (fwrite(row, 1, (size_t)w * 3 * bytes, out) != (size_t)w * 3 * bytes) {
            status = -1;
        }
    }
    free(row);
    return status;
}

/* Raw YCbCr in the layouts the reference model reads and writes (its
 * README.TXT: YUV_FILE_FORMAT 0 and 1): planar 4:2:0 (Y, Cb, Cr) for native
 * 4:2:0, interleaved UYVY for 4:2:2. 4:4:4 YCbCr, which the model writes as
 * DPX only, is written planar (Y, Cb, Cr). Samples above 8 bits take two
 * bytes, least significant first; as in the model's files, planar samples
 * hold the value as it is, UYVY samples in the most significant bits. An
 * odd-width 4:2:2 row ends with its last luma sample repeated. */
static int put_sample(FILE *out, unsigned v, unsigned bits, int top)
{
    if (bits > 8) {
        v <<= top ? 16 - bits : 0;
        if (fputc((int)(v & 255), out) == EOF) {
            return -1;
        }
        v >>= 8;
    }
    return fputc((int)v, out) == EOF ? -1 : 0;
}

static int write_yuv(FILE *out, const struct dsc_planes *o, const unsigned w[3],
                     const unsigned h[3], unsigned bits, int interleaved)
{
    unsigned x, y, c;

    if (interleaved) {
        for (y = 0; y < h[0]; y++) {
            for (x = 0; x < w[0]; x += 2) {
                const uint16_t *luma = o->plane[0] + (size_t)y * o->stride[0];
                unsigned odd = x + 1 < w[0] ? luma[x + 1] : luma[x];

                if (put_sample(out, o->plane[1][(size_t)y * o->stride[1] + x / 2], bits, 1) ||
                    put_sample(out, luma[x], bits, 1) ||
                    put_sample(out, o->plane[2][(size_t)y * o->stride[2] + x / 2], bits, 1) ||
                    put_sample(out, odd, bits, 1)) {
                    return -1;
                }
            }
        }
        return 0;
    }
    for (c = 0; c < 3; c++) {
        for (y = 0; y < h[c]; y++) {
            for (x = 0; x < w[c]; x++) {
                if (put_sample(out, o->plane[c][(size_t)y * o->stride[c] + x], bits, 0)) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), k = strlen(suffix);

    return n >= k && !strcmp(name + n - k, suffix);
}

int main(int argc, char **argv)
{
    int single = 0, want_stats = 0, status, exitcode = 1, off = 1, yuv;
    unsigned w, h, pw[3], ph[3], i;
    size_t pps_n, n;
    struct drm_dsc_config c;
    struct dsc_options opt;
    struct dsc_stats stats = {0};
    struct dsc_planes planes = {{NULL}, {0}, {0}};
    uint8_t *pps = NULL, *input = NULL;
    FILE *output = NULL, *trace = NULL;

    dsc_options_init(&opt);
    for (; off < argc && !strncmp(argv[off], "--", 2); off++) {
        if (!strcmp(argv[off], "--slice")) {
            single = 1;
        } else if (!strcmp(argv[off], "--stats")) {
            want_stats = 1;
        } else if (!strcmp(argv[off], "--reading") && off + 1 < argc) {
            if (set_reading(&opt, argv[++off])) {
                fprintf(stderr, "unknown reading: %s\n", argv[off]);
                return usage(argv[0]);
            }
        } else if (!strcmp(argv[off], "--trace") && off + 1 < argc) {
            if (trace) {
                fclose(trace);
            }
            trace = fopen(argv[++off], "w");
            if (!trace) {
                perror(argv[off]);
                return 1;
            }
        } else {
            if (trace) {
                fclose(trace);
            }
            return usage(argv[0]);
        }
    }
    if (argc - off != 3) {
        if (trace) {
            fclose(trace);
        }
        return usage(argv[0]);
    }
    /* Always counted: the padding warning below needs the count. */
    opt.stats = &stats;
    if (trace) {
        fputs("slice,group,x,y,qp,actual,ideal,range,generated_qp,buffer_fullness,model_fullness,ich,flat_override\n",
              trace);
        opt.trace = trace_csv;
        opt.trace_context = trace;
    }
    pps = read_file(argv[off], 128, &pps_n);
    if (!pps) {
        goto done;
    }
    status = dsc_parse_pps(pps, pps_n, &c);
    if (status) {
        fprintf(stderr, "PPS: %s\n", dsc_strerror(status));
        goto done;
    }
    status = dsc_plane_size(&c, single, pw, ph);
    if (status) {
        fprintf(stderr, "decode: %s\n", dsc_strerror(status));
        goto done;
    }
    yuv = has_suffix(argv[off + 2], ".yuv");
    if (yuv == !!c.convert_rgb) {
        fprintf(stderr, "%s\n", c.convert_rgb ? "RGB output: name a .ppm file, not .yuv"
                                              : "YCbCr output: name a .yuv file");
        goto done;
    }
    w = pw[0];
    h = ph[0];
    if ((size_t)w * h > DSC_MAX_PIXELS) {
        fprintf(stderr, "image exceeds pixel limit\n");
        goto done;
    }
    input = read_file(argv[off + 1], 256u * 1024u * 1024u, &n);
    if (!input) {
        goto done;
    }
    for (i = 0; i < 3; i++) {
        planes.stride[i] = pw[i];
        planes.capacity[i] = (size_t)pw[i] * ph[i];
        planes.plane[i] = malloc(planes.capacity[i] * sizeof(uint16_t));
        if (!planes.plane[i]) {
            goto done;
        }
    }
    status = single ? dsc_decode_slice_planes(&c, &opt, input, n, &planes)
                    : dsc_decode_frame_planes(&c, &opt, input, n, &planes);
    if (want_stats) {
        fprintf(stderr,
                "stats: groups=%lu threshold_equal=%lu flat_overrides=%lu flat_queue_differs=%lu frac_differs=%lu bp_groups=%lu bp_left_differs=%lu"
                " incr_order_differs=%lu range_lag_differs=%lu partial_groups=%lu very_flat_low_qp=%lu padding_nonzero=%lu"
                " flat_max_qp_differs=%lu delay_partial_differs=%lu bit_save_groups=%lu line_flat=%lu"
                " activity_differs=%lu bp420_edge_differs=%lu ich_window_differs=%lu\n",
                stats.groups, stats.threshold_equal, stats.flat_overrides,
                stats.flat_queue_differs, stats.frac_differs, stats.bp_groups,
                stats.bp_left_differs, stats.incr_order_differs, stats.range_lag_differs,
                stats.partial_groups, stats.very_flat_low_qp, stats.padding_nonzero,
                stats.flat_max_qp_differs, stats.delay_partial_differs, stats.bit_save_groups,
                stats.line_flat, stats.activity_differs, stats.bp420_edge_differs,
                stats.ich_window_differs);
    }
    /* OQ-17: padding the default reading accepts is reported, not hidden. */
    if (opt.partial_padding == DSC_PARTIAL_PADDING_ACCEPT && stats.padding_nonzero) {
        fprintf(stderr,
                "warning: %lu partial group%s with nonzero padding accepted (DSC 1.1 section 6.6);"
                " --reading partial_padding=reject makes this an error\n",
                stats.padding_nonzero, stats.padding_nonzero == 1 ? "" : "s");
    }
    if (status) {
        fprintf(stderr, "decode: %s\n", dsc_strerror(status));
        goto done;
    }
    /* Only open output after successful validation and decoding. */
    output = fopen(argv[off + 2], "wb");
    if (!output) {
        perror(argv[off + 2]);
        goto done;
    }
    if (yuv ? write_yuv(output, &planes, pw, ph, c.bits_per_component,
                        c.simple_422 || c.native_422)
            : write_ppm(output, &planes, w, h, c.bits_per_component)) {
        fprintf(stderr, "output write failed\n");
        goto done;
    }
    if (fclose(output)) {
        output = NULL;
        fprintf(stderr, "output close failed\n");
        goto done;
    }
    output = NULL;
    exitcode = 0;
done:
    if (output) {
        fclose(output);
    }
    if (trace && fclose(trace)) {
        exitcode = 1;
    }
    free(pps);
    free(input);
    for (i = 0; i < 3; i++) {
        free(planes.plane[i]);
    }
    return exitcode;
}
