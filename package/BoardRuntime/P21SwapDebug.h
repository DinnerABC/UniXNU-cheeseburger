#pragma once
#include <stdint.h>
typedef struct P21SwapSample {
 uint32_t serial,swap_id,mask,src[4],dst[4];
 uint32_t raw804,raw808,dummy,request_w,request_h,display_w,display_h,gop_w,gop_h;
 uint64_t self,request;
} P21SwapSample;
static inline uint32_t p21_swap_failed_fields(const P21SwapSample *s) {
 uint32_t m=0;
 if(s->src[0])m|=1; if(s->src[1])m|=2;
 if(s->src[2]!=s->display_w)m|=4; if(s->src[3]!=s->display_h)m|=8;
 if(s->dst[0])m|=16; if(s->dst[1])m|=32;
 if(s->dst[2]!=s->display_w)m|=64; if(s->dst[3]!=s->display_h)m|=128;
 if(s->raw804)m|=256; if(s->raw808)m|=512; if(s->dummy)m|=1024;
 if(s->request_w!=s->display_w)m|=2048; if(s->request_h!=s->display_h)m|=4096;
 if(!s->display_w || !s->display_h)m|=8192;
 return m;
}
#ifdef __cplusplus
extern "C" {
#endif
void p21_swap_debug_snapshot(P21SwapSample *,P21SwapSample *);
#ifdef __cplusplus
}
#endif
