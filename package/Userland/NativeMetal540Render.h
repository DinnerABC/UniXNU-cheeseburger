// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NativeMetal540Client.h"
#include <stddef.h>
#include <stdint.h>
// Independent semantic host subset; not an iOS/IOGPU wire ABI.
namespace Native540Render {
constexpr uint64_t AddressLimit=1ull<<36;
constexpr uint32_t Width=64,Height=64,Pitch=256,TargetBytes=16384;
constexpr uint32_t Background=0xff000000u,Center=0xff0000ffu,Sentinel=0x13579bdfu;
struct Info {
    uint32_t stage,codeWords,instrlen,constlen,fullRegs,halfRegs,branchStack,threadSize,subgroupSize;
    uint32_t immediateBase,immediateCount,driverBase,driverCount;
    uint32_t vertexID,instanceID,position,color,colorMask,colorRegister,fragCoord,perspective,linear,centerRHW,flags;
    // Host-only exact AIR subset, not NativeAdreno/iOS wire ABI. Legacy
    // aggregate fixtures omit these default-zero fields and keep their stream.
    uint32_t bufferMask=0,writeMask=0,strides[8]={},logicalBytes[8]={};
    uint32_t constantPrefixWords=0,constantWindowCount=0,variant=0;
};
struct Program { Info info; const uint32_t *code,*immediates; };
struct AIRProgram { Program program; const uint32_t *reflection; uint32_t reflectionWords; };
struct Region { uint64_t address=0,bytes=0; };
struct Binding { Region allocation{}; uint32_t offset=0,logicalBytes=0; bool readOnly=true; };
struct Resources {
    Region ib{},vs{},fs{},target{},scratch{};
    Binding vertexBindings[8]{},fragmentBindings[8]{};
};
struct Plan { uint32_t vsConstants[512]={},fsConstants[512]={},position=252,stride=0; };
// Public helpers provide STRUCTURAL admission, not an ISA/security verifier.
// Only the fixed compiled fixture is admitted by the one-shot client; no
// external programs may be submitted without independent compiler attestation.
bool plan(const Program &,const Program &,const Resources &,Plan &);
// All-or-nothing: staging then copy, written=0 and caller buffer untouched on failure.
bool encode(const Program &,const Program &,const Resources &,uint32_t *,uint32_t capacity,uint32_t &written);
// Only the exact compiled 21H461 SolidColor ordinary base pair. Caller retains
// full allocation/CPU views through genuine fence or kernel retirement. No
// upload, dereference, submit or lifetime release in these encoder helpers.
bool planAIR(const AIRProgram &,const AIRProgram &,const Resources &,Plan &);
bool encodeAIR(const AIRProgram &,const AIRProgram &,const Resources &,uint32_t *,uint32_t capacity,uint32_t &written);
struct Pixels { uint32_t center=0,corner=0,inside=0,outside=0,uncheckedEdge=0,mismatches=0; };
// Readback-only oracle. Never called to initialize an allocation.
bool verify(const volatile uint32_t *,size_t bytes,Pixels &);
bool verifyColor(const volatile uint32_t *,size_t bytes,uint32_t color,Pixels &);
}
// One-shot connection-local render AFTER all compute regressions succeed.
// Caller serializes and retains connection/context/task views on EVERY exit.
// No destructor release, import/free/unmap/retry/reset; existing kernel close
// retirement/quarantine owns eventual teardown, including failed submissions.
class NativeMetal540Render {
public:
    enum Stage : uint32_t { Begin=1,Query,Device,Allocate,MapGPU,MapCPU,Encode,Upload,Submit,Fence,Readback,Complete };
    enum { Invalid=-5450,Mismatch=-5451,AlreadyAttempted=-5452,Prerequisite=-5453 };
    enum CpuPolicy { InhibitCache=1 };
    struct Record {
        Stage stage=Begin;int error=0;uint32_t resource=0,ibWords=0,vsWords=0,fsWords=0;
        uint64_t handle=0,iova=0,cpuBytes=0,fence=0;
        Native540Render::Pixels pixels{};uint32_t clearTimestamp=0,colorTimestamp=0,ucheTimestamp=0;
        bool readbackVerified=false;
    };
    struct Transport {
        NativeMetal540Client::Call call=nullptr;
        int (*mapView)(void *,uint32_t,uint64_t &,uint64_t &)=nullptr;
        void (*order)(void *,bool beforeSubmit)=nullptr;
        void (*record)(void *,const Record &)=nullptr;
        void *context=nullptr;CpuPolicy policy=InhibitCache;
    };
    explicit NativeMetal540Render(const Transport &t):transport_(t) {}
    NativeMetal540Render(const NativeMetal540Render &)=delete;
    NativeMetal540Render &operator=(const NativeMetal540Render &)=delete;
    int run(bool allComputeSucceeded);
    const Record &result() const { return record_; }
private:
    struct Bo {uint64_t handle=0,iova=0,address=0,bytes=0;uint32_t token=0;};
    Transport transport_;Record record_{};Bo bos_[5]{};bool attempted_=false;
    void stage(Stage,uint32_t=0);int fail(int);
    int request(uint32_t,const void *,size_t,void *,size_t);
};
