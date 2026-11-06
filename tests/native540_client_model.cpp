// SPDX-License-Identifier: MIT
#include "../package/Userland/NativeMetal540Client.h"
#include <cassert>
#include <cstring>
#include <iostream>
struct Model { int calls=0,fail=0;bool shortReply=false;uint64_t seq=0;NativeAdrenoAbiSubmit last{}; };
static int call(void *ctx,uint32_t method,const void *in,size_t n,void *out,size_t *bytes) {
    auto &m=*static_cast<Model *>(ctx);++m.calls;if(m.fail)return m.fail;
    if(method==NativeAdrenoMethodQuery){assert(!in&&!n&&*bytes==24);*static_cast<NativeAdrenoAbiParams *>(out)={1,36,36,0,1ull<<28};}
    else if(method==NativeAdrenoMethodBoAlloc){assert(n==16);*static_cast<NativeAdrenoAbiBoAllocReply *>(out)={1};}
    else if(method==NativeAdrenoMethodBoMap){assert(n==32);auto &r=*static_cast<const NativeAdrenoAbiBoMap *>(in);assert(!r.reserved&&r.shareability==2&&!r.writeback);*static_cast<NativeAdrenoAbiBoMapReply *>(out)={r.iova};}
    else if(method==NativeAdrenoMethodSubmit){assert(n==416);m.last=*static_cast<const NativeAdrenoAbiSubmit *>(in);assert(!m.last.reserved&&!m.last.queue&&!m.last.wait_fence);*static_cast<NativeAdrenoAbiSubmitReply *>(out)={++m.seq};}
    else if(method==NativeAdrenoMethodWaitFence){assert(n==16&&!*bytes);}
    else assert(false);
    if(m.shortReply&&*bytes)--*bytes;return 0;
}
int main(){
    Model m;NativeMetal540Client c(call,&m);uint64_t handle=99,mapped=99,fence=99;
    assert(c.allocate(4096,handle)==c.Invalid&&!handle&&!m.calls);
    assert(!c.query()&&c.ready());int calls=m.calls;
    for(uint64_t n:{0ull,1ull,4095ull,(1ull<<28)+4096})assert(c.allocate(n,handle)==c.Invalid&&!handle);
    assert(m.calls==calls);assert(!c.allocate(4096,handle)&&handle==1);
    assert(c.map(handle,4097,1,mapped)==c.Invalid&&!mapped);
    assert(c.map(handle,4096,2,mapped)==c.Invalid&&!mapped);
    assert(!c.map(handle,4096,3,mapped)&&mapped==4096);
    NativeAdrenoAbiSubmitBuffer b{handle,mapped,12};assert(!c.submit(&b,1,fence)&&fence==1);
    for(unsigned i=1;i<16;++i)assert(!m.last.buffers[i].handle&&!m.last.buffers[i].bytes);
    assert(!c.wait(fence,1000000));assert(c.wait(2,0)==c.Invalid);
    b.bytes=3;calls=m.calls;assert(c.submit(&b,1,fence)==c.Invalid&&!fence&&calls==m.calls);
    b.bytes=12;assert(!c.submit(&b,1,fence));m.fail=-7;assert(c.wait(fence,0)==-7&&!c.ready());
    calls=m.calls;assert(c.submit(&b,1,fence)==c.Invalid&&m.calls==calls);
    Model shortM;shortM.shortReply=true;NativeMetal540Client s(call,&shortM);assert(s.query()==s.Invalid&&!s.ready());
    uint32_t w[4]={0xdeadbeef,0xdeadbeef,0xdeadbeef,0xdeadbeef};assert(!NativeMetal540EncodeBarrier(w,2)&&w[0]==0xdeadbeef);
    assert(NativeMetal540EncodeBarrier(w,4)==3&&w[0]==0x70268000&&w[1]==0x70100001&&w[2]==0&&w[3]==0xdeadbeef);
    std::cout<<"NativeMetal540 ABI, refusal, quarantine and A540 packet tests PASS\n";
}
