// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540Surface.h"
#include "../../tools/gpu-native540/render_fixture.h"
#include <atomic>
#include <initializer_list>

bool NativeMetal540Surface::admit(const Layout &s) {
    // This first probe accepts ONLY plain RGBA. No prefix, rounding, compressed
    // plane, align-down, hidden padding or inferred layout from create requests.
    return s.surfaceID && s.format==RGBA && s.base && !(s.base&4095) &&
        s.base<=UINTPTR_MAX && s.allocation==Native540Render::TargetBytes &&
        s.allocation<=UINTPTR_MAX-s.base && s.width==64 && s.height==64 &&
        s.pitch==256 && s.element==4 && !s.planes && s.planeBase==s.base &&
        s.planeWidth==s.width && s.planeHeight==s.height &&
        s.planePitch==s.pitch && s.planeElement==s.element &&
        s.elementWidth==1 && s.elementHeight==1 &&
        s.planeElementWidth==1 && s.planeElementHeight==1;
}
void NativeMetal540Surface::stage(Stage s,uint32_t i) {
    record_.stage=s; record_.resource=i; record_.handle=bos_[i].handle; record_.iova=bos_[i].iova;
}
int NativeMetal540Surface::fail(int error,int rawrc) {
    if(!record_.error){record_.error=error;record_.rawrc=rawrc;}
    if(transport_.record)transport_.record(transport_.context,record_);
    return record_.error;
}
int NativeMetal540Surface::request(uint32_t method,const void *in,size_t n,void *out,size_t expected) {
    size_t bytes=expected;
    const int rc=transport_.call(transport_.context,method,in,n,out,&bytes);
    if(rc){record_.rawrc=rc;return rc;}
    return bytes==expected?0:Invalid;
}
int NativeMetal540Surface::cache(uint32_t i,CachePhase phase) {
    const auto &b=bos_[i]; record_.cachephase=phase;record_.cacheBase=b.address;record_.cacheBytes=b.bytes;
    return transport_.cache(transport_.context,b.address,b.bytes,phase);
}
static bool sameSurface(const NativeMetal540Surface::Layout &a,const NativeMetal540Surface::Layout &b) {
    return NativeMetal540Surface::admit(b) && a.base==b.base &&
        a.surfaceID==b.surfaceID; // admission fixes EVERY remaining field exactly.
}
int NativeMetal540Surface::run(bool prerequisites) {
    if(attempted_)return AlreadyAttempted;
    attempted_=true;
    if(!prerequisites)return fail(Prerequisite);
    const auto &t=transport_;
    if(!t.call||!t.mapView||!t.create||!t.getters||!t.lock||!t.unlock||!t.cache||
       !t.retainContext||!t.releaseContext||!t.releaseSurface||!t.context||
       !t.exclusiveConnection||!t.newBoCPUInhibit||!t.mappedHoldsCoverAll)return fail(Invalid);
    stage(Retain);int rc=t.retainContext(t.context);if(rc)return fail(rc,rc);
    record_.contextHeld=true;
    // Preserve strict query rc/size and host-import prerequisite before creating.
    stage(Query);NativeAdrenoAbiParams params{};
    rc=request(NativeAdrenoMethodQuery,nullptr,0,&params,sizeof(params));if(rc)return fail(rc,record_.rawrc);
    if(params.abi_version!=1||params.input_bits!=36||params.output_bits!=36||
       params.flags!=NativeAdrenoParamsHostImport||params.max_bo_bytes<16384||
       params.max_bo_bytes>(1ull<<NativeAdrenoAbiMaxBoBytesShift))return fail(Invalid);
    // NativeMetal540Client owns the existing submit/wait fence semantics. It has
    // its own admission query; do not bypass or change the established renderer.
    // Adapt the established client without losing the actual transport rc when
    // its semantic checks reject an otherwise successful/malformed wire reply.
    struct TracedCall { NativeMetal540Client::Call call; void *context; int rawrc=0; } traced{t.call,t.context};
    NativeMetal540Client client([](void *opaque,uint32_t method,const void *in,size_t n,void *out,size_t *bytes)->int {
        auto &trace=*static_cast<TracedCall *>(opaque);
        return trace.rawrc=trace.call(trace.context,method,in,n,out,bytes);
    },&traced);
    rc=client.query();if(rc)return fail(rc,traced.rawrc);
    stage(Device);NativeAdrenoAbiDeviceInfo info{};
    rc=request(NativeAdrenoMethodGetDeviceInfo,nullptr,0,&info,sizeof(info));if(rc)return fail(rc,record_.rawrc);
    constexpr uint64_t offset=0x100000,extent=9*4096;
    if(info.chip_id!=0x05040001||info.reserved||info.flags!=NativeAdrenoDeviceClientIova||
       !info.va_start||(info.va_start&4095)||info.va_start>=Native540Render::AddressLimit||
       info.va_size<offset+extent||info.va_size>Native540Render::AddressLimit-info.va_start)return fail(Invalid);
    stage(Create);rc=t.create(t.context,surface_);
    // Even a callback failing AFTER allocation must transfer its owner here.
    record_.surfaceHeld=surface_!=nullptr;
    if(rc)return fail(rc,rc);if(!surface_)return fail(Invalid);
    stage(Getters);rc=t.getters(t.context,surface_,record_.surface);if(rc)return fail(rc,rc);
    if(!admit(record_.surface))return fail(Invalid);
    stage(WriteLock);rc=t.lock(t.context,surface_,false,record_.lockSeed);record_.writeLockRC=rc;
    if(rc)return fail(rc,rc);record_.lock=WriteLocked;
    Layout locked{};rc=t.getters(t.context,surface_,locked);if(rc)return fail(rc,rc);
    if(!sameSurface(record_.surface,locked))return fail(Invalid);
    stage(Initialize);auto *pixels=reinterpret_cast<volatile uint32_t *>(locked.base);
    for(unsigned i=0;i<Native540Render::TargetBytes/4;++i)pixels[i]=Native540Render::Sentinel;
    stage(WriteUnlock);rc=t.unlock(t.context,surface_,false,record_.unlockSeed);record_.writeUnlockRC=rc;
    if(rc)return fail(rc,rc);record_.lock=Unlocked;
    // Layout follows the established encoder: IB, VS, FS, target, scratch. The
    // target alone is imported; job mapped-holds MUST cover all five mappings.
    const uint64_t sizes[]={8192,4096,4096,16384,4096};
    uint64_t cursor=info.va_start+offset;
    for(unsigned i=0;i<5;++i) {
        auto &b=bos_[i];b.bytes=sizes[i];
        if(i==3) {
            b.address=record_.surface.base;stage(Import,i);
            const NativeAdrenoAbiBoImport in{b.address,b.bytes};NativeAdrenoAbiBoAllocReply out{};
            rc=request(NativeAdrenoMethodBoImport,&in,sizeof(in),&out,sizeof(out));
            if(rc)return fail(rc,record_.rawrc);b.handle=out.handle;if(!b.handle)return fail(Invalid);
        } else {
            stage(Allocate,i);rc=client.allocate(sizes[i],b.handle);if(rc)return fail(rc,traced.rawrc);
        }
        for(unsigned j=0;j<i;++j)if(b.handle==bos_[j].handle)return fail(Invalid);
        b.iova=cursor;stage(MapGPU,i);
        // Preserve requested IOVA in the failure record even when map rejects.
        rc=client.map(b.handle,cursor,i<3?1:3,b.iova);if(rc)return fail(rc,traced.rawrc);
        if(b.iova!=cursor)return fail(Invalid);cursor+=sizes[i];
        if(i!=3) {
            NativeAdrenoAbiBoCpuMap in{b.handle};NativeAdrenoAbiBoCpuMapReply out{};stage(MapCPU,i);
            rc=request(NativeAdrenoMethodBoCpuMap,&in,sizeof(in),&out,sizeof(out));if(rc)return fail(rc,record_.rawrc);
            if(!out.token||out.reserved||out.bytes!=sizes[i])return fail(Invalid);b.token=out.token;
            for(unsigned j=0;j<i;++j)if(b.token==bos_[j].token)return fail(Invalid);
            rc=t.mapView(t.context,b.token,b.address,b.bytes);if(rc)return fail(rc,rc);
            if(!b.address||(b.address&3)||b.bytes!=sizes[i]||b.address>UINTPTR_MAX||b.bytes>UINTPTR_MAX-b.address)return fail(Invalid);
            if(b.address<record_.surface.base+16384 && record_.surface.base<b.address+b.bytes)return fail(Invalid);
        }
        for(unsigned j=0;j<i;++j)if(b.address<bos_[j].address+bos_[j].bytes&&bos_[j].address<b.address+b.bytes)return fail(Invalid);
    }
    const Native540Render::Resources resources{{bos_[0].iova,bos_[0].bytes},
        {bos_[1].iova,bos_[1].bytes},{bos_[2].iova,bos_[2].bytes},
        {bos_[3].iova,bos_[3].bytes},{bos_[4].iova,bos_[4].bytes}};
    stage(Encode);uint32_t words[2048]={},written=0;
    if(!Native540Render::encode(Native540RenderFixture::vs,Native540RenderFixture::fs,
                               resources,words,2048,written)||!written)return fail(Invalid);
    record_.ibWords=written;stage(Upload);
    for(unsigned i:{0u,1u,2u,4u}) {
        auto *p=reinterpret_cast<volatile uint32_t *>(bos_[i].address);
        for(unsigned j=0;j<bos_[i].bytes/4;++j)p[j]=0;
    }
    const Native540Render::Program *programs[]={&Native540RenderFixture::vs,&Native540RenderFixture::fs};
    for(unsigned k=0;k<2;++k) {
        auto *p=reinterpret_cast<volatile uint32_t *>(bos_[k+1].address);
        for(unsigned i=0;i<programs[k]->info.codeWords;++i)p[i]=programs[k]->code[i];
    }
    auto *ib=reinterpret_cast<volatile uint32_t *>(bos_[0].address);
    for(unsigned i=0;i<written;++i)ib[i]=words[i];
    // Exact ranges, not DSB pretending to clean cached imported IOSurface data.
    stage(CachePublish);
    for(unsigned i:{0u,1u,2u,4u}){rc=cache(i,PublishBO);if(rc)return fail(rc,rc);}
    rc=cache(3,PublishSurface);if(rc)return fail(rc,rc);
    for(unsigned i=0;i<written;++i)if(ib[i]!=words[i])return fail(Mismatch);
    for(unsigned k=0;k<2;++k) {
        auto *p=reinterpret_cast<volatile const uint32_t *>(bos_[k+1].address);
        for(unsigned i=0;i<programs[k]->info.codeWords;++i)if(p[i]!=programs[k]->code[i])return fail(Mismatch);
    }
    stage(Submit);const NativeAdrenoAbiSubmitBuffer submitted[]={{bos_[0].handle,bos_[0].iova,uint64_t(written)*4}};
    record_.submitted=true;rc=client.submit(submitted,1,record_.fence);if(rc)return fail(rc,traced.rawrc);
    stage(Fence);rc=client.wait(record_.fence,200000000ull);if(rc)return fail(rc,traced.rawrc);
    record_.fenceSucceeded=true;
    // On any submit/wait failure NO lock, read, post-cache or teardown occurs.
    stage(ReadLock,3);rc=t.lock(t.context,surface_,true,record_.lockSeed);record_.readLockRC=rc;
    if(rc)return fail(rc,rc);record_.lock=ReadLocked;
    stage(LockedGetters,3);locked={};rc=t.getters(t.context,surface_,locked);if(rc)return fail(rc,rc);
    if(!sameSurface(record_.surface,locked))return fail(Invalid);
    stage(CacheAcquire,3);rc=cache(3,AcquireSurface);if(rc)return fail(rc,rc);
    rc=cache(4,AcquireBO);if(rc)return fail(rc,rc);
    record_.cacheAcquired=true;
    stage(Verify,3);const auto *scratch=reinterpret_cast<volatile const uint32_t *>(bos_[4].address);
    record_.clearTimestamp=scratch[0];record_.colorTimestamp=scratch[2];record_.ucheTimestamp=scratch[4];
    record_.pixelsRead=true;
    const bool verified=Native540Render::verify(reinterpret_cast<volatile const uint32_t *>(locked.base),16384,record_.pixels);
    const bool timestamps=record_.clearTimestamp==1&&record_.colorTimestamp==2&&record_.ucheTimestamp==3;
    // Preserve oracle failure as first error even if unlock subsequently fails.
    if(!verified||!timestamps)record_.error=Mismatch;
    stage(ReadUnlock,3);rc=t.unlock(t.context,surface_,true,record_.unlockSeed);record_.readUnlockRC=rc;
    if(!rc)record_.lock=Unlocked;
    if(!verified||!timestamps){record_.stage=Verify;return fail(Mismatch,rc);}
    if(rc)return fail(rc,rc);
    record_.readbackVerified=true;stage(Complete,3);
    if(t.record)t.record(t.context,record_);return 0;
}
int NativeMetal540Surface::retireResources() {
    if(retirementAttempted_)return NativeMetal540Retirement::AlreadyAttempted;
    retirementAttempted_=true;
    NativeMetal540Retirement::Transport t{};t.call=transport_.call;
    t.unmapView=transport_.unmapView;t.context=transport_.context;t.session=transport_.session;
    NativeMetal540Retirement ledger(t);
    int rc=0;
    if(!record_.readbackVerified||record_.lock!=Unlocked)ledger.markUncertain();
    else for(unsigned i=0;i<5;++i){
        const Bo &bo=bos_[i];NativeMetal540Retirement::Resource b{};
        b.handle=bo.handle;b.iova=bo.iova;b.address=bo.address;b.bytes=bo.bytes;
        b.token=bo.token;b.session=t.session;b.imported=i==3;b.cpuMapped=i!=3;
        rc=ledger.add(b);if(rc)break;
    }
    if(!rc)rc=ledger.retire(record_.fenceSucceeded,record_.cacheAcquired,record_.lock==Unlocked);
    record_.retirement=ledger.result();
    if(transport_.record)transport_.record(transport_.context,record_);
    return rc;
}
int NativeMetal540Surface::confirmConnectionRetired() {
    if(record_.retirementConfirmed)return 0;
    if(!record_.contextHeld||!record_.retirement.proof)return RetirementPending;
    record_.retirementConfirmed=true;
    if(surface_){transport_.releaseSurface(transport_.context,surface_);surface_=nullptr;record_.surfaceHeld=false;}
    record_.contextHeld=false;transport_.releaseContext(transport_.context);
    return 0;
}

#if defined(__OBJC__) && !defined(NATIVE540_SURFACE_SEMANTIC_ONLY)
#import <IOSurface/IOSurfaceRef.h>
#import <IOKit/IOKitLib.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <memory>
#include <stdio.h>
#if !__has_feature(objc_arc)
#error Native540SurfaceSession requires ARC
#endif
@implementation Native540SurfaceSession {
    io_connect_t _connection, _closedConnection;
    io_service_t _service;
    bool _closeAttempted, _ran; int _closeRC;
    void *_selfHold;
    Native540SurfaceRecord _recordSink; void *_recordContext; id _recordOwner;
    std::unique_ptr<NativeMetal540Surface> _engine;
}
- (instancetype)initWithService:(uint32_t)service error:(int *)error {
    return [self initWithService:service record:nullptr context:nullptr owner:nil error:error];
}
- (instancetype)initWithService:(uint32_t)service record:(Native540SurfaceRecord)sink
    context:(void *)context owner:(id)owner error:(int *)error {
    if(error)*error=0;
    if(!(self=[super init]))return nil;
    if(!service||(context&&!owner)){if(error)*error=NativeMetal540Surface::Invalid;return nil;}
    _recordSink=sink;_recordContext=context;_recordOwner=owner;
    const kern_return_t retained=IOObjectRetain(service);
    if(retained){if(error)*error=int(retained);return nil;}_service=service;
    const kern_return_t rc=IOServiceOpen(service,mach_task_self(),0x4147,&_connection);
    if(rc||!_connection){if(error)*error=rc?int(rc):NativeMetal540Surface::Invalid;return nil;}
    NativeMetal540Surface::Transport t{};t.context=(__bridge void *)self;
    t.exclusiveConnection=true;t.newBoCPUInhibit=true;t.mappedHoldsCoverAll=true;
    t.call=[](void *p,uint32_t method,const void *in,size_t n,void *out,size_t *bytes)->int {
        auto *s=(__bridge Native540SurfaceSession *)p;
        return int(IOConnectCallStructMethod(s->_connection,method,in,n,out,bytes));
    };
    t.mapView=[](void *p,uint32_t token,uint64_t &address,uint64_t &bytes)->int {
        auto *s=(__bridge Native540SurfaceSession *)p;mach_vm_address_t a=0;mach_vm_size_t n=0;
        const kern_return_t rc=IOConnectMapMemory64(s->_connection,token,mach_task_self(),&a,&n,kIOMapAnywhere);
        address=a;bytes=n;return int(rc); // mappings held by self/task until retirement.
    };
    t.unmapView=[](void *p,uint32_t token,uint64_t address,uint64_t bytes)->int {
        auto *s=(__bridge Native540SurfaceSession *)p;
        if(!token||!address||!bytes)return NativeMetal540Surface::Invalid;
        return int(IOConnectUnmapMemory64(s->_connection,token,mach_task_self(),address));
    };
    static std::atomic<uint64_t> sessionCounter{0};
    t.session=++sessionCounter;
    t.retainContext=[](void *p)->int {
        auto *s=(__bridge Native540SurfaceSession *)p;
        if(s->_selfHold)return NativeMetal540Surface::Invalid;
        s->_selfHold=(void *)CFBridgingRetain(s);return 0;
    };
    t.releaseContext=[](void *p) {
        auto *s=(__bridge Native540SurfaceSession *)p;void *hold=s->_selfHold;s->_selfHold=nullptr;
        if(hold){id owner=CFBridgingRelease(hold);(void)owner;}
    };
    t.create=[](void *,void *&owner)->int {
        // Private process-local object, never publish a port/ID or use global/pool.
        NSDictionary *properties=@{(__bridge id)kIOSurfaceWidth:@64,(__bridge id)kIOSurfaceHeight:@64,
            (__bridge id)kIOSurfaceBytesPerRow:@256,(__bridge id)kIOSurfaceBytesPerElement:@4,
            (__bridge id)kIOSurfaceElementWidth:@1,(__bridge id)kIOSurfaceElementHeight:@1,
            (__bridge id)kIOSurfacePixelFormat:@(NativeMetal540Surface::RGBA),
            (__bridge id)kIOSurfaceAllocSize:@16384};
        owner=(void *)IOSurfaceCreate((__bridge CFDictionaryRef)properties);
        return owner?0:NativeMetal540Surface::Invalid;
    };
    t.getters=[](void *,void *owner,NativeMetal540Surface::Layout &l)->int {
        IOSurfaceRef s=(IOSurfaceRef)owner;if(!s)return NativeMetal540Surface::Invalid;
        l={};l.surfaceID=IOSurfaceGetID(s);l.format=IOSurfaceGetPixelFormat(s);
        l.base=(uintptr_t)IOSurfaceGetBaseAddress(s);l.allocation=IOSurfaceGetAllocSize(s);
        l.width=IOSurfaceGetWidth(s);l.height=IOSurfaceGetHeight(s);l.pitch=IOSurfaceGetBytesPerRow(s);
        l.element=IOSurfaceGetBytesPerElement(s);l.planes=IOSurfaceGetPlaneCount(s);
        l.elementWidth=IOSurfaceGetElementWidth(s);l.elementHeight=IOSurfaceGetElementHeight(s);
        l.planeBase=(uintptr_t)IOSurfaceGetBaseAddressOfPlane(s,0);
        l.planeWidth=IOSurfaceGetWidthOfPlane(s,0);l.planeHeight=IOSurfaceGetHeightOfPlane(s,0);
        l.planePitch=IOSurfaceGetBytesPerRowOfPlane(s,0);l.planeElement=IOSurfaceGetBytesPerElementOfPlane(s,0);
        l.planeElementWidth=IOSurfaceGetElementWidthOfPlane(s,0);l.planeElementHeight=IOSurfaceGetElementHeightOfPlane(s,0);
        return 0;
    };
    t.lock=[](void *,void *s,bool read,uint32_t &seed)->int {
        return int(IOSurfaceLock((IOSurfaceRef)s,read?kIOSurfaceLockReadOnly:0,&seed));
    };
    t.unlock=[](void *,void *s,bool read,uint32_t &seed)->int {
        return int(IOSurfaceUnlock((IOSurfaceRef)s,read?kIOSurfaceLockReadOnly:0,&seed));
    };
    t.cache=[](void *,uint64_t address,uint64_t bytes,NativeMetal540Surface::CachePhase phase)->int {
        if(phase==NativeMetal540Surface::PublishSurface||phase==NativeMetal540Surface::AcquireSurface) {
            if(!address||(address&4095)||bytes!=16384)return NativeMetal540Surface::Invalid;
            // Publish may clean CPU initialization. Acquire is ordering only:
            // successful WaitFence has already invalidated the pinned pages.
            if(phase==NativeMetal540Surface::PublishSurface)
                sys_dcache_flush((void *)(uintptr_t)address,(size_t)bytes);
        }
        // Inhibit-cache NEWBO ordering ONLY; never substitute this for surface flush.
#if defined(__aarch64__)
        __asm__ volatile("dsb sy":::"memory");
#else
        std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
        return 0;
    };
    t.releaseSurface=[](void *,void *s){CFRelease((IOSurfaceRef)s);};
    t.record=[](void *p,const NativeMetal540Surface::Record &r) {
        auto *s=(__bridge Native540SurfaceSession *)p;
        if(s->_recordSink){auto forwarded=r;forwarded.connection=s->_connection;s->_recordSink(s->_recordContext,forwarded);return;}
        // Bounded defaults ONCE; integration sink owns persistent metadata.
        fprintf(stderr,"SF540 s%u e%d rc%d f%llu px%x/%x bad%u\n",unsigned(r.stage),r.error,r.rawrc,
            (unsigned long long)r.fence,r.pixels.center,r.pixels.corner,r.pixels.mismatches);
        fprintf(stderr,"SP in%u out%u edge%u ts%u/%u/%u cache%u lock%u/%d/%d/%d/%d\n",
            r.pixels.inside,r.pixels.outside,r.pixels.uncheckedEdge,r.clearTimestamp,r.colorTimestamp,
            r.ucheTimestamp,unsigned(r.cachephase),unsigned(r.lock),r.writeLockRC,r.writeUnlockRC,r.readLockRC,r.readUnlockRC);
        fprintf(stderr,"SI id%u base%llx alloc%llu pitch%llu iova%llx retain%d/%d retired%d\n",
            r.surface.surfaceID,(unsigned long long)r.surface.base,(unsigned long long)r.surface.allocation,
            (unsigned long long)r.surface.pitch,(unsigned long long)r.iova,r.contextHeld,r.surfaceHeld,r.retirementConfirmed);
        if(r.error)fprintf(stderr,"SE s%u e%d rc%d\n",unsigned(r.stage),r.error,r.rawrc);
    };
    _engine=std::make_unique<NativeMetal540Surface>(t);return self;
}
- (void)dealloc {
    // Only an UNUSED connection can reach this path without a confirmed gate.
    // A run's permanent self-hold keeps mappings/surface alive on every failure.
    if(!_ran&&_connection&&!_closeAttempted)IOServiceClose(_connection);
    if(_service)IOObjectRelease(_service);
}
- (int)runWithPrerequisites:(BOOL)succeeded {
    if(_closeAttempted)return NativeMetal540Surface::Invalid;
    const int rc=_engine->run(succeeded);_ran=_ran||_engine->result().contextHeld;return rc;
}
- (NativeMetal540Surface::Record)result {auto result=_engine->result();result.connection=_connection;return result;}
- (int)retire {
    if(_closeAttempted)return NativeMetal540Surface::RetirementPending;
    const int rc=_engine->retireResources();
    if(rc||!_engine->result().retirement.proof)return rc?rc:NativeMetal540Surface::RetirementPending;
    _closeAttempted=true;_closedConnection=_connection;
    _closeRC=int(IOServiceClose(_connection));
    if(_closeRC)return _closeRC;
    _connection=0;
    return _engine->confirmConnectionRetired();
}
- (int)retireWithConfirmation:(BOOL (^)(uint32_t))confirmation {
    if(!_closeAttempted) {
        _closeAttempted=true;_closedConnection=_connection;
        _closeRC=int(IOServiceClose(_connection));
        // No reuse even on failed close. No close retries/free/unmap.
    }
    if(_closeRC)return _closeRC;
    // An arbitrary caller callback never replaces the pre-close BO proof.
    if(!_engine->result().retirement.proof||!confirmation||!confirmation(_closedConnection))
        return NativeMetal540Surface::RetirementPending;
    return _engine->confirmConnectionRetired();
}
@end
#endif
