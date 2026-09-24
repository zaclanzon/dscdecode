/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Parameters derived from the PPS for the entropy decoder, predictor and
 * rate control. Section numbers are DSC 1.1 unless marked otherwise; DSC 1.2
 * behavior follows DSC 1.2b.
 */

#include "format.h"
#include <string.h>

/* The maximum size of one syntax element, the refill threshold of each
 * substream (§4.4, §7.1). The text gives no formula; it follows from the
 * syntax of §4.5. A luma unit holds up to three flatness bits, a prefix of up
 * to bpc + 1 bits (the P-mode maximum after an ICH group, "0" and bpc zeros),
 * and three bpc-bit residuals: 4 * bpc + 4. A chroma unit holds a prefix of up
 * to cpntBitDepth zeros, its "1" inferred, and three residuals of that size:
 * 4 * cpntBitDepth. ICH units are shorter. For 8 bpc RGB both are 36, which
 * agrees with the 83-unit balance FIFO of §3.7.1 (48 + 36 - 1). At 16 bpc
 * the luma formula gives 68 against 64-bit mux words; DSC 1.2b §3.10.2 and
 * Table 4-10 limit the luma prefix at QP 0 to keep elements within a mux
 * word, but the text does not restate the refill threshold (OQ-33,
 * dsc_format_apply_options). */
/* Native modes (DSC 1.2b Table E-1, numExtraMuxBits) count Y2 and the
 * odd-position luma of 4:2:0 with the chroma units: 4 * cpntBitDepth. */
static void set_units(struct dsc_format *f)
{
    unsigned u;

    for (u = 0; u < f->units; u++) {
        f->max_se[u] = u == 0 ? 4 * f->bpc + 4 : 4 * f->depth[u];
    }
}

/* Table 6-2 ends at QP 15, 19 and 23 for 8, 10 and 12 bpc; §4.5 and
 * §6.8.5.2 scale the flatness QPs by the same two steps per bit. */
int dsc_qp_scale(unsigned bpc, unsigned *max_qp, unsigned *flat_type_qp, unsigned *very_flat_qp)
{
    if (bpc < 8 || bpc > 16 || bpc % 2) {
        return -1;
    }
    *max_qp = 15 + 2 * (bpc - 8);
    *flat_type_qp = 7 + 2 * (bpc - 8);
    *very_flat_qp = 1 + 2 * (bpc - 8);
    return 0;
}

int dsc_format_init(struct dsc_format *f, const struct drm_dsc_config *c)
{
    unsigned u;

    memset(f, 0, sizeof(*f));
    if (!c || c->dsc_version_major != 1 ||
        (c->dsc_version_minor != 1 && c->dsc_version_minor != 2)) {
        return DSC_UNSUPPORTED;
    }
    /* §4.1: 8, 10 or 12 bits per component; DSC 1.2 adds 14 and 16 (DSC
     * 1.2b Table 4-1). */
    if (c->bits_per_component != 8 && c->bits_per_component != 10 &&
        c->bits_per_component != 12 &&
        (c->dsc_version_minor != 2 ||
         (c->bits_per_component != 14 && c->bits_per_component != 16))) {
        return DSC_UNSUPPORTED;
    }
    /* DSC 1.2b Table 4-1: at most one of simple_422, native_422 and
     * native_420, the native modes in DSC 1.2 only; subsampled coding is
     * YCbCr (Annex B, §3.10.1). */
    if (c->simple_422 + c->native_422 + c->native_420 > 1 ||
        ((c->native_422 || c->native_420) && c->dsc_version_minor != 2)) {
        return DSC_INVALID;
    }
    if (c->convert_rgb && (c->simple_422 || c->native_422 || c->native_420)) {
        return DSC_UNSUPPORTED;
    }
    f->version = c->dsc_version_minor;
    f->bpc = c->bits_per_component;
    f->rgb = !!c->convert_rgb;
    f->simple_422 = !!c->simple_422;
    f->native = c->native_422 ? DSC_NATIVE_422 : c->native_420 ? DSC_NATIVE_420 : DSC_NATIVE_NONE;
    f->units = f->native == DSC_NATIVE_422 ? 4 : 3;
    f->depth[0] = f->bpc;
    if (f->rgb) {
        /* §6.1: YCoCg-R chroma has one bit more than luma, except at 16
         * bpc, where DSC 1.2b §6.1 rounds it to 16 bits. */
        f->depth[1] = f->depth[2] = f->bpc < 16 ? f->bpc + 1 : 16;
    } else {
        /* §7.7: Cb and Cr have the luma bit depth. */
        for (u = 1; u < f->units; u++) {
            f->depth[u] = f->bpc;
        }
    }
    f->luma[0] = 1;
    /* DSC 1.2b §6.1: the odd-position luma samples are the fourth component
     * in native 4:2:2 and the second in native 4:2:0. Luma units use qLevelY
     * (§6.4.5, §6.8.6) and the Y2 codebook of Table 4-13. */
    f->odd_luma = f->native == DSC_NATIVE_422 ? 3 : f->native == DSC_NATIVE_420 ? 1 : 0;
    if (f->odd_luma) {
        f->luma[f->odd_luma] = 1;
    }
    /* §4.5 and §6.6.2: in native 4:2:2 the Y unit carries only the ICH
     * escape, and the first index is coded in Y2. */
    f->index_unit[0] = f->native == DSC_NATIVE_422 ? 3 : 0;
    f->index_unit[1] = 1;
    f->index_unit[2] = 2;
    /* §6.4.2: BP predicts every unit, but only luma in native 4:2:0. */
    f->bp_units = f->native == DSC_NATIVE_420 ? 3u : (1u << f->units) - 1;
    /* §4.4: 48-bit mux words for 8 and 10 bpc, 64-bit above. */
    f->mux_word = f->bpc <= 10 ? 48 : 64;
    /* DSC 1.2b §6.8.6: with equal luma and chroma depths, DSC 1.2 lowers
     * qLevelC by one. That is always so for YCbCr; for RGB, 16 bpc only,
     * where OQ-20 asks whether it applies (dsc_format_apply_options). */
    f->chroma_adjust = f->version == 2 && !f->rgb;
    dsc_qp_scale(f->bpc, &f->max_qp, &f->flat_type_qp, &f->very_flat_qp);
    set_units(f);
    return DSC_OK;
}

unsigned dsc_format_width(const struct dsc_format *f, unsigned slice_width)
{
    return f->native ? slice_width >> 1 : slice_width;
}

void dsc_format_apply_options(struct dsc_format *f, const struct dsc_options *o)
{
    if (f && o && f->version == 2 && f->depth[1] == f->depth[0]) {
        f->chroma_adjust = !f->rgb || o->chroma_qlevel == DSC_CHROMA_QLEVEL_EQUAL_DEPTH;
    }
    /* OQ-33: at 16 bpc the luma threshold is 68 by the formula above, or
     * the 64-bit mux word, which the limited prefix keeps elements within. */
    if (f && o && f->bpc == 16 && o->mux16 == DSC_MUX16_WORD) {
        f->max_se[0] = f->mux_word;
    }
}

void dsc_format_default(struct dsc_format *f)
{
    struct drm_dsc_config c;

    memset(&c, 0, sizeof(c));
    c.dsc_version_major = 1;
    c.dsc_version_minor = 1;
    c.bits_per_component = 8;
    c.convert_rgb = 1;
    dsc_format_init(f, &c);
}

/* Table 6-2 as arithmetic: below the top of the scale, qLevelY is
 * (QP - 1) / 2 and qLevelC is QP / 2 + 1 (both 0 at QP 0). At two below the
 * largest QP the table moves one step from luma to chroma. Every entry for
 * 8, 10 and 12 bpc (DSC 1.1 Table 6-2) and for 14 and 16 bpc (DSC 1.2b
 * Table 6-3) agrees with this. */
unsigned dsc_qlevel(const struct dsc_format *f, unsigned qp, unsigned unit)
{
    unsigned y = qp ? (qp - 1) / 2 : 0, c = qp ? qp / 2 + 1 : 0;

    if (qp + 2 == f->max_qp) {
        --y;
        ++c;
    }
    if (f->chroma_adjust && c) {
        --c;
    }
    return f->luma[unit] ? y : c;
}
