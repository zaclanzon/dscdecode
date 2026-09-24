/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Userspace compatibility definitions required by the unmodified drm_dsc.h.
 * This is not the kernel DisplayPort API.
 */
#ifndef DSC_USERSPACE_DP_H
#define DSC_USERSPACE_DP_H
#include <stdint.h>
#include <stdbool.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint16_t __be16;
#define __packed __attribute__((__packed__))
struct dp_sdp_header { u8 HB0, HB1, HB2, HB3; } __packed;
#endif
