// SPDX-License-Identifier: MIT
#include "NativeMetal540DesktopSession.h"
#include <cstring>
#include <algorithm>

int NativeMetal540DesktopSession::initialize() {
    std::lock_guard<std::mutex> guard(lock_);
    if(ready_||!transport_.call||!transport_.mapView||!transport_.unmapView||!transport_.flush)return Invalid;
    int rc=client_.query();if(rc)return rc;
    NativeAdrenoAbiDeviceInfo info{};size_t bytes=sizeof info;
    rc=transport_.call(transport_.context,NativeAdrenoMethodGetDeviceInfo,nullptr,0,&info,&bytes);
    if(rc)return rc;
    if(bytes!=sizeof info||info.chip_id!=0x05040001||info.reserved||info.flags!=NativeAdrenoDeviceClientIova||
       !info.va_start||(info.va_start&4095)||info.va_size<4096||info.va_start>=(1ull<<36)||info.va_size>(1ull<<36)-info.va_start)return Invalid;
    NativeAdrenoAbiParams limits{};size_t limitBytes=sizeof limits;
    rc=transport_.call(transport_.context,NativeAdrenoMethodQuery,nullptr,0,&limits,&limitBytes);
    if(rc)return rc;
    if(limitBytes!=sizeof limits||limits.abi_version!=1||limits.input_bits<32||limits.input_bits>40||
       !(limits.flags&NativeAdrenoParamsHostImport)||!limits.max_bo_bytes)return Invalid;
    begin_=info.va_start;end_=begin_+info.va_size;maxBo_=std::min<uint64_t>(limits.max_bo_bytes,16ull<<20);ready_=true;return 0;
}
NativeMetal540DesktopSession::Slot *NativeMetal540DesktopSession::find(uint64_t id) {
    for(auto &slot:slots_)if(slot.used&&slot.value.id==id)return &slot;return nullptr;
}
const NativeMetal540DesktopSession::Slot *NativeMetal540DesktopSession::find(uint64_t id) const {
    for(const auto &slot:slots_)if(slot.used&&slot.value.id==id)return &slot;return nullptr;
}
uint64_t NativeMetal540DesktopSession::address(uint64_t bytes) const {
    uint64_t cursor=begin_;
    for(size_t n=0;n<=slots_.size();++n){
        if(cursor>end_||bytes>end_-cursor)return 0;
        uint64_t after=cursor;
        for(const auto &slot:slots_)if(slot.used&&slot.value.iova&&cursor<slot.value.iova+slot.value.bytes&&slot.value.iova<cursor+bytes)
            after=std::max(after,slot.value.iova+slot.value.bytes);
        if(after==cursor)return cursor;cursor=after;
    }
    return 0;
}
int NativeMetal540DesktopSession::allocate(uint64_t bytes,Resource &out) {return create(nullptr,bytes,out);}
int NativeMetal540DesktopSession::import(void *address,uint64_t bytes,Resource &out) {
    if(!address||(uintptr_t(address)&4095)||(bytes&4095))return Invalid;return create(address,bytes,out);
}
int NativeMetal540DesktopSession::create(void *imported,uint64_t bytes,Resource &out) {
    std::lock_guard<std::mutex> guard(lock_);out={};
    if(!ready_||lost_||issued_!=completed_||!bytes||bytes>maxBo_||next_==UINT64_MAX)return Invalid;
    bytes=(bytes+4095)&~4095ull;
    if(bytes>(128ull<<20)-budget_)return Capacity;
    Slot *slot=nullptr;for(auto &s:slots_)if(!s.used){slot=&s;break;}
    uint64_t iova=address(bytes);if(!slot||!iova)return Capacity;
    if(imported){uintptr_t a=uintptr_t(imported);if(bytes>UINTPTR_MAX-a)return Invalid;
        for(const auto &s:slots_)if(s.used&&s.value.cpu&&a<uintptr_t(s.value.cpu)+s.value.bytes&&uintptr_t(s.value.cpu)<a+bytes)return Invalid;
    }
    slot->used=true;slot->value={++next_,0,iova,bytes,0,0,imported,imported!=nullptr};budget_+=bytes;
    NativeAdrenoAbiBoAllocReply reply{};
    int rc=imported?request(NativeAdrenoMethodBoImport,NativeAdrenoAbiBoImport{uint64_t(uintptr_t(imported)),bytes},reply):
        request(NativeAdrenoMethodBoAlloc,NativeAdrenoAbiBoAlloc{bytes,12,0},reply);
    slot->value.handle=reply.handle;
    if(!rc&&!reply.handle)rc=Invalid;
    if(!rc)for(const auto &s:slots_)if(&s!=slot&&s.used&&s.value.handle==reply.handle)rc=Invalid;
    if(!rc){uint64_t mapped=0;rc=client_.map(reply.handle,iova,3,mapped);}
    if(!rc&&!imported){
        NativeAdrenoAbiBoCpuMapReply view{};rc=request(NativeAdrenoMethodBoCpuMap,NativeAdrenoAbiBoCpuMap{reply.handle},view);
        slot->value.token=view.token;
        if(!rc&&(!view.token||view.reserved||view.bytes!=bytes))rc=Invalid;
        size_t length=0;
        if(!rc)rc=transport_.mapView(transport_.context,view.token,&slot->value.cpu,&length);
        if(!rc&&(!slot->value.cpu||length!=bytes||(uintptr_t(slot->value.cpu)&3)||bytes>UINTPTR_MAX-uintptr_t(slot->value.cpu)))rc=Invalid;
        if(!rc)for(const auto &s:slots_)if(&s!=slot&&s.used&&s.value.cpu&&uintptr_t(slot->value.cpu)<uintptr_t(s.value.cpu)+s.value.bytes&&uintptr_t(s.value.cpu)<uintptr_t(slot->value.cpu)+bytes)rc=Invalid;
    }
    if(rc){slot->uncertain=true;lost_=true;return rc;}
    if(!imported)std::memset(slot->value.cpu,0,size_t(bytes));out=slot->value;return 0;
}
int NativeMetal540DesktopSession::release(uint64_t id) {
    std::lock_guard<std::mutex> guard(lock_);Slot *slot=find(id);
    if(!slot)return Invalid;
    if(lost_||slot->uncertain||issued_!=completed_||slot->value.lastFence>completed_)return Uncertain;
    auto &r=slot->value;int rc=0;
    if(r.token){rc=transport_.unmapView(transport_.context,r.token,r.cpu,size_t(r.bytes));
        if(!rc)rc=request(NativeAdrenoMethodBoCpuUnmap,NativeAdrenoAbiBoCpuUnmap{r.handle,r.token,0});}
    if(!rc)rc=request(NativeAdrenoMethodBoFree,NativeAdrenoAbiBoFree{r.handle});
    if(rc){slot->uncertain=true;lost_=true;return rc;}
    budget_-=r.bytes;*slot={};return 0;
}
int NativeMetal540DesktopSession::submit(uint64_t ib,uint32_t words,const uint64_t *resources,size_t count,uint64_t &fence) {
    std::lock_guard<std::mutex> guard(lock_);fence=0;
    if(!ready_||lost_||!resources||!count||count>64||!words||issued_!=completed_)return Invalid;
    Slot *commands=find(ib);if(!commands||commands->value.imported||uint64_t(words)*4>commands->value.bytes)return Invalid;
    std::vector<Slot *> selected;
    for(size_t i=0;i<count;++i){Slot *slot=find(resources[i]);if(!slot||slot->uncertain||slot->value.lastFence>completed_)return Invalid;
        if(std::find(selected.begin(),selected.end(),slot)==selected.end())selected.push_back(slot);}
    if(std::find(selected.begin(),selected.end(),commands)==selected.end())return Invalid;
    for(Slot *slot:selected)if(!slot->value.imported){int rc=transport_.flush(transport_.context,slot->value.cpu,size_t(slot->value.bytes),true);
        if(rc){lost_=true;return rc;}}
    NativeAdrenoAbiSubmitBuffer in{commands->value.handle,commands->value.iova,uint64_t(words)*4};
    int rc=client_.submit(&in,1,fence);if(rc){lost_=true;return rc;}
    issued_=fence;for(auto &slot:slots_)if(slot.used)slot.value.lastFence=fence;return 0;
}
int NativeMetal540DesktopSession::wait(uint64_t fence,uint64_t timeoutNs) {
    std::lock_guard<std::mutex> guard(lock_);
    if(lost_||!fence||fence!=issued_)return Invalid;
    int rc=client_.wait(fence,timeoutNs);if(rc){lost_=true;return rc;}
    for(auto &slot:slots_)if(slot.used&&slot.value.lastFence==fence&&!slot.value.imported){
        rc=transport_.flush(transport_.context,slot.value.cpu,size_t(slot.value.bytes),false);
        if(rc){lost_=true;return rc;}}
    completed_=fence;return 0;
}
bool NativeMetal540DesktopSession::resource(uint64_t id,Resource &out) const {
    std::lock_guard<std::mutex> guard(lock_);const Slot *slot=find(id);if(!slot||lost_||issued_!=completed_||slot->uncertain)return false;out=slot->value;return true;
}
uint64_t NativeMetal540DesktopSession::allocatedBytes() const {std::lock_guard<std::mutex> guard(lock_);return budget_;}
bool NativeMetal540DesktopSession::lost() const {std::lock_guard<std::mutex> guard(lock_);return lost_;}
