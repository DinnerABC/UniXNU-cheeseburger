// SPDX-License-Identifier: MIT
#pragma once
#include "../BoardRuntime/NativeAdrenoABI.h"
#include <stddef.h>
#include <stdint.h>

// Controlled 21H461 transport, not an MTLDevice and not an IOGPU impersonation.
// The owner serializes all calls and supplies the existing IOConnectCallStructMethod transport. It must
// retain the connection and BOs through successful fence completion. No hidden
// selectors, automatic reconnect, resource destruction or hardware discovery.
class NativeMetal540Client {
public:
    using Call = int (*)(void *, uint32_t, const void *, size_t, void *, size_t *);
    enum { Invalid = -5401, Lost = -5402 };
    NativeMetal540Client(Call call, void *context) : call_(call), context_(context) {}
    int query();
    int allocate(uint64_t bytes, uint64_t &handle);
    int map(uint64_t handle, uint64_t iova, uint32_t permissions, uint64_t &mapped);
    int submit(const NativeAdrenoAbiSubmitBuffer *buffers, uint32_t count, uint64_t &fence);
    int wait(uint64_t fence, uint64_t timeoutNs);
    bool ready() const { return ready_ && !lost_; }
private:
    template<class In, class Out> int request(uint32_t selector, const In &in, Out &out) {
        if (!call_) return Invalid;
        size_t n=sizeof(out);out={};int rc=call_(context_,selector,&in,sizeof(in),&out,&n);
        return rc ? rc : n==sizeof(out) ? 0 : Invalid;
    }
    Call call_;void *context_;NativeAdrenoAbiParams params_{};
    bool ready_=false,lost_=false;uint64_t lastFence_=0;
};

// No arbitrary AIR blobs become command buffers. The initial, generation-
// matched adapter encodes only CP_WAIT_FOR_IDLE + CP_NOP, into a caller-owned
// BO. Shader pipelines require an independent A540 state/binding admission
// layer; that is deliberately not advertised by this controlled frontend.
size_t NativeMetal540EncodeBarrier(uint32_t *words, size_t capacity);
