// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NativeMetal540Client.h"
// Connection-local, one-shot fixed real AIR probe AFTER P1/constant/transform.
// Caller serializes the connection. Six NEW BOs/task views survive every exit
// for existing connection retirement/quarantine: no free/unmap/reset/retry.
// Not a system MTLDevice, general AIR loader, or new kernel/Metal ABI.
class NativeMetal540AIRProbe {
public:
    enum Stage : uint32_t { Begin=1, Query, Device, Allocate, MapGPU, MapCPU,
        Encode, Upload, Submit, Fence, Readback, Complete };
    enum { Invalid=-5430, Mismatch=-5431, AlreadyAttempted=-5432 };
    enum CpuPolicy { InhibitCache=1 };
    struct Record {
        Stage stage=Begin; int error=0; uint32_t resource=0, codeWords=0, ibWords=0;
        uint64_t handle=0, iova=0, fence=0, cpuBytes=0;
        uint32_t verified=0, guardVerified=0, inputVerified=0, inputGuardVerified=0;
        uint32_t paramVerified=0, paramGuardVerified=0, observed=0, scratch=0;
        // output indices 0,127,255,256; source indices 262,389,517,780.
        uint32_t samples[4]={}, inputSamples[4]={};
        const char *producer="AIR-NIR-A540-IR3 21H461 padding_gradient; known fixed AIR only; NOT system MTLDevice";
        const char *codeSHA256="8786fdb358bab5ee91dc7b79b26d6e736aa1fb79cec6bd7799fe36b94e1c528b";
    };
    struct Transport {
        NativeMetal540Client::Call call=nullptr;
        int (*mapView)(void *,uint32_t,uint64_t &,uint64_t &)=nullptr;
        // Existing inhibit-cache ordering; no claim about cacheable imports.
        void (*order)(void *,bool beforeSubmit)=nullptr;
        void (*record)(void *,const Record &)=nullptr;
        void *context=nullptr;
        CpuPolicy policy=InhibitCache;
    };
    explicit NativeMetal540AIRProbe(const Transport &transport):transport_(transport) {}
    int run();
    const Record &result() const { return record_; }
private:
    struct Bo { uint64_t handle=0,iova=0,address=0,bytes=0; uint32_t token=0; };
    Transport transport_; Record record_{}; Bo bos_[6]{}; bool attempted_=false;
    void stage(Stage,uint32_t resource=0);
    int fail(int);
    int request(uint32_t,const void *,size_t,void *,size_t);
};
