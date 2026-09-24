/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "dsc.h"
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] * 256u + p[1]); }

int dsc_parse_pps(const uint8_t *p, size_t n, struct drm_dsc_config *c)
{
    unsigned i, slices, bits;
    if (!p || !c || n != DSC_PPS_BYTES) return DSC_INVALID;
    memset(c, 0, sizeof(*c));
    c->dsc_version_major = p[0] >> 4;
    c->dsc_version_minor = p[0] & 15;
    if (c->dsc_version_major != 1 || (c->dsc_version_minor != 1 && c->dsc_version_minor != 2))
        return DSC_UNSUPPORTED;
    if (p[2] || (p[4] & 192) || (p[16] & 252) || p[20] || (p[21] & 192) || (p[24] & 240) || p[26] ||
        (p[27] & 224) || (p[36] & 224) || (p[37] & 224) || (p[40] & 240) || (p[41] & 224) ||
        (p[42] & 224))
        return DSC_INVALID;
    for (i = c->dsc_version_minor == 1 ? 88 : 94; i < 128; i++) {
        if (p[i]) return DSC_INVALID;
    }
    if (c->dsc_version_minor == 2 && ((p[88] & 252) || (p[89] & 224))) return DSC_INVALID;
    c->bits_per_component = p[3] >> 4;
    c->line_buf_depth = p[3] & 15;
    if (c->dsc_version_minor == 2) {
        if (!c->bits_per_component) c->bits_per_component = 16;
        if (!c->line_buf_depth) c->line_buf_depth = 16;
    }
    c->block_pred_enable = !!(p[4] & 32);
    c->convert_rgb = !!(p[4] & 16);
    c->simple_422 = !!(p[4] & 8);
    c->vbr_enable = !!(p[4] & 4);
    c->bits_per_pixel = ((p[4] & 3) << 8) | p[5];
    c->pic_height = be16(p + 6);
    c->pic_width = be16(p + 8);
    c->slice_height = be16(p + 10);
    c->slice_width = be16(p + 12);
    c->slice_chunk_size = be16(p + 14);
    c->initial_xmit_delay = be16(p + 16);
    c->initial_dec_delay = be16(p + 18);
    c->initial_scale_value = p[21];
    c->scale_increment_interval = be16(p + 22);
    c->scale_decrement_interval = be16(p + 24);
    c->first_line_bpg_offset = p[27];
    c->nfl_bpg_offset = be16(p + 28);
    c->slice_bpg_offset = be16(p + 30);
    c->initial_offset = be16(p + 32);
    c->final_offset = be16(p + 34);
    c->flatness_min_qp = p[36];
    c->flatness_max_qp = p[37];
    c->rc_model_size = be16(p + 38);
    c->rc_edge_factor = p[40];
    c->rc_quant_incr_limit0 = p[41];
    c->rc_quant_incr_limit1 = p[42];
    c->rc_tgt_offset_high = p[43] >> 4;
    c->rc_tgt_offset_low = p[43] & 15;
    for (i = 0; i < 14; i++) {
        c->rc_buf_thresh[i] = p[44 + i];
    }
    for (i = 0; i < 15; i++) {
        unsigned r = be16(p + 58 + 2 * i);
        c->rc_range_params[i].range_min_qp = (u8)(r >> 11);
        c->rc_range_params[i].range_max_qp = (r >> 6) & 31;
        c->rc_range_params[i].range_bpg_offset = r & 63;
    }
    if (c->dsc_version_minor == 2) {
        c->native_422 = !!(p[88] & 1);
        c->native_420 = !!(p[88] & 2);
        c->second_line_bpg_offset = p[89];
        c->nsl_bpg_offset = be16(p + 90);
        c->second_line_offset_adj = be16(p + 92);
    }
    if (!c->pic_width || !c->pic_height || !c->slice_width || !c->slice_height ||
        !c->bits_per_pixel || !c->slice_chunk_size || !c->rc_model_size)
        return DSC_INVALID;
    slices = ((unsigned)c->pic_width + c->slice_width - 1) / c->slice_width;
    if (slices > 255) return DSC_LIMIT;
    c->slice_count = (u8)slices;
    c->mux_word_size = c->bits_per_component <= 10 ? 48 : 64;
    bits = ((unsigned)c->initial_xmit_delay + c->initial_dec_delay) * c->bits_per_pixel;
    bits = (bits + 15) / 16;
    if (bits > 65535) return DSC_LIMIT;
    c->rc_bits = (u16)bits;
    return DSC_OK;
}

const char *dsc_strerror(int s)
{
    switch (s) {
    case DSC_OK: return "success";
    case DSC_INVALID: return "invalid parameters";
    case DSC_UNSUPPORTED: return "unsupported DSC profile or transport";
    case DSC_TRUNCATED: return "truncated input";
    case DSC_LIMIT: return "resource or representation limit";
    case DSC_NOMEM: return "allocation failed";
    case DSC_BITSTREAM: return "invalid compressed slice";
    case DSC_RATE_CONTROL: return "rate-control buffer violation";
    default: return "unknown error";
    }
}
