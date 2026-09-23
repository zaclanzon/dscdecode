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
int main(int argc,char **argv)
{
 int single=0,status,exitcode=1,off=1;
 unsigned w,h;
 size_t pps_n,n,cap;
 struct drm_dsc_config c;
 uint8_t *pps=NULL,*input=NULL,*rgb=NULL;
 FILE *output=NULL;
 if(argc==5 && !strcmp(argv[1],"--slice")){single=1;off=2;}
 else if(argc!=4){fprintf(stderr,"Usage: %s [--slice] PPS.bin slices.bin output.ppm\n",argv[0]);return 2;}
 pps=read_file(argv[off],128,&pps_n);if(!pps)goto done;
 status=dsc_parse_pps(pps,pps_n,&c);
 if(status){fprintf(stderr,"PPS: %s\n",dsc_strerror(status));goto done;}
 w=single?c.slice_width:c.pic_width;h=single?c.slice_height:c.pic_height;
 if((size_t)w*h>DSC_MAX_PIXELS){fprintf(stderr,"image exceeds pixel limit\n");goto done;}
 cap=(size_t)w*h*3;
 input=read_file(argv[off+1],256u*1024u*1024u,&n);if(!input)goto done;
 rgb=malloc(cap);if(!rgb)goto done;
 status=single?dsc_decode_slice(&c,input,n,rgb,cap):dsc_decode_frame(&c,input,n,rgb,cap);
 if(status){fprintf(stderr,"decode: %s\n",dsc_strerror(status));goto done;}
 /* Only open output after successful validation and decoding. */
 output=fopen(argv[off+2],"wb");if(!output){perror(argv[off+2]);goto done;}
 if(fprintf(output,"P6\n%u %u\n255\n",w,h)<0 || fwrite(rgb,1,cap,output)!=cap){fprintf(stderr,"output write failed\n");goto done;}
 if(fclose(output)){output=NULL;fprintf(stderr,"output close failed\n");goto done;}
 output=NULL;exitcode=0;
 done:if(output)fclose(output);free(pps);free(input);free(rgb);return exitcode;
}
