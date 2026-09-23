/* SPDX-License-Identifier: MIT */
#include "dsc.h"
#include <stdlib.h>
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
 struct drm_dsc_config c;
 uint8_t *out;
 size_t pixels;
 if(size<DSC_PPS_BYTES || dsc_parse_pps(data,DSC_PPS_BYTES,&c))return 0;
 pixels=(size_t)c.pic_width*c.pic_height;
 /* Bound per-input work so coverage-guided fuzzing cannot allocate enormous
  * frames. Public decoder has its own independent, larger resource limits. */
 if(pixels>4096 || (size_t)c.slice_width*c.slice_height>4096)return 0;
 out=malloc(4096*3);
 if(!out)return 0;
 dsc_decode_frame(&c,data+128,size-128,out,4096*3);
 dsc_decode_slice(&c,data+128,size-128,out,4096*3);
 free(out);return 0;
}
