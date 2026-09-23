/* SPDX-License-Identifier: MIT */
#include "dsc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Input files need not be seekable. Limit both allocation and read work. */
static uint8_t *read_file(const char *name,size_t limit,size_t *length)
{
 FILE *f=fopen(name,"rb");
 size_t n=0,cap=limit<4096?limit:4096;
 uint8_t *p;
 if(!f){perror(name);return NULL;}
 p=malloc(cap?cap:1);
 if(!p){fclose(f);return NULL;}
 for(;;) {
  size_t got;
  if(n==cap) {
   size_t next=cap>limit/2?limit:cap*2;
   uint8_t *q;
   if(cap==limit) {
    if(fgetc(f)!=EOF || ferror(f)){fprintf(stderr,"%s: input too large or unreadable\n",name);free(p);fclose(f);return NULL;}
    break;
   }
   q=realloc(p,next);if(!q){free(p);fclose(f);return NULL;}p=q;cap=next;
  }
  got=fread(p+n,1,cap-n,f);n+=got;
  if(!got){if(ferror(f)){perror(name);free(p);fclose(f);return NULL;}break;}
 }
 if(fclose(f)){free(p);return NULL;}*length=n;return p;
}
/* Interpretation switches: RESEARCH.md, open questions. */
struct reading {
 const char *name,*value;int *(*field)(struct dsc_options *);int code;
};
static int *flat_restart(struct dsc_options *o){return &o->flat_restart;}
static int *threshold_eq(struct dsc_options *o){return &o->threshold_eq;}
static int *frac_reset(struct dsc_options *o){return &o->frac_reset;}
static int *delay_offset(struct dsc_options *o){return &o->delay_offset;}
static int *bp_left(struct dsc_options *o){return &o->bp_left;}
static int *bp_edge(struct dsc_options *o){return &o->bp_edge;}
static int *bp_sad(struct dsc_options *o){return &o->bp_sad;}
static const struct reading readings[]={
 {"flat_restart","next-cycle",flat_restart,DSC_FLAT_RESTART_NEXT_CYCLE},
 {"flat_restart","in-flight",flat_restart,DSC_FLAT_RESTART_IN_FLIGHT},
 {"threshold_eq","lower",threshold_eq,DSC_THRESHOLD_EQ_LOWER},
 {"threshold_eq","upper",threshold_eq,DSC_THRESHOLD_EQ_UPPER},
 {"frac_reset","chunk",frac_reset,DSC_FRAC_RESET_CHUNK},
 {"frac_reset","literal",frac_reset,DSC_FRAC_RESET_LITERAL},
 {"delay_offset","inclusive",delay_offset,DSC_DELAY_OFFSET_INCLUSIVE},
 {"delay_offset","exclusive",delay_offset,DSC_DELAY_OFFSET_EXCLUSIVE},
 {"bp_left","replicate",bp_left,DSC_BP_LEFT_REPLICATE},
 {"bp_left","midpoint",bp_left,DSC_BP_LEFT_MIDPOINT},
 {"bp_edge","window",bp_edge,DSC_BP_EDGE_WINDOW},
 {"bp_edge","before",bp_edge,DSC_BP_EDGE_BEFORE},
 {"bp_sad","shift",bp_sad,DSC_BP_SAD_SHIFT},
 {"bp_sad","clip",bp_sad,DSC_BP_SAD_CLIP},
};
static const struct reading *find_reading(const char *name,const char *value)
{
 size_t i;
 for(i=0;i<sizeof(readings)/sizeof(readings[0]);i++)
  if(!strcmp(readings[i].name,name) && !strcmp(readings[i].value,value))return &readings[i];
 return NULL;
}
static int set_reading(struct dsc_options *o,const char *arg)
{
 char name[32];
 const char *eq=strchr(arg,'=');
 const struct reading *r;
 size_t len=eq?(size_t)(eq-arg):0;
 if(!eq || len>=sizeof(name))return -1;
 memcpy(name,arg,len);name[len]=0;
 r=find_reading(name,eq+1);
 if(!r)return -1;
 *r->field(o)=r->code;
 return 0;
}
static void trace_csv(void *context,const struct dsc_group_trace *t)
{
 fprintf((FILE *)context,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%lld,%lld,%d,%d\n",t->slice,t->group,t->x,t->y,
  t->qp,t->actual,t->ideal,t->range,t->generated_qp,t->buffer_fullness,t->model_fullness,t->ich,t->flat_override);
}
static int usage(const char *self)
{
 size_t i;
 struct dsc_options defaults;
 dsc_options_init(&defaults);
 fprintf(stderr,"Usage: %s [--slice] [--stats] [--trace FILE.csv] [--reading NAME=VALUE]... PPS.bin slices.bin output.ppm\n"
  "Readings (see RESEARCH.md, open questions):\n",self);
 for(i=0;i<sizeof(readings)/sizeof(readings[0]);i++)
  fprintf(stderr,"  %s=%s%s\n",readings[i].name,readings[i].value,
   *readings[i].field(&defaults)==readings[i].code?" (default)":"");
 return 2;
}
int main(int argc,char **argv)
{
 int single=0,want_stats=0,status,exitcode=1,off=1;
 unsigned w,h;
 size_t pps_n,n,cap;
 struct drm_dsc_config c;
 struct dsc_options opt;
 struct dsc_stats stats={0};
 uint8_t *pps=NULL,*input=NULL,*rgb=NULL;
 FILE *output=NULL,*trace=NULL;
 dsc_options_init(&opt);
 for(;off<argc && !strncmp(argv[off],"--",2);off++) {
  if(!strcmp(argv[off],"--slice"))single=1;
  else if(!strcmp(argv[off],"--stats"))want_stats=1;
  else if(!strcmp(argv[off],"--reading") && off+1<argc) {
   if(set_reading(&opt,argv[++off])){fprintf(stderr,"unknown reading: %s\n",argv[off]);return usage(argv[0]);}
  } else if(!strcmp(argv[off],"--trace") && off+1<argc) {
   if(trace)fclose(trace);
   trace=fopen(argv[++off],"w");
   if(!trace){perror(argv[off]);return 1;}
  } else {if(trace)fclose(trace);return usage(argv[0]);}
 }
 if(argc-off!=3){if(trace)fclose(trace);return usage(argv[0]);}
 if(want_stats)opt.stats=&stats;
 if(trace) {
  fputs("slice,group,x,y,qp,actual,ideal,range,generated_qp,buffer_fullness,model_fullness,ich,flat_override\n",trace);
  opt.trace=trace_csv;opt.trace_context=trace;
 }
 pps=read_file(argv[off],128,&pps_n);if(!pps)goto done;
 status=dsc_parse_pps(pps,pps_n,&c);
 if(status){fprintf(stderr,"PPS: %s\n",dsc_strerror(status));goto done;}
 w=single?c.slice_width:c.pic_width;h=single?c.slice_height:c.pic_height;
 if((size_t)w*h>DSC_MAX_PIXELS){fprintf(stderr,"image exceeds pixel limit\n");goto done;}
 cap=(size_t)w*h*3;
 input=read_file(argv[off+1],256u*1024u*1024u,&n);if(!input)goto done;
 rgb=malloc(cap);if(!rgb)goto done;
 status=single?dsc_decode_slice_ex(&c,&opt,input,n,rgb,cap):dsc_decode_frame_ex(&c,&opt,input,n,rgb,cap);
 if(want_stats)fprintf(stderr,"stats: groups=%lu threshold_equal=%lu flat_overrides=%lu flat_queue_differs=%lu frac_differs=%lu bp_groups=%lu bp_left_differs=%lu\n",
  stats.groups,stats.threshold_equal,stats.flat_overrides,stats.flat_queue_differs,stats.frac_differs,
  stats.bp_groups,stats.bp_left_differs);
 if(status){fprintf(stderr,"decode: %s\n",dsc_strerror(status));goto done;}
 /* Only open output after successful validation and decoding. */
 output=fopen(argv[off+2],"wb");if(!output){perror(argv[off+2]);goto done;}
 if(fprintf(output,"P6\n%u %u\n255\n",w,h)<0 || fwrite(rgb,1,cap,output)!=cap){fprintf(stderr,"output write failed\n");goto done;}
 if(fclose(output)){output=NULL;fprintf(stderr,"output close failed\n");goto done;}
 output=NULL;exitcode=0;
 done:if(output)fclose(output);if(trace && fclose(trace))exitcode=1;
 free(pps);free(input);free(rgb);return exitcode;
}
