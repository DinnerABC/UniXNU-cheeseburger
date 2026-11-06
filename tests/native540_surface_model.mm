// SPDX-License-Identifier: MIT
// Software transport/DMA oracle only. NEVER executes a GPU or opens a device.
#include "NativeMetal540Surface.h"
#include "render_fixture.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using S=NativeMetal540Surface;
struct Model {
    struct Bo {uint64_t iova=0,bytes=0;uint32_t perms=0;std::vector<uint32_t> data;};
    std::array<Bo,5> bos{};void *memory=nullptr;S::Layout layout{};
    unsigned next=0,events=0,failAt=0,calls=0,badReplyAt=0,views=0,creates=0,getters=0;
    unsigned holds=0,releases=0,surfaceReleases=0,imports=0,allocs=0,submits=0,waits=0,records=0;
    unsigned publish=0,acquire=0,writeLocks=0,readLocks=0,writeUnlocks=0,readUnlocks=0;
    unsigned unmappedViews=0,cpuUnmaps=0,frees=0;
    bool retired[5]={};
    bool draw=true,stamp=true,cacheCorrupt=false,mutateLocked=false,badToken=false,badView=false;
    bool duplicateHandle=false,badMap=false,badQuery=false,badDevice=false,timeout=false;
    bool writeLocked=false,readLocked=false;uint32_t hostFlags=1;
    std::vector<uint32_t> stream;
    Model() {
        assert(!posix_memalign(&memory,4096,16384));std::memset(memory,0xa5,16384);
        layout.base=layout.planeBase=(uintptr_t)memory;layout.allocation=16384;
        layout.width=layout.planeWidth=64;layout.height=layout.planeHeight=64;
        layout.pitch=layout.planePitch=256;layout.element=layout.planeElement=4;
        layout.elementWidth=layout.planeElementWidth=1;layout.elementHeight=layout.planeElementHeight=1;
        layout.surfaceID=71;layout.format=S::RGBA;
    }
    ~Model(){std::free(memory);}
    int event(){return ++events==failAt?-77:0;}
    static Model &model(void *p){return *static_cast<Model *>(p);}
    static int call(void *p,uint32_t m,const void *in,size_t n,void *out,size_t *bytes) {
        auto &s=model(p);++s.calls;if(int rc=s.event())return rc;
        if(m==NativeAdrenoMethodBoCpuUnmap){
            const auto &r=*static_cast<const NativeAdrenoAbiBoCpuUnmap *>(in);
            assert(n==sizeof(r)&&r.handle!=4&&r.token==r.handle&&!r.reserved&&!*bytes);
            ++s.cpuUnmaps;return 0;
        }
        if(m==NativeAdrenoMethodBoFree){
            const auto &r=*static_cast<const NativeAdrenoAbiBoFree *>(in);
            assert(n==sizeof(r)&&r.handle&&r.handle<=5&&!*bytes&&!s.retired[r.handle-1]);
            s.retired[r.handle-1]=true;++s.frees;return 0;
        }
        auto reply=[&](const auto &r){assert(out&&*bytes==sizeof(r));std::memcpy(out,&r,sizeof(r));
            if(s.calls==s.badReplyAt)--*bytes;return 0;};
        if(m==NativeAdrenoMethodQuery){assert(!in&&!n);return reply(NativeAdrenoAbiParams{1,s.badQuery?40u:36u,36,s.hostFlags,1u<<28});}
        if(m==NativeAdrenoMethodGetDeviceInfo){S::Layout unused{};(void)unused;NativeAdrenoAbiDeviceInfo d{};d.chip_id=s.badDevice?0x740:0x05040001;d.va_start=0x40000000;d.va_size=0x1000000;d.flags=1;return reply(d);}
        if(m==NativeAdrenoMethodBoAlloc) {
            assert(n==sizeof(NativeAdrenoAbiBoAlloc)&&s.next<5&&s.next!=3);
            const auto &r=*static_cast<const NativeAdrenoAbiBoAlloc *>(in);
            const uint64_t sizes[]={8192,4096,4096,16384,4096};assert(r.bytes==sizes[s.next]&&r.align_log2==12&&!r.contiguous);
            auto &b=s.bos[s.next];b.bytes=r.bytes;b.data.resize(r.bytes/4);++s.next;++s.allocs;
            return reply(NativeAdrenoAbiBoAllocReply{s.duplicateHandle&&s.next==2?1u:s.next});
        }
        if(m==NativeAdrenoMethodBoImport) {
            assert(n==sizeof(NativeAdrenoAbiBoImport)&&s.next==3&&s.writeUnlocks==1);
            const auto &r=*static_cast<const NativeAdrenoAbiBoImport *>(in);
            assert(r.address==(uintptr_t)s.memory&&r.bytes==16384&&!(r.address&4095));
            s.bos[3].bytes=r.bytes;++s.next;++s.imports;return reply(NativeAdrenoAbiBoAllocReply{4});
        }
        if(m==NativeAdrenoMethodBoMap) {
            const auto &r=*static_cast<const NativeAdrenoAbiBoMap *>(in);assert(r.handle&&r.handle<=s.next);
            assert(!r.writeback&&r.shareability==2&&!r.reserved);
            auto &b=s.bos[r.handle-1];b.iova=r.iova;b.perms=r.permissions;
            return reply(NativeAdrenoAbiBoMapReply{s.badMap?r.iova+4096:r.iova});
        }
        if(m==NativeAdrenoMethodBoCpuMap) {
            const auto &r=*static_cast<const NativeAdrenoAbiBoCpuMap *>(in);assert(r.handle!=4&&r.handle<=s.next);
            return reply(NativeAdrenoAbiBoCpuMapReply{s.badToken?0u:uint32_t(r.handle),0,s.bos[r.handle-1].bytes});
        }
        if(m==NativeAdrenoMethodSubmit) {
            const auto &r=*static_cast<const NativeAdrenoAbiSubmit *>(in);
            assert(n==sizeof(r)&&r.buffer_count==1&&!r.queue&&!r.wait_fence&&!r.wait_timeout_ns&&!r.reserved);
            assert(r.buffers[0].handle==1&&r.buffers[0].iova==s.bos[0].iova&&s.publish==5);
            assert(s.holds==1&&!s.releases&&!s.writeLocked&&!s.readLocked&&s.imports==1&&s.allocs==4&&s.views==4);
            for(unsigned i=0;i<4096;++i)assert(((uint32_t *)s.memory)[i]==Native540Render::Sentinel);
            s.stream.assign(s.bos[0].data.begin(),s.bos[0].data.begin()+r.buffers[0].bytes/4);
            ++s.submits;return reply(NativeAdrenoAbiSubmitReply{17});
        }
        if(m==NativeAdrenoMethodWaitFence) {
            const auto &r=*static_cast<const NativeAdrenoAbiWaitFence *>(in);
            assert(r.fence==17&&r.timeout_ns==200000000&&!*bytes);++s.waits;
            if(s.timeout)return -88;
            // EXPLICIT SOFTWARE DMA. The production engine NEVER synthesizes pixels.
            if(s.draw)for(int y=0;y<64;++y)for(int x=0;x<64;++x){int X=2*x+1,Y=2*y+1;
                ((uint32_t *)s.memory)[y*64+x]=(Y<112&&2*X+Y>144&&112-2*X+Y>0)?Native540Render::Center:Native540Render::Background;}
            if(s.stamp){s.bos[4].data[0]=1;s.bos[4].data[2]=2;s.bos[4].data[4]=3;}
            if(s.calls==s.badReplyAt)*bytes=1;return 0;
        }
        assert(false);return -1;
    }
    static int view(void *p,uint32_t token,uint64_t &a,uint64_t &n) {
        auto &s=model(p);++s.views;if(int rc=s.event())return rc;
        assert(token&&token!=4&&token<=s.next);auto &b=s.bos[token-1];
        a=(uintptr_t)b.data.data();n=s.badView?b.bytes-4:b.bytes;return 0;
    }
    static int unmapView(void *p,uint32_t token,uint64_t a,uint64_t n) {
        auto &s=model(p);if(int rc=s.event())return rc;
        assert(token&&token!=4&&token<=5&&a==(uintptr_t)s.bos[token-1].data.data()&&n==s.bos[token-1].bytes);
        ++s.unmappedViews;return 0;
    }
    static int create(void *p,void *&owner){auto &s=model(p);++s.creates;if(int rc=s.event())return rc;owner=&s;return 0;}
    static int gettersFn(void *p,void *owner,S::Layout &l) {
        auto &s=model(p);assert(owner==&s);++s.getters;if(int rc=s.event())return rc;l=s.layout;
        if(s.mutateLocked&&s.getters==3)l.surfaceID++;return 0;
    }
    static int lock(void *p,void *owner,bool read,uint32_t &seed) {
        auto &s=model(p);assert(owner==&s);if(int rc=s.event())return rc;
        assert(!s.writeLocked&&!s.readLocked);seed=9;
        if(read){assert(s.waits==1&&!s.timeout);++s.readLocks;s.readLocked=true;}
        else{++s.writeLocks;s.writeLocked=true;}return 0;
    }
    static int unlock(void *p,void *owner,bool read,uint32_t &seed) {
        auto &s=model(p);assert(owner==&s);if(int rc=s.event())return rc;seed=10;
        if(read){assert(s.readLocked);++s.readUnlocks;s.readLocked=false;}
        else{assert(s.writeLocked);++s.writeUnlocks;s.writeLocked=false;}return 0;
    }
    static int cache(void *p,uint64_t a,uint64_t n,S::CachePhase phase) {
        auto &s=model(p);if(int rc=s.event())return rc;assert(a&&n);
        if(phase==S::PublishSurface||phase==S::AcquireSurface)assert(a==(uintptr_t)s.memory&&n==16384);
        if(phase==S::PublishBO||phase==S::PublishSurface){assert(!s.submits&&!s.readLocked&&!s.writeLocked);++s.publish;}
        else{assert(s.waits==1&&!s.timeout&&s.readLocked);++s.acquire;}
        if(s.cacheCorrupt&&phase==S::PublishSurface)s.bos[1].data[0]^=1;return 0;
    }
    static int hold(void *p){auto &s=model(p);if(int rc=s.event())return rc;++s.holds;return 0;}
    static void release(void *p){++model(p).releases;}
    static void releaseSurface(void *p,void *owner){assert(owner==p);++model(p).surfaceReleases;}
    static void record(void *p,const S::Record &){++model(p).records;}
    S::Transport transport(){S::Transport t{};t.call=call;t.mapView=view;t.create=create;t.getters=gettersFn;
        t.lock=lock;t.unlock=unlock;t.cache=cache;t.retainContext=hold;t.releaseContext=release;
        t.releaseSurface=releaseSurface;t.record=record;t.context=this;t.unmapView=unmapView;t.session=71;
        t.exclusiveConnection=t.newBoCPUInhibit=t.mappedHoldsCoverAll=true;return t;}
};
static void retained(const Model &m,const S::Record &r) {
    assert(!m.releases&&!m.surfaceReleases&&!r.retirementConfirmed);
    assert(r.contextHeld==(m.holds==1));assert(r.surfaceHeld==(m.creates&&r.surfaceHeld));
}
static void layoutTests() {
    Model m;assert(S::admit(m.layout));unsigned count=0;
    auto reject=[&](S::Layout l){assert(!S::admit(l));Model n;n.layout=l;
        S engine(n.transport());assert(engine.run(true)==S::Invalid);
        assert(!n.allocs&&!n.imports&&!n.submits&&!n.writeLocks&&!n.publish);
        for(unsigned i=0;i<4096;++i)assert(((uint32_t *)n.memory)[i]==0xa5a5a5a5);
        retained(n,engine.result());++count;};
    for(unsigned offset:{1u,4u,64u,4095u}){auto l=m.layout;l.base+=offset;l.planeBase=l.base;reject(l);}
    for(uint64_t size:{0ull,4096ull,16380ull,16385ull,32768ull,UINT64_MAX}){auto l=m.layout;l.allocation=size;reject(l);}
    auto l=m.layout;l.base=l.planeBase=UINT64_MAX-4095;reject(l);
    for(unsigned pitch:{0u,64u,255u,260u,512u}){l=m.layout;l.pitch=l.planePitch=pitch;reject(l);}
    for(unsigned planes:{1u,2u,3u}){l=m.layout;l.planes=planes;reject(l);}
    l=m.layout;l.planeBase+=4096;reject(l);l=m.layout;l.planePitch=512;reject(l);
    l=m.layout;l.width=63;reject(l);l=m.layout;l.height=65;reject(l);
    l=m.layout;l.element=8;reject(l);l=m.layout;l.planeElement=1;reject(l);
    l=m.layout;l.elementWidth=2;reject(l);l=m.layout;l.planeElementHeight=2;reject(l);
    l=m.layout;l.surfaceID=0;reject(l);l=m.layout;l.format=0x42475241;reject(l);
    std::cout<<"layout/alignment/pitch/plane/allocation: "<<count<<" rejects PASS\n";
}
static void successTests() {
    Model m;S engine(m.transport());assert(!engine.run(true));const auto &r=engine.result();
    assert(r.readbackVerified&&r.pixelsRead&&r.fenceSucceeded&&r.submitted&&r.lock==S::Unlocked);
    assert(r.pixels.inside==968&&r.pixels.outside==2744&&r.pixels.uncheckedEdge==384&&!r.pixels.mismatches);
    assert(r.clearTimestamp==1&&r.colorTimestamp==2&&r.ucheTimestamp==3);
    assert(m.allocs==4&&m.imports==1&&m.views==4&&m.submits==1&&m.waits==1&&m.acquire==2&&m.records==1);
    retained(m,r);
    const uint64_t expected[]={0x40100000,0x40102000,0x40103000,0x40104000,0x40108000};
    for(unsigned i=0;i<5;++i){assert(m.bos[i].iova==expected[i]);assert(m.bos[i].perms==(i<3?1u:3u));}
    // Byte-for-byte same established fixture/encoder, not a new renderer.
    const Native540Render::Resources resources{{expected[0],8192},{expected[1],4096},{expected[2],4096},{expected[3],16384},{expected[4],4096}};
    uint32_t words[2048]={},written=0;
    assert(Native540Render::encode(Native540RenderFixture::vs,Native540RenderFixture::fs,resources,words,2048,written));
    assert(m.stream==std::vector<uint32_t>(words,words+written));
    unsigned calls=m.calls;assert(engine.run(true)==S::AlreadyAttempted&&m.calls==calls);
    assert(engine.confirmConnectionRetired()==S::RetirementPending);
    assert(!engine.retireResources()&&r.retirement.proof&&r.retirement.operations==13);
    assert(m.unmappedViews==4&&m.cpuUnmaps==4&&m.frees==5);
    assert(!engine.confirmConnectionRetired());assert(m.releases==1&&m.surfaceReleases==1&&r.retirementConfirmed);
    // A completely scoped-away failed engine MUST NOT release its context/surface.
    Model n;n.timeout=true;{S scoped(n.transport());assert(scoped.run(true)==-88);}
    assert(n.holds==1&&!n.releases&&!n.surfaceReleases&&!n.acquire&&!n.readLocks);
    std::cout<<"one-shot/4NEWBO/import/no-token/exact-PM4/sentinel/timestamps/oracle/retirement: PASS\n";
}
static void failureTests() {
    Model baseline;S good(baseline.transport());assert(!good.run(true));    const unsigned total=baseline.events;
    for(unsigned point=1;point<=13;++point){Model m;S e(m.transport());assert(!e.run(true));
        m.failAt=m.events+point;assert(e.retireResources()==-77&&!e.result().retirement.proof);retained(m,e.result());
        unsigned events=m.events;assert(e.retireResources()==NativeMetal540Retirement::AlreadyAttempted&&m.events==events);
        assert(e.confirmConnectionRetired()==S::RetirementPending);}
    for(unsigned point=1;point<=total;++point){Model m;m.failAt=point;S e(m.transport());assert(e.run(true)==-77);
        assert(e.result().error==-77&&e.result().rawrc==-77&&!e.result().readbackVerified);retained(m,e.result());
        unsigned c=m.calls;assert(e.run(true)==S::AlreadyAttempted&&m.calls==c);
        if(!e.result().fenceSucceeded)assert(!m.acquire&&!m.readLocks&&!e.result().pixelsRead);}
    for(unsigned point=1;point<=baseline.calls;++point){Model m;m.badReplyAt=point;S e(m.transport());assert(e.run(true)<0);
        assert(!e.result().readbackVerified);retained(m,e.result());}
    for(unsigned flags:{0u,2u,3u}){Model m;m.hostFlags=flags;S e(m.transport());assert(e.run(true)==S::Invalid&&!m.creates);}
    {Model m;S e(m.transport());assert(e.run(false)==S::Prerequisite&&!m.events&&!m.calls);}
    for(unsigned mode=0;mode<8;++mode){Model m;switch(mode){case 0:m.badQuery=true;break;case 1:m.badDevice=true;break;
        case 2:m.badToken=true;break;case 3:m.badView=true;break;case 4:m.duplicateHandle=true;break;case 5:m.badMap=true;break;
        case 6:m.cacheCorrupt=true;break;case 7:m.mutateLocked=true;break;}
        S e(m.transport());assert(e.run(true)<0&&!e.result().readbackVerified);retained(m,e.result());}
    for(unsigned mode=0;mode<4;++mode){Model m;auto t=m.transport();
        switch(mode){case 0:t.exclusiveConnection=false;break;case 1:t.newBoCPUInhibit=false;break;
            case 2:t.mappedHoldsCoverAll=false;break;case 3:t.retainContext=nullptr;break;}
        S e(t);assert(e.run(true)==S::Invalid&&!m.events);}
    {Model m;m.timeout=true;S e(m.transport());assert(e.run(true)==-88);
        assert(e.result().stage==S::Fence&&!e.result().fenceSucceeded&&!e.result().pixelsRead&&!m.acquire&&!m.readLocks);retained(m,e.result());
        const unsigned events=m.events;assert(e.retireResources()<0&&m.events==events&&!m.frees&&!m.unmappedViews);}
    std::cout<<"failures: "<<total<<" callback/transport + "<<baseline.calls<<" malformed replies + ownership/import/map/cache/timeout PASS\n";
}
static void notWrittenTests() {
    {Model m;m.draw=false;S e(m.transport());assert(e.run(true)==S::Mismatch);
        const auto &r=e.result();assert(r.fenceSucceeded&&r.clearTimestamp==1&&r.colorTimestamp==2&&r.ucheTimestamp==3);
        assert(r.pixelsRead&&!r.readbackVerified&&r.pixels.center==Native540Render::Sentinel&&r.pixels.mismatches==3712);
        assert(m.readUnlocks==1);retained(m,r);}
    {Model m;m.stamp=false;S e(m.transport());assert(e.run(true)==S::Mismatch);
        assert(e.result().pixels.mismatches==0&&!e.result().readbackVerified&&!e.result().clearTimestamp);retained(m,e.result());}
    std::cout<<"fence-but-notwritten / pixels-without-timestamps fail closed PASS\n";
}
int main(int argc,char **argv) {
    assert(argc==2);std::string suite=argv[1];
    if(suite=="layout")layoutTests();else if(suite=="success")successTests();
    else if(suite=="failures")failureTests();else if(suite=="notwritten")notWrittenTests();else assert(false);
    std::cout<<"HOST SOFTWARE MODEL ONLY; NOT GPU execution / NOT IOSurface device validation\n";
}
