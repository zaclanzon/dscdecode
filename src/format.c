/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Parameters derived from the PPS for the entropy decoder, predictor and
 * rate control. Section numbers are DSC 1.1 unless marked otherwise.
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
 * agrees with the 83-unit balance FIFO of §3.7.1 (48 + 36 - 1). */
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
    memset(f, 0, sizeof(*f));
    if (!c || c->dsc_version_major != 1 || c->dsc_version_minor != 1) {
        return DSC_UNSUPPORTED;
    }
    /* §4.1: 8, 10 or 12 bits per component. */
    if (c->bits_per_component != 8 && c->bits_per_component != 10 &&
        c->bits_per_component != 12) {
        return DSC_UNSUPPORTED;
    }
    if (!c->convert_rgb || c->simple_422 || c->native_422 || c->native_420) {
        return DSC_UNSUPPORTED;
    }
    f->version = c->dsc_version_minor;
    f->bpc = c->bits_per_component;
    f->rgb = 1;
    f->units = 3;
    /* §6.1: YCoCg-R chroma has one bit more than luma. */
    f->depth[0] = f->bpc;
    f->depth[1] = f->depth[2] = f->bpc + 1;
    f->luma[0] = 1;
    /* §4.4: 48-bit mux words for 8 and 10 bpc, 64-bit for 12 bpc. */
    f->mux_word = f->bpc <= 10 ? 48 : 64;
    dsc_qp_scale(f->bpc, &f->max_qp, &f->flat_type_qp, &f->very_flat_qp);
    set_units(f);
    return DSC_OK;
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
 * 8, 10 and 12 bpc agrees with this. */
unsigned dsc_qlevel(const struct dsc_format *f, unsigned qp, unsigned unit)
{
    unsigned y = qp ? (qp - 1) / 2 : 0, c = qp ? qp / 2 + 1 : 0;

    if (qp + 2 == f->max_qp) {
        --y;
        ++c;
    }
    return f->luma[unit] ? y : c;
}
