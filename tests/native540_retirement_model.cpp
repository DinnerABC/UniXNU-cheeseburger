// SPDX-License-Identifier: GPL-2.0-only
#include "../package/Userland/NativeMetal540Retirement.h"
#include <assert.h>
#include <limits>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

using Retirement=NativeMetal540Retirement;
struct Event { uint32_t selector,resource; };
struct Model {
    std::vector<Retirement::Resource> resources;
    std::vector<Event> events;
    bool view[5]={},cpu[5]={},freed[5]={},completeFailed[5]={};
    uint32_t failAt=0,malformedAt=0;int failure=-7001;size_t malformedBytes=1;
    unsigned freeCount[5]={},viewCount[5]={},cpuCount[5]={};
    // These owners deliberately never change, on failure OR on proof success.
    bool contextHeld=true,surfaceHeld=true,connectionOpen=true;
    Retirement::Transport transport(uint32_t budget=15) {
        Retirement::Transport t{};t.context=this;t.session=0x123456789ull;t.budget=budget;
        t.call=[](void *p,uint32_t selector,const void *in,size_t n,void *out,size_t *bytes)->int {
            return static_cast<Model *>(p)->call(selector,in,n,out,bytes);
        };
        t.unmapView=[](void *p,uint32_t token,uint64_t address,uint64_t bytes)->int {
            return static_cast<Model *>(p)->unmap(token,address,bytes);
        };
        return t;
    }
    void populate(Retirement &r,unsigned count=5,unsigned importMask=8) {
        uint64_t address=0x100000000ull,iova=0x80000000ull;
        for(unsigned i=0;i<count;++i) {
            Retirement::Resource b{};
            b.handle=0x100+i;b.session=0x123456789ull;b.bytes=i==0?8192:i==3?16384:4096;
            b.address=address;b.iova=iova;b.imported=(importMask>>i)&1;
            b.cpuMapped=!b.imported;b.token=b.imported?0:0x200+i;
            address+=b.bytes;iova+=b.bytes;resources.push_back(b);
            view[i]=b.cpuMapped;cpu[i]=bool(b.token);assert(r.add(b)==0);
        }
    }
    unsigned byHandle(uint64_t handle) {
        for(unsigned i=0;i<resources.size();++i)if(resources[i].handle==handle)return i;
        assert(false&&"stale or invented handle");return 0;
    }
    int unmap(uint32_t token,uint64_t address,uint64_t bytes) {
        unsigned i=0;for(;i<resources.size();++i)if(resources[i].token==token)break;
        assert(i<resources.size());const auto &b=resources[i];
        assert(!b.imported&&b.cpuMapped&&token&&b.address==address&&b.bytes==bytes);
        assert(view[i]&&cpu[i]&&!freed[i]&&viewCount[i]++==0);
        events.push_back({0,i});if(events.size()==failAt)return failure;
        view[i]=false;return 0;
    }
    int call(uint32_t selector,const void *in,size_t n,void *out,size_t *bytes) {
        assert(in&&bytes&&!out&&*bytes==0);
        uint64_t handle=0;uint32_t token=0;
        if(selector==NativeAdrenoMethodBoCpuUnmap) {
            assert(n==sizeof(NativeAdrenoAbiBoCpuUnmap));NativeAdrenoAbiBoCpuUnmap b{};
            memcpy(&b,in,sizeof(b));handle=b.handle;token=b.token;assert(!b.reserved);
        } else {
            assert(selector==NativeAdrenoMethodBoFree&&n==sizeof(NativeAdrenoAbiBoFree));
            NativeAdrenoAbiBoFree b{};memcpy(&b,in,sizeof(b));handle=b.handle;
        }
        const unsigned i=byHandle(handle);assert(!freed[i]);events.push_back({selector,i});
        if(selector==NativeAdrenoMethodBoCpuUnmap) {
            assert(!resources[i].imported&&!view[i]&&cpu[i]&&token==resources[i].token);
            assert(cpuCount[i]++==0);
        } else { assert(!view[i]&&!cpu[i]&&freeCount[i]++==0); }
        if(events.size()==failAt) {
            if(selector==NativeAdrenoMethodBoFree&&resources[i].imported)completeFailed[i]=true;
            return failure;
        }
        // Simulate a kernel operation that DID happen but whose success reply
        // was malformed: the helper must not ACK it, retry it or release owners.
        if(selector==NativeAdrenoMethodBoCpuUnmap)cpu[i]=false;else freed[i]=true;
        if(events.size()==malformedAt)*bytes=malformedBytes;
        return 0;
    }
    void owners() const { assert(contextHeld&&surfaceHeld&&connectionOpen); }
};
static void repeated(Retirement &r,Model &m,int rc) {
    const auto operations=r.result().operations;const size_t events=m.events.size();
    for(unsigned i=0;i<8;++i) {
        assert(r.retire(true,true,true)==(rc?rc:Retirement::AlreadyAttempted));
        assert(r.result().operations==operations&&m.events.size()==events);
        assert(r.result().proof==!bool(rc));m.owners();
    }
}
static void success() {
    for(unsigned count=1;count<=5;++count)for(unsigned mask=0;mask<(1u<<count);++mask) {
        Model m;Retirement r(m.transport());m.populate(r,count,mask);assert(r.retire(true,true,true)==0);
        const auto &result=r.result();unsigned expected=0;
        for(unsigned i=0;i<count;++i) {
            const auto &b=result.bos[i];expected+=m.resources[i].imported?1:3;
            assert(b.freeAck&&b.viewUnmapped&&!b.error&&!b.rawrc&&m.freeCount[i]==1);
            assert(b.freeAttempted&&b.resource.session==result.session);
            assert(b.cpuUnmapAck==!m.resources[i].imported);
            assert(b.cpuUnmapAttempted==!m.resources[i].imported);
            assert(b.viewUnmapAttempted==!m.resources[i].imported);
            assert(b.resource.handle==m.resources[i].handle&&b.resource.iova==m.resources[i].iova);
            assert(b.resource.address==m.resources[i].address&&b.resource.bytes==m.resources[i].bytes);
        }
        assert(result.proof&&result.stage==Retirement::Complete&&!result.error&&!result.rawrc);
        assert(result.operations==expected&&m.events.size()==expected&&result.operations<=15);
        repeated(r,m,0);
    }
    // Known allocated BOs with no CPU map, or a known token but no task view.
    for(unsigned token: {0u,0x888u}) {
        Model m;Retirement r(m.transport());Retirement::Resource b{};
        b.handle=1;b.bytes=4096;b.session=m.transport().session;b.token=token;
        m.resources.push_back(b);m.cpu[0]=bool(token);assert(r.add(b)==0);
        assert(r.retire(true,true,true)==0&&r.result().proof);
        assert(r.result().operations==1+unsigned(bool(token))&&m.viewCount[0]==0);repeated(r,m,0);
    }
}
static void gates() {
    for(unsigned flags=0;flags<7;++flags) {
        Model m;Retirement r(m.transport());m.populate(r);
        assert(r.retire(flags&1,flags&2,flags&4)==Retirement::Prerequisite);
        assert(r.result().stage==Retirement::Gate&&!r.result().proof&&r.result().operations==0);
        assert(m.events.empty());repeated(r,m,Retirement::Prerequisite);
    }
    Model m;Retirement r(m.transport());m.populate(r);r.markUncertain();
    assert(r.result().uncertainOwnership&&r.retire(true,true,true)==Retirement::Uncertain);
    assert(m.events.empty());repeated(r,m,Retirement::Uncertain);
    Model failed;Retirement c(failed.transport());Retirement::Resource b{};
    b.handle=1;b.bytes=4096;b.address=0x100000;b.imported=true;b.completeFailed=true;
    b.session=failed.transport().session;assert(c.add(b)==Retirement::Uncertain);
    assert(c.result().count==1&&c.result().bos[0].resource.completeFailed);
    assert(c.retire(true,true,true)==Retirement::Uncertain&&failed.events.empty());
    repeated(c,failed,Retirement::Uncertain);
    for(unsigned missing=0;missing<4;++missing) {
        Model invalid;auto t=invalid.transport();
        if(missing==0)t.call=nullptr;if(missing==1)t.unmapView=nullptr;
        if(missing==2)t.context=nullptr;if(missing==3)t.session=0;
        Retirement bad(t);
        if(missing!=3)invalid.populate(bad);
        assert(bad.retire(true,true,true)==Retirement::Invalid&&invalid.events.empty());
    }
    Model empty;Retirement noBO(empty.transport());
    assert(noBO.retire(true,true,true)==Retirement::Invalid&&empty.events.empty());
}
static Retirement::Stage eventStage(const Event &e) {
    return !e.selector?Retirement::UnmapView:
        e.selector==NativeAdrenoMethodBoCpuUnmap?Retirement::CpuUnmap:Retirement::Free;
}
static void failures() {
    for(unsigned at=1;at<=13;++at)for(int rc: {-7001,-7002,-536870186,123}) {
        Model m;m.failAt=at;m.failure=rc;Retirement r(m.transport());m.populate(r);
        assert(r.retire(true,true,true)==rc);const auto &result=r.result();
        assert(result.error==rc&&result.rawrc==rc&&!result.proof&&result.operations==at);
        assert(m.events.size()==at&&result.stage==eventStage(m.events.back()));
        const auto &b=result.bos[m.events.back().resource];assert(b.error==rc&&b.rawrc==rc);
        if(result.stage==Retirement::Free)assert(b.freeAttempted&&!b.freeAck&&b.freeRC==rc&&b.freeError==rc);
        if(result.stage==Retirement::CpuUnmap)assert(!b.cpuUnmapAck&&b.cpuUnmapRC==rc&&b.cpuUnmapError==rc);
        if(result.stage==Retirement::UnmapView)assert(!b.viewUnmapped&&b.viewUnmapRC==rc);
        for(unsigned i=0;i<m.events.back().resource;++i)assert(result.bos[i].freeAck&&m.freeCount[i]==1);
        repeated(r,m,rc);
        // A later ledger mutation cannot erase the first retirement failure.
        assert(r.add(m.resources[0])==rc);r.markUncertain();
        assert(r.result().stage==eventStage(m.events.back())&&r.result().rawrc==rc);repeated(r,m,rc);
    }
}
static void malformed() {
    Model good;Retirement baseline(good.transport());good.populate(baseline);assert(baseline.retire(true,true,true)==0);
    for(unsigned at=1;at<=13;++at)if(good.events[at-1].selector)
        for(size_t length: {size_t(1),std::numeric_limits<size_t>::max()}) {
            Model m;m.malformedAt=at;m.malformedBytes=length;Retirement r(m.transport());m.populate(r);
            assert(r.retire(true,true,true)==Retirement::MalformedReply);const auto &result=r.result();
            assert(result.rawrc==0&&result.error==Retirement::MalformedReply&&!result.proof);
            assert(result.operations==at&&m.events.size()==at&&result.stage==eventStage(m.events.back()));
            const auto &b=result.bos[m.events.back().resource];assert(b.error==Retirement::MalformedReply&&!b.rawrc);
            if(result.stage==Retirement::Free)assert(!b.freeAck&&b.freeError==Retirement::MalformedReply);
            else assert(!b.cpuUnmapAck&&b.cpuUnmapError==Retirement::MalformedReply);
            repeated(r,m,Retirement::MalformedReply);
        }
}
static void identity() {
    for(unsigned field=0;field<22;++field) {
        Model m;Retirement r(m.transport());m.populate(r,1,0);auto b=m.resources[0];
        b.handle+=1;b.token+=1;b.address+=8192;b.iova+=8192;
        switch(field) {
        case 0:b.handle=m.resources[0].handle;break;
        case 1:b.token=m.resources[0].token;break;
        case 2:b.address=m.resources[0].address;break;
        case 3:b.iova=m.resources[0].iova;break;
        case 4:b.session+=1;break;
        case 5:b.session=0;break;
        case 6:b.handle=0;break;
        case 7:b.bytes=0;break;
        case 8:b.bytes=4097;break;
        case 9:b.bytes=(1ull<<NativeAdrenoAbiMaxBoBytesShift)+4096;break;
        case 10:b.address=0;break;
        case 11:b.address+=1;break;
        case 12:b.address=UINT64_MAX-4095;break;
        case 13:b.iova+=1;break;
        case 14:b.iova=UINT64_MAX-4095;break;
        case 15:b.token=0;break;
        case 16:b.imported=true;break;
        case 17:b.cpuMapped=false;break;
        case 18:b.imported=true;b.cpuMapped=false;break;
        case 19:b.imported=true;b.cpuMapped=false;b.token=0;b.address+=4;break;
        case 20:b.address=m.resources[0].address+4096;break;
        case 21:b.iova=m.resources[0].iova+4096;break;
        }
        const int expected=field==4||field==5?Retirement::StaleIdentity:Retirement::Invalid;
        assert(r.add(b)==expected&&r.result().count==1&&r.result().uncertainOwnership);
        assert(r.retire(true,true,true)==expected&&m.events.empty());repeated(r,m,expected);
    }
    Model m;Retirement r(m.transport());m.populate(r,5,0);auto b=m.resources.back();
    b.handle+=1;b.token+=1;b.address+=4096;b.iova+=4096;
    assert(r.add(b)==Retirement::Invalid&&r.result().count==5);
    assert(r.retire(true,true,true)==Retirement::Invalid&&m.events.empty());
    Model frozen;Retirement done(frozen.transport());frozen.populate(done,1,0);
    assert(done.retire(true,true,true)==0);assert(done.add(frozen.resources[0])==Retirement::AlreadyAttempted);
    assert(done.result().proof);repeated(done,frozen,0);
}
static void budget() {
    for(unsigned budget=0;budget<=16;++budget) {
        Model m;Retirement r(m.transport(budget));m.populate(r,5,0);
        const int expected=budget==15?0:!budget||budget>15?Retirement::Invalid:Retirement::BudgetExceeded;
        assert(r.retire(true,true,true)==expected);
        assert(r.result().operations==(expected?0:15));if(expected)assert(m.events.empty());
        repeated(r,m,expected);
    }
    Model m;Retirement r(m.transport(1));m.populate(r,1,1);
    assert(r.retire(true,true,true)==0&&r.result().operations==1);repeated(r,m,0);
}
int main(int argc,char **argv) {
    assert(argc==2);const std::string suite=argv[1];
    if(suite=="success")success();else if(suite=="gates")gates();
    else if(suite=="failures")failures();else if(suite=="malformed")malformed();
    else if(suite=="identity")identity();else if(suite=="budget")budget();else return 2;
    printf("PASS %s: exact retirement callback model; NOT GPU execution or owner release\n",argv[1]);
    return 0;
}
