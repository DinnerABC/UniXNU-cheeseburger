// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NativeMetal540Render.h"
#include "NativeMetal540Retirement.h"

// Independent host-testable semantics, NOT an Apple/IOGPU wire ABI. Callbacks
// are synchronous. Caller serializes this one-shot connection and owns its task.
class NativeMetal540Surface {
public:
    enum Stage : uint32_t { Begin=1, Retain, Query, Device, Create, Getters,
        WriteLock, Initialize, WriteUnlock, Allocate, MapGPU, MapCPU, Import,
        Encode, Upload, CachePublish, Submit, Fence, ReadLock, LockedGetters,
        CacheAcquire, Verify, ReadUnlock, Complete };
    enum { Invalid=-5460, Mismatch=-5461, AlreadyAttempted=-5462,
        Prerequisite=-5463, RetirementPending=-5464 };
    enum CachePhase : uint32_t { NoCache=0, PublishBO, PublishSurface, AcquireBO, AcquireSurface };
    enum LockState : uint32_t { Unlocked=0, WriteLocked, ReadLocked };
    static constexpr uint32_t RGBA=0x52474241;
    struct Layout {
        uint64_t base=0, planeBase=0, allocation=0;
        uint64_t width=0, height=0, pitch=0, element=0, planes=0;
        uint64_t planeWidth=0, planeHeight=0, planePitch=0, planeElement=0;
        uint64_t elementWidth=0, elementHeight=0, planeElementWidth=0, planeElementHeight=0;
        uint32_t surfaceID=0, format=0;
    };
    struct Record {
        Stage stage=Begin; int error=0, rawrc=0;
        // Actual dedicated 0x4147 connection, populated by ObjC forwarding only.
        // Sink may use it synchronously for metadata, never close or retain it.
        uint32_t connection=0;
        uint32_t resource=0, ibWords=0; uint64_t handle=0, iova=0, fence=0;
        Layout surface{}; CachePhase cachephase=NoCache; uint64_t cacheBase=0, cacheBytes=0;
        LockState lock=Unlocked; int writeLockRC=0, writeUnlockRC=0, readLockRC=0, readUnlockRC=0;
        uint32_t lockSeed=0, unlockSeed=0, clearTimestamp=0, colorTimestamp=0, ucheTimestamp=0;
        Native540Render::Pixels pixels{};
        bool contextHeld=false, surfaceHeld=false, submitted=false, fenceSucceeded=false;
        bool pixelsRead=false, readbackVerified=false, retirementConfirmed=false;
        bool cacheAcquired=false;
        NativeMetal540Retirement::Record retirement{};
    };
    struct Transport {
        NativeMetal540Client::Call call=nullptr;
        int (*mapView)(void *,uint32_t token,uint64_t &address,uint64_t &bytes)=nullptr;
        int (*unmapView)(void *,uint32_t token,uint64_t address,uint64_t bytes)=nullptr;
        // create returns one OWNED, fresh, unpublished surface; no lookup/pool.
        int (*create)(void *,void *&surfaceOwner)=nullptr;
        int (*getters)(void *,void *surfaceOwner,Layout &)=nullptr;
        int (*lock)(void *,void *surfaceOwner,bool readOnly,uint32_t &seed)=nullptr;
        int (*unlock)(void *,void *surfaceOwner,bool readOnly,uint32_t &seed)=nullptr;
        int (*cache)(void *,uint64_t address,uint64_t bytes,CachePhase)=nullptr;
        // A successful hold retains context + all task mappings on EVERY exit,
        // independent of engine lifetime. Release ONLY after confirmed retirement.
        int (*retainContext)(void *)=nullptr;
        void (*releaseContext)(void *)=nullptr;
        void (*releaseSurface)(void *,void *surfaceOwner)=nullptr;
        void (*record)(void *,const Record &)=nullptr;
        void *context=nullptr;
        uint64_t session=0;
        bool exclusiveConnection=false, newBoCPUInhibit=false, mappedHoldsCoverAll=false;
    };
    explicit NativeMetal540Surface(const Transport &t):transport_(t) {}
    ~NativeMetal540Surface()=default; // Deliberately NO scope-based release/close.
    NativeMetal540Surface(const NativeMetal540Surface &)=delete;
    NativeMetal540Surface &operator=(const NativeMetal540Surface &)=delete;
    int run(bool allPrerequisitesSucceeded);
    const Record &result() const { return record_; }
    static bool admit(const Layout &);
    int retireResources();
    // Releases owners only after this session's complete pre-close BoFree proof
    // and the wrapper's close acknowledgement. Never a render retry.
    int confirmConnectionRetired();
private:
    struct Bo { uint64_t handle=0,iova=0,address=0,bytes=0; uint32_t token=0; };
    Transport transport_; Record record_{}; Bo bos_[5]{}; void *surface_=nullptr;
    bool attempted_=false, retirementAttempted_=false;
    void stage(Stage,uint32_t=0);
    int fail(int error,int rawrc=0);
    int request(uint32_t,const void *,size_t,void *,size_t);
    int cache(uint32_t,CachePhase);
};

#if defined(__OBJC__)
#import <Foundation/Foundation.h>
// Actual public IOSurfaceCreate/Getters/Lock/Unlock + sys_dcache_flush wrapper.
// Opens a NEW dedicated connection; no plugin/system Metal/presentation hooks.
// ARC required. A run self-retains even on failure until retirement is attested.
using Native540SurfaceRecord = void (*)(void *,const NativeMetal540Surface::Record &);
@interface Native540SurfaceSession : NSObject
- (instancetype)initWithService:(uint32_t)service error:(int *)error;
// Non-null callback context requires an explicit strong owner. Sink receives
// one completion/first-error record; it owns SF540/SP/SI/SE persistence policy.
- (instancetype)initWithService:(uint32_t)service record:(Native540SurfaceRecord)sink
    context:(void *)context owner:(id)owner error:(int *)error;
- (int)runWithPrerequisites:(BOOL)succeeded;
- (NativeMetal540Surface::Record)result;
- (int)retire;
// Legacy explicit close remains conservative; callback cannot replace a ledger
// proof. Call retire first to acquire the real pre-close acknowledgements.
- (int)retireWithConfirmation:(BOOL (^)(uint32_t closedConnection))confirmation;
@end
#endif
