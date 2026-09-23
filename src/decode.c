/* SPDX-License-Identifier: MIT
 * DSC 1.1 prose-based decoder. Clause references and unresolved ambiguities
 * are documented in RESEARCH.md. This file contains no reference-model code.
 */
#include "dsc.h"
#include "predict.h"
#include "rate_control.h"
#include <stdlib.h>
#include <string.h>
struct reservoir { uint8_t bits[96]; unsigned count, read; };
struct syntax_state {
 struct reservoir s[3];
 unsigned predicted[3], last_level[3];
 int was_ich, flat_flag, flat_type;
 size_t flat_group;
};
static unsigned bound(int n,unsigned hi) {return n<0?0:(unsigned)n>hi?hi:(unsigned)n;}
static int validate(const struct drm_dsc_config *c)
{
 unsigned i;
 if(!c)return DSC_INVALID;
 if(c->dsc_version_major!=1 || c->dsc_version_minor!=1 || c->bits_per_component!=8 || c->vbr_enable ||
    !c->convert_rgb || c->simple_422 || c->native_422 || c->native_420)return DSC_UNSUPPORTED;
 if(!c->slice_width || !c->slice_height || !c->pic_width || !c->pic_height ||
    !c->slice_chunk_size || !c->bits_per_pixel || c->bits_per_pixel>384 ||
    c->line_buf_depth<8 || c->line_buf_depth>13 || !c->rc_model_size ||
    c->initial_scale_value<8 || c->initial_scale_value>63 ||
    (c->initial_scale_value>8 && !c->scale_decrement_interval) ||
    c->initial_offset>c->rc_model_size || c->final_offset>c->rc_model_size ||
    c->flatness_min_qp>c->flatness_max_qp || c->flatness_max_qp>15 ||
    c->rc_quant_incr_limit0>15 || c->rc_quant_incr_limit1>15 ||
    c->rc_edge_factor>15 || c->rc_tgt_offset_high>15 || c->rc_tgt_offset_low>15)
  return DSC_INVALID;
 if((size_t)c->slice_width*c->slice_height>DSC_MAX_PIXELS ||
    (size_t)c->pic_width*c->pic_height>DSC_MAX_PIXELS)return DSC_LIMIT;
 if(c->slice_chunk_size != ((unsigned)c->slice_width*c->bits_per_pixel+127)/128)return DSC_INVALID;
 for(i=0;i<14;i++)if(c->rc_buf_thresh[i]>255 ||
   (i && c->rc_buf_thresh[i]<=c->rc_buf_thresh[i-1]) ||
   c->rc_buf_thresh[i]*64u>=c->rc_model_size)return DSC_INVALID;
 for(i=0;i<15;i++)if(c->rc_range_params[i].range_min_qp>c->rc_range_params[i].range_max_qp ||
    c->rc_range_params[i].range_max_qp>15 || c->rc_range_params[i].range_bpg_offset>63)return DSC_INVALID;
 return DSC_OK;
}
/* Table 4-6: at most one 48-bit word per component before each group.
 * 8-bit RGB maximum syntax sizes are 36 bits for all three components.
 */
static int refill(struct reservoir *r,const uint8_t *p,size_t n,size_t *pos)
{
 unsigned i,left=r->count-r->read;
 if(left>=36)return 0;
 if(*pos>n || n-*pos<6)return -1;
 memmove(r->bits,r->bits+r->read,left);
 for(i=0;i<48;i++)r->bits[left+i]=(p[*pos+i/8]>>(7-i%8))&1;
 *pos+=6;r->read=0;r->count=left+48;
 return 0;
}
static int take(struct reservoir *r,unsigned n,unsigned *v)
{
 unsigned k,x=0;
 if(n>16 || r->count-r->read<n)return -1;
 for(k=0;k<n;k++)x=x*2+r->bits[r->read++];
 *v=x;return 0;
}
static unsigned signed_size(int n)
{
 unsigned w;
 if(!n)return 0;
 for(w=1;w<=10;w++)if(n>=-(1<<(w-1)) && n<(1<<(w-1)))return w;
 return 11;
}
static int syntax(struct syntax_state *s,const struct drm_dsc_config *c,
 unsigned group,unsigned qp,unsigned level[3],int res[3][3],int mpp[3],
 int *ich,unsigned idx[3],unsigned *actual,unsigned *ideal)
{
 static const unsigned luma[16]={0,0,0,1,1,2,2,3,3,4,4,5,5,5,6,7};
 static const unsigned chroma[16]={0,1,2,2,3,3,4,4,5,5,6,6,7,8,8,8};
 unsigned start=0,j,k,v,z,max,pred,width,required[3],largest;
 if(qp>15)return -1;
 for(j=0;j<3;j++)start+=s->s[j].read;
 if(group%4==3) {
  s->flat_flag=0;
  if(qp>=c->flatness_min_qp && qp<=c->flatness_max_qp) {
   if(take(&s->s[0],1,&v))return -1;
   s->flat_flag=(int)v;
  }
 } else if(group%4==0 && s->flat_flag) {
  s->flat_type=0;
  if(qp>=7) {if(take(&s->s[0],1,&v))return -1;s->flat_type=(int)v;}
  if(take(&s->s[0],2,&v))return -1;
  /* 6.6.3: supergroup starts one group after metadata. */
  s->flat_group=(size_t)group+1+v;
 }
 *ich=0;*ideal=0;
 for(j=0;j<3;j++) {
  level[j]=j?chroma[qp]:luma[qp];
  max=(j?9:8)-level[j];
  pred=bound((int)s->predicted[j]+(int)s->last_level[j]-(int)level[j],max-1);
  if(j==0 || !*ich) {
   unsigned limit=max-pred+(j==0);
   for(z=0;z<limit;z++){if(take(&s->s[j],1,&v))return -1;if(v)break;}
   width=pred+z;
   if(j==0) {
    if(s->was_ich) {if(z==0)*ich=1;else width--;}
    else if(width==max+1)*ich=1;
   }
   if(!*ich && width>max)return -1;
  } else width=0;
  if(*ich) {
   if(take(&s->s[j],5,&idx[j]))return -1;
   mpp[j]=0;
  } else {
   mpp[j]=width==max;
   largest=0;
   for(k=0;k<3;k++) {
    if(take(&s->s[j],width,&v))return -1;
    res[j][k]=(int)v;
    if(width && (v&(1u<<(width-1))))res[j][k]-=1<<width;
    required[k]=mpp[j]?max:signed_size(res[j][k]);
    if(required[k]>largest)largest=required[k];
   }
   s->predicted[j]=(required[0]+required[1]+2*required[2]+2)/4;
   *ideal+=3*largest+1;
  }
  s->last_level[j]=level[j];
 }
 if(*ich)*ideal=16;
 s->was_ich=*ich;
 *actual=0;for(j=0;j<3;j++)*actual+=s->s[j].read;
 *actual-=start;
 return 0;
}
static int half_floor(int v) {return v>=0?v/2:-((-v+1)/2);}
static uint8_t byte(int v){return (uint8_t)bound(v,255);}
static int decode_slice(const struct drm_dsc_config *c,const struct dsc_options *opt,unsigned slice,
 const uint8_t *p,size_t n,uint8_t *rgb,size_t cap)
{
 struct syntax_state s={0};
 struct dsc_rc rc;
 struct dsc_predict *pred;
 struct dsc_group_trace tr;
 size_t pos=0,expected;
 unsigned x,y,g=0,j,k,level[3],idx[3],actual,ideal,qp;
 int res[3][3],mpp[3],ich,status=validate(c);
 uint16_t pixel[3][3];
 if(status)return status;
 if(!p || !rgb)return DSC_INVALID;
 if(cap<(size_t)c->slice_width*c->slice_height*3)return DSC_LIMIT;
 expected=(size_t)c->slice_chunk_size*c->slice_height;
 if(!c->vbr_enable && n!=expected)return n<expected?DSC_TRUNCATED:DSC_INVALID;
 if(c->vbr_enable && n>expected)return DSC_INVALID;
 if(dsc_rc_init(&rc,c))return DSC_INVALID;
 dsc_rc_set_options(&rc,opt);
 pred=dsc_predict_create(c->slice_width,c->slice_height,c->line_buf_depth,c->block_pred_enable,
                        c->pic_width!=c->slice_width);
 if(!pred)return DSC_NOMEM;
 dsc_predict_set_options(pred,opt);
 s.flat_group=(size_t)-1;
 for(y=0;y<c->slice_height;y++)for(x=0;x<c->slice_width;x+=3,g++) {
  unsigned count=c->slice_width-x<3?c->slice_width-x:3;
  for(j=0;j<3;j++)if(refill(&s.s[j],p,n,&pos)){status=DSC_TRUNCATED;goto done;}
  if(dsc_rc_apply_flat(&rc,s.flat_group==g,s.flat_type)){status=DSC_RATE_CONTROL;goto done;}
  memset(res,0,sizeof(res));memset(idx,0,sizeof(idx));
  qp=dsc_rc_qp(&rc);
  if(syntax(&s,c,g,qp,level,res,mpp,&ich,idx,&actual,&ideal)) {
   status=DSC_BITSTREAM;goto done;
  }
  /* Section 6.6: partial groups carry zero residuals or repeat the
   * rightmost real ICH index. These bits affect entropy state even though
   * they do not produce pixels, so validate them before reconstruction. */
  for(k=count;k<3;k++) {
   if(ich) {
    if(idx[k]!=idx[count-1]){status=DSC_BITSTREAM;goto done;}
   } else for(j=0;j<3;j++)
    if(res[j][k]){status=DSC_BITSTREAM;goto done;}
  }
  if(dsc_predict_group(pred,x,y,level,(const int (*)[3])res,mpp,ich,idx,pixel)) {status=DSC_BITSTREAM;goto done;}
  for(k=0;k<count;k++) {
   int co=(int)pixel[1][k]-256,cg=(int)pixel[2][k]-256;
   int t=(int)pixel[0][k]-half_floor(cg),b=t-half_floor(co);
   size_t o=((size_t)y*c->slice_width+x+k)*3;
   rgb[o]=byte(co+b);rgb[o+1]=byte(cg+t);rgb[o+2]=byte(b);
  }
  if(dsc_rc_step(&rc,y,g,count,actual,ideal)){status=DSC_RATE_CONTROL;goto done;}
  if(opt->trace) {
   tr.slice=slice;tr.group=g;tr.x=x;tr.y=y;tr.qp=qp;tr.actual=actual;tr.ideal=ideal;
   tr.range=rc.range;tr.generated_qp=rc.last_qp;tr.buffer_fullness=rc.fullness;
   tr.model_fullness=rc.last_inputs.model;tr.ich=ich;tr.flat_override=rc.flat_override;
   opt->trace(opt->trace_context,&tr);
  }
 }
 /* Residual funnel and CBR slice-tail padding must be zero (6.7.4). */
 for(j=0;j<3;j++)for(k=s.s[j].read;k<s.s[j].count;k++)
  if(s.s[j].bits[k]){status=DSC_BITSTREAM;goto done;}
 if(c->vbr_enable && pos!=n){status=DSC_INVALID;goto done;}
 for(;pos<n;pos++)if(p[pos]){status=DSC_BITSTREAM;goto done;}
 status=DSC_OK;
 done:dsc_predict_destroy(pred);return status;
}
int dsc_decode_slice_ex(const struct drm_dsc_config *c,const struct dsc_options *opt,
 const uint8_t *p,size_t n,uint8_t *rgb,size_t cap)
{
 struct dsc_options defaults;
 if(!opt){dsc_options_init(&defaults);opt=&defaults;}
 return decode_slice(c,opt,0,p,n,rgb,cap);
}
int dsc_decode_slice(const struct drm_dsc_config *c,const uint8_t *p,size_t n,uint8_t *rgb,size_t cap)
{
 return dsc_decode_slice_ex(c,NULL,p,n,rgb,cap);
}
int dsc_decode_frame_ex(const struct drm_dsc_config *c,const struct dsc_options *opt,
 const uint8_t *data,size_t n,uint8_t *rgb,size_t cap)
{
 struct dsc_options defaults;
 unsigned nx,ny,sx,sy,y;
 size_t bytes,pixels,expected;
 uint8_t *slice,*decoded;
 int status=validate(c);
 if(status)return status;
 if(!opt){dsc_options_init(&defaults);opt=&defaults;}
 if(c->vbr_enable)return DSC_UNSUPPORTED;
 if(!data||!rgb)return DSC_INVALID;
 if(cap<(size_t)c->pic_width*c->pic_height*3)return DSC_LIMIT;
 nx=((unsigned)c->pic_width+c->slice_width-1)/c->slice_width;
 ny=((unsigned)c->pic_height+c->slice_height-1)/c->slice_height;
 bytes=(size_t)c->slice_chunk_size*c->slice_height;
 pixels=(size_t)c->slice_width*c->slice_height*3;
 if(nx>255 || bytes>SIZE_MAX/nx/ny)return DSC_LIMIT;
 expected=bytes*nx*ny;
 if(n!=expected)return n<expected?DSC_TRUNCATED:DSC_INVALID;
 slice=malloc(bytes);decoded=malloc(pixels);
 if(!slice||!decoded){free(slice);free(decoded);return DSC_NOMEM;}
 for(sy=0;sy<ny;sy++)for(sx=0;sx<nx;sx++) {
  for(y=0;y<c->slice_height;y++) {
   size_t source=((size_t)sy*c->slice_height*nx+(size_t)y*nx+sx)*c->slice_chunk_size;
   memcpy(slice+(size_t)y*c->slice_chunk_size,data+source,c->slice_chunk_size);
  }
  status=decode_slice(c,opt,sy*nx+sx,slice,bytes,decoded,pixels);
  if(status)goto done;
  for(y=0;y<c->slice_height && sy*c->slice_height+y<c->pic_height;y++) {
   unsigned width=c->pic_width-sx*c->slice_width;
   if(width>c->slice_width)width=c->slice_width;
   memcpy(rgb+((size_t)(sy*c->slice_height+y)*c->pic_width+sx*c->slice_width)*3,
          decoded+(size_t)y*c->slice_width*3,(size_t)width*3);
  }
 }
 done:free(slice);free(decoded);return status;
}
int dsc_decode_frame(const struct drm_dsc_config *c,const uint8_t *data,size_t n,uint8_t *rgb,size_t cap)
{
 return dsc_decode_frame_ex(c,NULL,data,n,rgb,cap);
}
