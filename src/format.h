/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef DSC_FORMAT_H
#define DSC_FORMAT_H

#include "dsc.h"

/* Coding parameters that follow from the PPS: component bit depths, units
 * per group, substream sizes and the QP scale. One derivation, shared by the
 * entropy decoder, the predictor and the rate control. */
struct dsc_format {
    unsigned version;       /* dsc_version_minor */
    unsigned bpc;           /* bits_per_component */
    unsigned units;         /* units (and substreams) per group: 3 */
    unsigned depth[4];      /* cpntBitDepth of each unit, in substream order */
    unsigned luma[4];       /* 1 where the unit uses qLevelY */
    unsigned max_se[4];     /* maximum syntax element size of each substream */
    unsigned mux_word;      /* muxWordSize */
    unsigned max_qp;        /* largest QP: 15 + 2 * (bpc - 8) */
    unsigned flat_type_qp;  /* somewhatFlatQpThresh: 7 + 2 * (bpc - 8) */
    unsigned very_flat_qp;  /* veryFlatQp: 1 + 2 * (bpc - 8) */
    int rgb;                /* convert_rgb */
    int chroma_adjust;      /* DSC 1.2 §6.8.6: qLevelC - 1 (equal luma and chroma depths) */
};

/* QPs that scale with the bit depth (Table 6-2, §4.5, §6.8.5.2): the
 * largest QP, somewhatFlatQpThresh and veryFlatQp. Nonzero for a depth
 * without a QP scale. */
int dsc_qp_scale(unsigned bpc, unsigned *max_qp, unsigned *flat_type_qp, unsigned *very_flat_qp);

/* Zero on success; DSC_UNSUPPORTED for a format the decoder does not
 * implement. Does not validate the rest of the PPS. */
int dsc_format_init(struct dsc_format *f, const struct drm_dsc_config *c);

/* Applies the reading switches that change the format (OQ-20). */
void dsc_format_apply_options(struct dsc_format *f, const struct dsc_options *o);

/* The 8 bpc RGB format of DSC 1.1, for callers that predate other depths. */
void dsc_format_default(struct dsc_format *f);

/* Quantization level of a unit at a QP (DSC 1.1 Table 6-2, DSC 1.2b
 * Table 6-3 and §6.8.6). */
unsigned dsc_qlevel(const struct dsc_format *f, unsigned qp, unsigned unit);

#endif
