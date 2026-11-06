// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NativeMetal540Client.h"

// One connection-local controlled dispatch. Not an MTLDevice or public wire ABI.
// The caller owns/serializes the already-open ABI connection. No reopen/reset,
// free, CPU/GPU unmap or destructor cleanup: every resource survives ALL exits
// until IOServiceClose; the existing kernel engine/job policy owns retirement.
class NativeMetal540Probe {
public:
    enum Stage : uint32_t { Begin=1, Query, Device, Allocate, MapGPU, MapCPU,
        Encode, Upload, Submit, Fence, Readback, Complete };
    enum { Invalid=-5410, Mismatch=-5411, AlreadyAttempted=-5412 };
    enum CpuPolicy { InhibitCache=1 }; // kernel clientMemoryForType contract
    struct Record {
        Stage stage=Begin; int error=0; uint32_t resource=0, codeWords=0, ibWords=0;
        uint64_t handle=0, iova=0, fence=0, cpuBytes=0;
        uint32_t verified=0, guardVerified=0, observed=0, scratch=0;
        uint32_t state[8]={},samples[4]={};
        const char *producer="GLSL430-NIR-A540-IR3; NOT Metalcompiler";
    };
    struct Transport {
        NativeMetal540Client::Call call=nullptr;
        int (*mapView)(void *, uint32_t token, uint64_t &address, uint64_t &bytes)=nullptr;
        // Both operations apply to inhibit-cache task views. This is ordering,
        // NOT cache clean/invalidate for imported or cacheable allocations.
        void (*order)(void *, bool beforeSubmit)=nullptr;
        void (*record)(void *, const Record &)=nullptr;
        void *context=nullptr;
        CpuPolicy policy=InhibitCache;
    };
    explicit NativeMetal540Probe(const Transport &transport) : transport_(transport) {}
    int run();
    const Record &result() const { return record_; }
private:
    struct Bo { uint64_t handle=0, iova=0, address=0, bytes=0; uint32_t token=0; };
    Transport transport_; Record record_{}; Bo bos_[4]{}; bool attempted_=false;
    void stage(Stage, uint32_t resource=0);
    int fail(int);
    int request(uint32_t, const void *, size_t, void *, size_t);
};
