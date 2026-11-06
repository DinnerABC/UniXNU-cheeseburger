// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540RenderAIRProbe.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using P=NativeMetal540RenderAIRProbe;
struct Model {
    struct Bo{uint64_t iova=0,bytes=0;std::vector<uint32_t> words;};
    std::array<Bo,7> bos{};unsigned next=0,events=0,failAt=0,calls=0,badReply=0,submits=0,waits=0,reads=0;
    bool timeout=false,draw=true,stamp=true,duplicate=false;
    int event(){return ++events==failAt?-77:0;}
    static int call(void *c,uint32_t s,const void *in,size_t n,void *out,size_t *bytes){
        auto &m=*(Model *)c;++m.calls;if(int rc=m.event())return rc;
        auto reply=[&](const auto &r){assert(out&&*bytes==sizeof(r));memcpy(out,&r,sizeof(r));if(m.calls==m.badReply)--*bytes;return 0;};
        if(s==NativeAdrenoMethodQuery)return reply(NativeAdrenoAbiParams{1,36,36,1,1u<<28});
        if(s==NativeAdrenoMethodGetDeviceInfo){NativeAdrenoAbiDeviceInfo r{};r.chip_id=0x05040001;r.va_start=0x40000000;r.va_size=0x1000000;r.flags=1;return reply(r);}
        if(s==NativeAdrenoMethodBoAlloc){auto a=(const NativeAdrenoAbiBoAlloc*)in;assert(m.next<7);auto &b=m.bos[m.next++];b.bytes=a->bytes;b.words.resize(b.bytes/4);return reply(NativeAdrenoAbiBoAllocReply{m.duplicate&&m.next==2?1:m.next});}
        if(s==NativeAdrenoMethodBoMap){auto a=(const NativeAdrenoAbiBoMap*)in;m.bos[a->handle-1].iova=a->iova;return reply(NativeAdrenoAbiBoMapReply{a->iova});}
        if(s==NativeAdrenoMethodBoCpuMap){auto a=(const NativeAdrenoAbiBoCpuMap*)in;return reply(NativeAdrenoAbiBoCpuMapReply{(uint32_t)a->handle,0,m.bos[a->handle-1].bytes});}
        if(s==NativeAdrenoMethodSubmit){auto a=(const NativeAdrenoAbiSubmit*)in;assert(n==sizeof(*a)&&m.next==7&&a->buffer_count==1&&a->buffers[0].handle==1);++m.submits;
            for(auto w:m.bos[3].words)assert(w==Native540Render::Sentinel);
            assert(m.bos[5].words[0]==0xbf400000&&m.bos[5].words[11]==0x3f800000&&m.bos[5].words[12]==Native540Render::Sentinel);
            assert(m.bos[6].words[0]==0x3f800000&&m.bos[6].words[3]==0x3f800000&&m.bos[6].words[4]==Native540Render::Sentinel);
            return reply(NativeAdrenoAbiSubmitReply{19});}
        if(s==NativeAdrenoMethodWaitFence){auto a=(const NativeAdrenoAbiWaitFence*)in;assert(a->fence==19&&a->timeout_ns==200000000&&!*bytes);++m.waits;if(m.timeout)return -88;
            // SOFTWARE transport model ONLY, never claims AIR or GPU execution.
            if(m.draw)for(int y=0;y<64;++y)for(int x=0;x<64;++x){int X=2*x+1,Y=2*y+1;m.bos[3].words[y*64+x]=(Y<112&&2*X+Y>144&&112-2*X+Y>0)?Native540Render::Center:Native540Render::Background;}
            if(m.stamp){m.bos[4].words[0]=1;m.bos[4].words[2]=2;m.bos[4].words[4]=3;}
            if(m.calls==m.badReply)*bytes=1;return 0;}
        assert(false);return -99;
    }
    static int map(void *c,uint32_t t,uint64_t &a,uint64_t &n){auto &m=*(Model*)c;if(int rc=m.event())return rc;assert(t&&t<=7);a=(uintptr_t)m.bos[t-1].words.data();n=m.bos[t-1].bytes;return 0;}
    static void order(void *c,bool before){auto &m=*(Model*)c;if(!before){assert(m.waits==1&&!m.timeout);++m.reads;}}
    P::Transport transport(){P::Transport t{};t.call=call;t.mapView=map;t.order=order;t.context=this;return t;}
};
int main(){
    Model base;P p(base.transport());assert(!p.run(true));auto r=p.result();assert(base.submits==1&&base.waits==1&&base.reads==1);
    assert(r.stage==P::Complete&&!r.error&&!r.pixels.mismatches&&r.pixels.inside==968&&r.pixels.outside==2744);
    assert(r.inputVerified==12&&r.inputGuardVerified==1012&&r.colorVerified==4&&r.colorGuardVerified==1020);
    for(unsigned point=1;point<=base.events;++point){Model m;m.failAt=point;P probe(m.transport());assert(probe.run(true)==-77);unsigned calls=m.calls;assert(probe.run(true)==P::AlreadyAttempted&&m.calls==calls);if(!m.waits)assert(!m.reads);}
    for(unsigned point=1;point<=base.calls;++point){Model m;m.badReply=point;P probe(m.transport());assert(probe.run(true)<0);}
    for(unsigned mode=0;mode<4;++mode){Model m;if(mode==0)m.timeout=true;if(mode==1)m.draw=false;if(mode==2)m.stamp=false;if(mode==3)m.duplicate=true;
        P probe(m.transport());assert(probe.run(true)<0);if(mode==0)assert(m.waits==1&&!m.reads);}
    Model m;P probe(m.transport());assert(probe.run(false)==P::Prerequisite&&!m.calls);
    std::cout<<"PASS runtime/failures "<<base.events<<" malformed "<<base.calls<<"; HOST SOFTWARE MODEL NOT GPU execution\n";
}
