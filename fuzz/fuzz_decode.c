/* SPDX-License-Identifier: MIT */
#include "dsc.h"
#include <stdlib.h>
static void ignore_trace(void *context,const struct dsc_group_trace *t){(void)context;(void)t;}
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
 struct drm_dsc_config c;
 struct dsc_options opt;
 struct dsc_stats stats={0};
 uint8_t *out,mix=0;
 size_t pixels,i;
 if(size<DSC_PPS_BYTES || dsc_parse_pps(data,DSC_PPS_BYTES,&c))return 0;
 pixels=(size_t)c.pic_width*c.pic_height;
 /* Bound per-input work so coverage-guided fuzzing cannot allocate enormous
  * frames. Public decoder has its own independent, larger resource limits. */
 if(pixels>4096 || (size_t)c.slice_width*c.slice_height>4096)return 0;
 out=malloc(4096*3);
 if(!out)return 0;
 dsc_decode_frame(&c,data+128,size-128,out,4096*3);
 dsc_decode_slice(&c,data+128,size-128,out,4096*3);
 /* Also decode under a combination of the open-question readings, taken
  * from the payload bytes so every input stays reproducible. */
 for(i=DSC_PPS_BYTES;i<size;i++)mix^=data[i];
 dsc_options_init(&opt);
 opt.flat_restart=mix&1;opt.threshold_eq=(mix>>1)&1;
 opt.frac_reset=(mix>>2)&1;opt.delay_offset=(mix>>3)&1;
 opt.bp_left=(mix>>4)&1;opt.bp_edge=(mix>>5)&1;opt.bp_sad=(mix>>6)&1;
 opt.stats=&stats;opt.trace=ignore_trace;
 dsc_decode_frame_ex(&c,&opt,data+128,size-128,out,4096*3);
 free(out);return 0;
}
