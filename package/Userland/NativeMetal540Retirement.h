// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NativeMetal540Client.h"

// Host-testable one-shot semantics, NOT a new wire ABI. The serialized caller
// populates EVERY allocation/token/view in this session before retirement; an
// uncertain allocation or map reply must call markUncertain(). Callbacks are
// synchronous and must be bounded themselves. No close, retry or owner release.
template <uint32_t Capacity=5>
class NativeMetal540RetirementLedger {
    static_assert(Capacity>0&&Capacity<=64,"bounded retirement ledger");
public:
    enum Stage : uint32_t { Begin=1, Ledger, Gate, UnmapView, CpuUnmap, Free, Complete, Failed };
    enum { Invalid=-5470, AlreadyAttempted=-5471, Prerequisite=-5472,
        Uncertain=-5473, BudgetExceeded=-5474, MalformedReply=-5475, StaleIdentity=-5476 };
    static constexpr uint32_t MaxBOs=Capacity, MaxOperations=Capacity*3;
    struct Transport {
        NativeMetal540Client::Call call=nullptr;
        int (*unmapView)(void *,uint32_t token,uint64_t address,uint64_t bytes)=nullptr;
        void *context=nullptr; uint64_t session=0; uint32_t budget=MaxOperations;
    };
    struct Resource {
        uint64_t handle=0,iova=0,address=0,bytes=0,session=0;
        uint32_t token=0; bool imported=false,cpuMapped=false,completeFailed=false;
    };
    struct Bo {
        Resource resource{};
        bool viewUnmapAttempted=false,viewUnmapped=false;
        bool cpuUnmapAttempted=false,cpuUnmapAck=false,freeAttempted=false,freeAck=false;
        Stage stage=Ledger; int error=0,rawrc=0;
        int viewUnmapRC=0,cpuUnmapRC=0,freeRC=0;
        int cpuUnmapError=0,freeError=0;
    };
    struct Record {
        Stage stage=Begin; int error=0,rawrc=0;
        uint64_t session=0; uint32_t count=0,operations=0;
        bool attempted=false,uncertainOwnership=false,proof=false;
        bool fenceSucceeded=false,cacheAcquired=false,unlocked=false;
        Bo bos[MaxBOs]{};
    };
    explicit NativeMetal540RetirementLedger(const Transport &t):transport_(t) { record_.session=t.session; }
    ~NativeMetal540RetirementLedger()=default;
    NativeMetal540RetirementLedger(const NativeMetal540RetirementLedger &)=delete;
    NativeMetal540RetirementLedger &operator=(const NativeMetal540RetirementLedger &)=delete;
    const Record &result() const { return record_; }

    int add(const Resource &r) {
        if(record_.error)return record_.error;
        if(record_.attempted)return AlreadyAttempted;
        record_.stage=Ledger;
        if(!transport_.session||r.session!=transport_.session)return badLedger(StaleIdentity);
        if(record_.count==MaxBOs||!valid(r))return badLedger(Invalid);
        for(uint32_t i=0;i<record_.count;++i) {
            const auto &b=record_.bos[i].resource;
            if(r.handle==b.handle||(r.token&&r.token==b.token)||
               overlap(r.address,r.bytes,b.address,b.bytes)||overlap(r.iova,r.bytes,b.iova,b.bytes))
                return badLedger(Invalid);
        }
        auto &b=record_.bos[record_.count++];b.resource=r;
        b.viewUnmapped=!r.cpuMapped;
        // A previously observed imported complete failure is terminal; never
        // ask BoFree to repeat complete() or infer safety from a later fence.
        if(r.completeFailed)return fail(Uncertain,0,&b);
        return 0;
    }
    void markUncertain() {
        record_.uncertainOwnership=true;
        if(!record_.error)record_.stage=Ledger;
        fail(Uncertain);
    }
    int retire(bool fenceSucceeded,bool cacheAcquired,bool unlocked) {
        if(record_.attempted)return record_.error?record_.error:AlreadyAttempted;
        record_.attempted=true;
        if(record_.error)return record_.error;
        record_.stage=Gate;record_.fenceSucceeded=fenceSucceeded;
        record_.cacheAcquired=cacheAcquired;record_.unlocked=unlocked;
        // Frame/oracle errors are intentionally not an authorization input.
        if(!fenceSucceeded||!cacheAcquired||!unlocked)return fail(Prerequisite);
        if(record_.uncertainOwnership)return fail(Uncertain);
        if(!transport_.call||!transport_.context||!transport_.session||!record_.count||
           !transport_.budget||transport_.budget>MaxOperations)return fail(Invalid);
        uint32_t needed=0;
        for(uint32_t i=0;i<record_.count;++i) {
            const auto &r=record_.bos[i].resource;
            if(r.cpuMapped&&!transport_.unmapView)return fail(Invalid);
            needed+=1+uint32_t(r.cpuMapped)+uint32_t(r.token!=0);
        }
        // Preflight the full budget, so exhaustion cannot initiate partial free.
        if(needed>transport_.budget)return fail(BudgetExceeded);
        for(uint32_t i=0;i<record_.count;++i) {
            auto &b=record_.bos[i];const auto &r=b.resource;
            if(r.cpuMapped) {
                stage(UnmapView,b);b.viewUnmapAttempted=true;++record_.operations;
                b.viewUnmapRC=transport_.unmapView(transport_.context,r.token,r.address,r.bytes);
                if(b.viewUnmapRC)return fail(b.viewUnmapRC,b.viewUnmapRC,&b);
                b.viewUnmapped=true;
            }
            if(r.token) {
                stage(CpuUnmap,b);b.cpuUnmapAttempted=true;
                const NativeAdrenoAbiBoCpuUnmap in{r.handle,r.token,0};
                b.cpuUnmapError=request(NativeAdrenoMethodBoCpuUnmap,&in,sizeof(in),b.cpuUnmapRC);
                if(b.cpuUnmapError)return fail(b.cpuUnmapError,b.cpuUnmapRC,&b);
                b.cpuUnmapAck=true;
            }
            stage(Free,b);b.freeAttempted=true;
            const NativeAdrenoAbiBoFree in{r.handle};
            b.freeError=request(NativeAdrenoMethodBoFree,&in,sizeof(in),b.freeRC);
            if(b.freeError)return fail(b.freeError,b.freeRC,&b);
            b.freeAck=true;
        }
        bool proof=!record_.uncertainOwnership;
        for(uint32_t i=0;i<record_.count;++i) {
            const auto &b=record_.bos[i];
            proof=proof&&b.freeAck&&b.viewUnmapped&&(!b.resource.token||b.cpuUnmapAck);
        }
        if(!proof)return fail(Uncertain);
        record_.proof=true;record_.stage=Complete;return 0;
    }
private:
    Transport transport_;Record record_{};
    static bool range(uint64_t address,uint64_t bytes) { return address&&bytes<=UINT64_MAX-address; }
    static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) {
        return a&&b&&a<b+m&&b<a+n;
    }
    static bool valid(const Resource &r) {
        if(!r.handle||!r.bytes||(r.bytes&4095)||r.bytes>(1ull<<NativeAdrenoAbiMaxBoBytesShift))return false;
        if(r.iova&&((r.iova&4095)||!range(r.iova,r.bytes)))return false;
        if(r.imported)return !r.token&&!r.cpuMapped&&!(r.address&4095)&&range(r.address,r.bytes);
        if(r.cpuMapped)return r.token&&!(r.address&3)&&range(r.address,r.bytes)&&
            r.address<=UINTPTR_MAX&&r.bytes<=UINTPTR_MAX-r.address;
        return !r.address;
    }
    int badLedger(int error) { record_.uncertainOwnership=true;return fail(error); }
    int fail(int error,int rawrc=0,Bo *b=nullptr) {
        record_.proof=false;
        if(!record_.error){record_.error=error;record_.rawrc=rawrc;}
        if(b&&!b->error){b->error=error;b->rawrc=rawrc;}
        return record_.error;
    }
    void stage(Stage s,Bo &b) { record_.stage=s;b.stage=s; }
    int request(uint32_t selector,const void *input,size_t inputBytes,int &rawrc) {
        size_t outputBytes=0;++record_.operations;
        rawrc=transport_.call(transport_.context,selector,input,inputBytes,nullptr,&outputBytes);
        return rawrc?rawrc:outputBytes?MalformedReply:0;
    }
};
using NativeMetal540Retirement=NativeMetal540RetirementLedger<5>;
using NativeMetal540SessionRetirement=NativeMetal540RetirementLedger<64>;
