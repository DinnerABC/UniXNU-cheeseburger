// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NativeMetal540Client.h"
// Connection-local one-shot transform AFTER P1 and constant probe. Caller owns
// serialization and connection; all five NEW BOs/task mappings survive all exits
// until connection close and existing kernel retirement/quarantine. No imports,
// free/unmap/reset/retry/destructor cleanup. This is not a Metal device or ABI.
class NativeMetal540Transform {
public:
    enum Stage : uint32_t { Begin=1, Query, Device, Allocate, MapGPU, MapCPU,
        Encode, Upload, Submit, Fence, Readback, Complete };
    enum { Invalid=-5420, Mismatch=-5421, AlreadyAttempted=-5422 };
    enum CpuPolicy { InhibitCache=1 };
    struct Record {
        Stage stage=Begin; int error=0; uint32_t resource=0, codeWords=0, ibWords=0;
        uint64_t handle=0, iova=0, fence=0, cpuBytes=0;
        uint32_t verified=0, guardVerified=0, inputVerified=0, inputGuardVerified=0;
        uint32_t observed=0, scratch=0, state[8]={}, samples[4]={}, inputSamples[4]={};
        const char *producer="GLSL430-NIR-A540-IR3 dual SSBO; NOT Metalcompiler";
    };
    struct Transport {
        NativeMetal540Client::Call call=nullptr;
        int (*mapView)(void *,uint32_t,uint64_t &,uint64_t &)=nullptr;
        // Existing inhibit-cache view ordering, not cacheable-import coherency.
        void (*order)(void *,bool beforeSubmit)=nullptr;
        void (*record)(void *,const Record &)=nullptr;
        void *context=nullptr;
        CpuPolicy policy=InhibitCache;
    };
    explicit NativeMetal540Transform(const Transport &transport):transport_(transport) {}
    int run();
    const Record &result() const { return record_; }
private:
    struct Bo { uint64_t handle=0,iova=0,address=0,bytes=0; uint32_t token=0; };
    Transport transport_; Record record_{}; Bo bos_[5]{}; bool attempted_=false;
    void stage(Stage,uint32_t resource=0);
    int fail(int);
    int request(uint32_t,const void *,size_t,void *,size_t);
};
