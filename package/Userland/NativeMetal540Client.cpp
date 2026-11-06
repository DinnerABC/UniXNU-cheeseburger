// SPDX-License-Identifier: MIT
#include "NativeMetal540Client.h"
static uint32_t oddParity(uint32_t n) { n^=n>>16;n^=n>>8;n^=n>>4;return (~0x6996u>>(n&15))&1; }
// sagit AdrenoSubmit.h packet7, fixed A5xx packet generation.
static uint32_t packet7(uint32_t op,uint32_t n) { return 0x70000000u|n|(oddParity(n)<<15)|(op<<16)|(oddParity(op)<<23); }
size_t NativeMetal540EncodeBarrier(uint32_t *w,size_t n) {
    if(!w||n<3)return 0;
    w[0]=packet7(0x26,0);w[1]=packet7(0x10,1);w[2]=0;return 3;
}
int NativeMetal540Client::query() {
    if(lost_||!call_)return Lost;
    params_={};size_t n=sizeof(params_);
    int rc=call_(context_,NativeAdrenoMethodQuery,nullptr,0,&params_,&n);
    ready_=false;
    if(rc)return rc;
    if(n!=sizeof(params_)||params_.abi_version!=NativeAdrenoAbiVersion||params_.input_bits<32||params_.input_bits>40||
       params_.output_bits<32||params_.output_bits>40||!params_.max_bo_bytes||params_.max_bo_bytes>(1ull<<NativeAdrenoAbiMaxBoBytesShift)||
       (params_.flags&~NativeAdrenoParamsHostImport))return Invalid;
    ready_=true;return 0;
}
int NativeMetal540Client::allocate(uint64_t bytes,uint64_t &handle) {
    handle=0;if(!ready()||!bytes||(bytes&4095)||bytes>params_.max_bo_bytes)return Invalid;
    NativeAdrenoAbiBoAlloc in{bytes,12,0};NativeAdrenoAbiBoAllocReply out{};
    int rc=request(NativeAdrenoMethodBoAlloc,in,out);if(rc)return rc;
    if(!out.handle)return Invalid;handle=out.handle;return 0;
}
int NativeMetal540Client::map(uint64_t handle,uint64_t iova,uint32_t perms,uint64_t &mapped) {
    mapped=0;if(!ready()||!handle||!iova||(iova&4095)||iova>=(1ull<<params_.input_bits)||!(perms&1)||(perms&~3))return Invalid;
    NativeAdrenoAbiBoMap in{handle,iova,perms,0,2,0};NativeAdrenoAbiBoMapReply out{};
    int rc=request(NativeAdrenoMethodBoMap,in,out);if(rc)return rc;
    if(out.iova!=iova)return Invalid;mapped=out.iova;return 0;
}
int NativeMetal540Client::submit(const NativeAdrenoAbiSubmitBuffer *buffers,uint32_t count,uint64_t &fence) {
    fence=0;if(!ready()||!buffers||!count||count>NativeAdrenoAbiMaxBuffers)return Invalid;
    NativeAdrenoAbiSubmit in{};in.buffer_count=count;
    for(uint32_t i=0;i<count;++i){const auto &b=buffers[i];
        if(!b.handle||!b.iova||(b.iova&3)||!b.bytes||(b.bytes&3)||b.bytes>params_.max_bo_bytes||
           b.iova>=(1ull<<params_.input_bits)||b.bytes>(1ull<<params_.input_bits)-b.iova)return Invalid;
        in.buffers[i]=b;
    }
    NativeAdrenoAbiSubmitReply out{};int rc=request(NativeAdrenoMethodSubmit,in,out);
    // Once the submit was sent, malformed completion cannot prove DMA stopped.
    if(rc||!out.fence||out.fence<=lastFence_){lost_=true;return rc?rc:Invalid;}
    fence=lastFence_=out.fence;return 0;
}
int NativeMetal540Client::wait(uint64_t fence,uint64_t timeoutNs) {
    if(!ready()||!fence||fence>lastFence_||timeoutNs>1000000000ull)return Invalid;
    NativeAdrenoAbiWaitFence in{fence,timeoutNs};size_t n=0;
    int rc=call_(context_,NativeAdrenoMethodWaitFence,&in,sizeof(in),nullptr,&n);
    if(rc||n){lost_=true;return rc?rc:Invalid;}return 0;
}
