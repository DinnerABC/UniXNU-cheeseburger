// SPDX-License-Identifier: MIT
#include "NativeMetal540DesktopSession.h"
#include <cassert>
#include <cstring>
#include <map>
#include <vector>
#include <cstdio>
struct Model {
    uint64_t next=0,fence=0;unsigned calls=0,freeCalls=0,mapCalls=0,flushes=0;
    int failAt=0;bool malformed=false;
    std::map<uint64_t,std::vector<uint32_t>> data;
};
static int call(void *opaque,uint32_t selector,const void *in,size_t,void *out,size_t *bytes) {
    auto &m=*static_cast<Model *>(opaque);if(++m.calls==unsigned(m.failAt))return -91;
    if(selector==NativeAdrenoMethodQuery){*static_cast<NativeAdrenoAbiParams *>(out)={1,36,36,1,1ull<<28};*bytes=sizeof(NativeAdrenoAbiParams);return 0;}
    if(selector==NativeAdrenoMethodGetDeviceInfo){NativeAdrenoAbiDeviceInfo info{};info.chip_id=0x05040001;info.flags=1;info.va_start=0x40000000;info.va_size=1ull<<28;*static_cast<NativeAdrenoAbiDeviceInfo *>(out)=info;*bytes=sizeof info;return 0;}
    if(selector==NativeAdrenoMethodBoAlloc||selector==NativeAdrenoMethodBoImport){uint64_t size=selector==NativeAdrenoMethodBoAlloc?static_cast<const NativeAdrenoAbiBoAlloc *>(in)->bytes:static_cast<const NativeAdrenoAbiBoImport *>(in)->bytes;
        uint64_t h=++m.next;m.data[h].resize(size/4);*static_cast<NativeAdrenoAbiBoAllocReply *>(out)={h};*bytes=sizeof(NativeAdrenoAbiBoAllocReply);return 0;}
    if(selector==NativeAdrenoMethodBoMap){*static_cast<NativeAdrenoAbiBoMapReply *>(out)={static_cast<const NativeAdrenoAbiBoMap *>(in)->iova};*bytes=sizeof(NativeAdrenoAbiBoMapReply);return 0;}
    if(selector==NativeAdrenoMethodBoCpuMap){uint64_t h=static_cast<const NativeAdrenoAbiBoCpuMap *>(in)->handle;*static_cast<NativeAdrenoAbiBoCpuMapReply *>(out)={uint32_t(h),0,m.data[h].size()*4};*bytes=sizeof(NativeAdrenoAbiBoCpuMapReply);return 0;}
    if(selector==NativeAdrenoMethodBoCpuUnmap){*bytes=0;return 0;}
    if(selector==NativeAdrenoMethodBoFree){++m.freeCalls;m.data.erase(static_cast<const NativeAdrenoAbiBoFree *>(in)->handle);*bytes=0;return 0;}
    if(selector==NativeAdrenoMethodSubmit){*static_cast<NativeAdrenoAbiSubmitReply *>(out)={m.malformed?0:++m.fence};*bytes=sizeof(NativeAdrenoAbiSubmitReply);return 0;}
    if(selector==NativeAdrenoMethodWaitFence){*bytes=0;return 0;}
    assert(false);return -99;
}
static NativeMetal540DesktopSession::Transport transport(Model &m) {
    NativeMetal540DesktopSession::Transport t{};t.call=call;t.context=&m;
    t.mapView=[](void *p,uint32_t token,void **cpu,size_t *bytes){auto &m=*static_cast<Model *>(p);++m.mapCalls;*cpu=m.data[token].data();*bytes=m.data[token].size()*4;return 0;};
    t.unmapView=[](void *,uint32_t,void *,size_t){return 0;};
    t.flush=[](void *p,void *,size_t,bool){++static_cast<Model *>(p)->flushes;return 0;};return t;
}
int main() {
    Model m;NativeMetal540DesktopSession session(transport(m));assert(!session.initialize());
    using Resource=NativeMetal540DesktopSession::Resource;Resource ib{},target{};
    assert(!session.allocate(65536,ib)&&!session.allocate(8294400,target));
    assert(session.allocatedBytes()==65536+8294400);uint64_t ids[]={ib.id,target.id},fence=0;
    assert(!session.submit(ib.id,16,ids,2,fence));assert(session.release(target.id)==NativeMetal540DesktopSession::Uncertain);
    Resource pending{};assert(session.allocate(4096,pending)==NativeMetal540DesktopSession::Invalid);
    assert(!session.resource(ib.id,pending));
    assert(!session.wait(fence,200000000)&&!session.release(target.id));
    uint64_t previous=target.id,base=target.iova;
    for(unsigned i=0;i<2000;++i){assert(!session.allocate(8294400,target)&&target.iova==base&&target.id>previous);previous=target.id;
        ids[1]=target.id;assert(!session.submit(ib.id,16,ids,2,fence));assert(!session.wait(fence,200000000));assert(!session.release(target.id));}
    assert(!session.release(ib.id)&&session.allocatedBytes()==0&&!session.lost());
    assert(session.release(previous)==NativeMetal540DesktopSession::Invalid);
    for(unsigned fail=4;fail<=6;++fail){Model bad;NativeMetal540DesktopSession s(transport(bad));assert(!s.initialize());bad.failAt=fail;Resource r{};assert(s.allocate(4096,r));assert(s.lost()&&bad.freeCalls==0);}
    Model timeout;NativeMetal540DesktopSession failed(transport(timeout));assert(!failed.initialize());assert(!failed.allocate(4096,ib));uint64_t one[]={ib.id};
    assert(!failed.submit(ib.id,3,one,1,fence));timeout.failAt=timeout.calls+1;assert(failed.wait(fence,200000000));assert(failed.release(ib.id)==NativeMetal540DesktopSession::Uncertain&&timeout.freeCalls==0);
    Model malformed;NativeMetal540DesktopSession broken(transport(malformed));assert(!broken.initialize()&&!broken.allocate(4096,ib));malformed.malformed=true;one[0]=ib.id;
    assert(broken.submit(ib.id,3,one,1,fence)&&broken.lost()&&malformed.freeCalls==0);
    std::puts("PASS desktop BO lifecycle: 2000 reuse rounds, full-HD allocation, stale IDs, pending/failed fence retention; host model only");
}
