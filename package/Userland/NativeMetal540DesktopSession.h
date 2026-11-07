// SPDX-License-Identifier: MIT
#pragma once
#include "NativeMetal540Client.h"
#include <array>
#include <mutex>
#include <vector>

// Long-lived connection-local BO ownership. A caller serializes CPU accesses
// with submit; imported storage remains externally owned through release.
class NativeMetal540DesktopSession {
public:
    using Call=NativeMetal540Client::Call;
    struct Transport {
        Call call=nullptr;void *context=nullptr;
        int (*mapView)(void *,uint32_t,void **,size_t *)=nullptr;
        int (*unmapView)(void *,uint32_t,void *,size_t)=nullptr;
        int (*flush)(void *,void *,size_t,bool)=nullptr;
    };
    struct Resource {
        uint64_t id=0,handle=0,iova=0,bytes=0,lastFence=0;
        uint32_t token=0;void *cpu=nullptr;bool imported=false;
    };
    enum { Invalid=-5510, Capacity=-5511, Uncertain=-5512 };
    explicit NativeMetal540DesktopSession(Transport t):transport_(t),client_(t.call,t.context){}
    int initialize();
    int allocate(uint64_t bytes,Resource &out);
    int import(void *address,uint64_t bytes,Resource &out);
    int release(uint64_t id);
    int submit(uint64_t ib,uint32_t words,const uint64_t *resources,size_t count,uint64_t &fence);
    int wait(uint64_t fence,uint64_t timeoutNs);
    bool resource(uint64_t id,Resource &out) const;
    uint64_t allocatedBytes() const;
    bool lost() const;
private:
    struct Slot { Resource value;bool used=false,uncertain=false; };
    template<class In,class Out> int request(uint32_t method,const In &in,Out &out) {
        out={};size_t bytes=sizeof out;
        int rc=transport_.call(transport_.context,method,&in,sizeof in,&out,&bytes);
        return rc?rc:bytes==sizeof out?0:Invalid;
    }
    template<class In> int request(uint32_t method,const In &in) {
        size_t bytes=0;int rc=transport_.call(transport_.context,method,&in,sizeof in,nullptr,&bytes);
        return rc?rc:bytes?Invalid:0;
    }
    int create(void *imported,uint64_t bytes,Resource &out);
    Slot *find(uint64_t id);
    const Slot *find(uint64_t id) const;
    uint64_t address(uint64_t bytes) const;
    Transport transport_;NativeMetal540Client client_;
    mutable std::mutex lock_;
    std::array<Slot,64> slots_{};
    uint64_t begin_=0,end_=0,next_=0,issued_=0,completed_=0,budget_=0,maxBo_=0;
    bool ready_=false,lost_=false;
};
