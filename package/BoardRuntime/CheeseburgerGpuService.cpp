// SPDX-License-Identifier: GPL-2.0-only
// Persistent A540 GPU service and user client. The wire ABI is the pinned
// sheng NativeAdrenoABI.h (ipados-17.7.11-21H461), so the userspace side can
// be the same native client the Metal plugin uses on sheng. The board probe
// starts the CP, GPMU and ZAP and leaves the ring, fence and engine alive;
// this service owns the user-visible buffer objects and submits their indirect
// buffers through that already-proven engine.
//
// Deliberately not implemented yet (they belong to the Metal frontend step):
// IOAcceleratorES publication, the sandbox-grant path, the mailbox transport,
// client VA windows and BoImport. Each unsupported method fails closed.
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOPlatformExpert.h>
#include <kern/clock.h>
#include <vm/memory_types.h>
#include <sys/proc.h>
#include <sys/kauth.h>
#include <mach/kmod.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSSymbol.h>
#include <string.h>
#include "NativeAdrenoABI.h"
#include "CheeseburgerGpuBoPolicy.h"

extern "C" {
volatile uint64_t native_adreno_service_stats[20];
volatile uint64_t native_adreno_client_uid=~0ull;
char native_adreno_client_name[17];
volatile int native_adreno_open_stage;
volatile uint64_t native_adreno_abi_opens;
// First dwords of the first submitted IB, copied from the kernel's own view.
volatile uint32_t native_adreno_ib_head[8];
// Up to four (slot, v0, v1, v2) rows read back from the mapped BOs after the
// submission: the user IB's memory writes land here even without a fence.
volatile uint32_t native_adreno_bo_dump[16];
// Hardware translation of every mapped BO: iova, physical page, leaf PTE, raw
// PAR and ATOS result. The write target is never in the submit list, so its
// leaf is the one a MEM_WRITE inside the IB depends on and the one never
// checked by the submission's own ATOS gate.
volatile uint32_t native_adreno_bo_atos[16];
extern volatile uint32_t native_adreno_submit_staged;
extern volatile uint64_t native_adreno_submit_stage_gva;
extern "C" void proc_selfname(char *, int);
char native_adreno_service_text[64];
volatile int native_adreno_service_ready;
int cheeseburger_gpu_engine_submit(const uint64_t *,const uint32_t *,unsigned,uint32_t *);
int cheeseburger_gpu_engine_wait(uint32_t,uint64_t);
int cheeseburger_gpu_engine_window(uint64_t *,uint64_t *);
int cheeseburger_gpu_engine_idle(void);
int cheeseburger_gpu_engine_failed(void);
int cheeseburger_gpu_engine_scratch(uint64_t *,volatile uint32_t **,unsigned *);
int cheeseburger_gpu_smmu_map(uint64_t,uint64_t,unsigned);
int cheeseburger_gpu_smmu_unmap(uint64_t,unsigned);
int cheeseburger_gpu_smmu_lookup(uint64_t,uint64_t *);
int cheeseburger_gpu_smmu_atos(uint64_t,uint64_t *,volatile uint64_t *);
void flush_dcache(vm_offset_t,unsigned,boolean_t);
void pmap_set_cache_attributes(ppnum_t,unsigned int);
}

namespace {
enum : uint32_t {OpenType=0x4147, BoTokenBase=0x100, MaxBo=64, AbiInputBits=36, AbiOutputBits=36};
IOService *gService=nullptr;
IOLock *gGateLock=nullptr;
CheeseburgerGpu::ClientGate gGate;
// A BO whose SMMU leaf could not be torn down is retained with its pages. Its
// IOVA must never be handed to a later connection: the CP may still hold that
// translation. The gate alone is not enough, because it is released when the
// owning client closes while the mapping stays.
struct RetainedRange { uint64_t iova=0, bytes=0; };
enum { MaxRetained=MaxBo };
RetainedRange gRetained[MaxRetained];
unsigned gRetainedCount=0;
void retainIova(uint64_t iova,uint64_t bytes) {
    if(!gGateLock||!iova||!bytes)return;
    IOLockLock(gGateLock);
    for(unsigned i=0;i<gRetainedCount;++i)
        if(gRetained[i].iova==iova){IOLockUnlock(gGateLock);return;}
    if(gRetainedCount<MaxRetained)gRetained[gRetainedCount++]={iova,bytes};
    IOLockUnlock(gGateLock);
}
}

class AGXDeviceUserClient : public IOUserClient {
    OSDeclareDefaultStructors(AGXDeviceUserClient)
    struct Bo {
        uint64_t handle=0,bytes=0,iova=0,physical=0;
        IOBufferMemoryDescriptor *buf=nullptr;
        IOMemoryDescriptor *desc=nullptr;
        uint32_t token=0;
        bool mapped=false,cpuMapped=false;
    };
    Bo *bos_=nullptr;
    IOLock *lock_=nullptr;
    bool live_=false;
    bool ownsClient_=false;
    bool abi_=false;
    uint64_t generation_=0;
    task_t task_=TASK_NULL;
    static IOReturn action(OSObject *,void *,IOExternalMethodArguments *);
    IOReturn run(uint32_t,const void *,size_t,void *,size_t);
    IOReturn query(NativeAdrenoAbiParams &);
    IOReturn deviceInfo(NativeAdrenoAbiDeviceInfo &);
    IOReturn boAlloc(const NativeAdrenoAbiBoAlloc &,NativeAdrenoAbiBoAllocReply &);
    IOReturn boMap(const NativeAdrenoAbiBoMap &,NativeAdrenoAbiBoMapReply &);
    IOReturn boFree(uint64_t);
    IOReturn boCpuMap(uint64_t,NativeAdrenoAbiBoCpuMapReply &);
    IOReturn boCpuUnmap(uint64_t,uint32_t);
    IOReturn submit(const NativeAdrenoAbiSubmit &,NativeAdrenoAbiSubmitReply &);
    IOReturn waitFence(const NativeAdrenoAbiWaitFence &);
    Bo *find(uint64_t);
    void dropView(Bo &);
    bool freeBo(Bo &);
public:
    void setGeneration(uint64_t generation) { generation_=generation; ownsClient_=true; }
    void setAbi(bool abi) { abi_=abi; }
    bool initWithTask(task_t,void *,UInt32,OSDictionary *) override;
    bool start(IOService *) override;
    void stop(IOService *) override;
    void free() override;
    IOReturn clientClose() override;
    IOReturn clientMemoryForType(UInt32,IOOptionBits *,IOMemoryDescriptor **) override;
    IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,OSObject *,void *) override;
};

class NativeAdrenoGpuService : public IOService {
    OSDeclareDefaultStructors(NativeAdrenoGpuService)
public:
    IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **) override;
};

OSDefineMetaClassAndStructors(NativeAdrenoGpuService,IOService)
OSDefineMetaClassAndStructors(AGXDeviceUserClient,IOUserClient)

extern "C" IOReturn cheeseburger_gpu_open_client(IOService *,task_t,void *,UInt32,OSDictionary *,
    IOUserClient **);

// Bounded msgbuf trail: the snapshot can stop before the discovery runs, but
// msgbuf and the heartbeat usually continue. Log the first few outcomes only.
static void open_trail(const char *what,unsigned stage,UInt32 type,bool abi) {
    static unsigned seen[9][2];
    unsigned slot=stage<9?stage:8, kind=abi?1:0;
    if(seen[slot][kind]++>=3)return;
    IOLog("CB GPU open: %s stage=%u type=%x abi=%d name=%s ready=%d gate=%d\n",
        what,stage,(unsigned)type,(int)abi,native_adreno_client_name,
        (int)native_adreno_service_ready,(int)gGate.open);
}


IOReturn NativeAdrenoGpuService::newUserClient(task_t owningTask,void *securityID,UInt32 type,
    OSDictionary *properties,IOUserClient **handler) {
    return cheeseburger_gpu_open_client(this,owningTask,securityID,type,properties,handler);
}

// Shared by the board service and the registry-visible accelerator: the client
// binds to whichever provider the discovery opened, while the ring, gate and
// statistics stay global to this driver.
extern "C" IOReturn cheeseburger_gpu_open_client(IOService *provider,task_t owningTask,void *securityID,
    UInt32 type,OSDictionary *properties,IOUserClient **handler) {
    if(handler)*handler=nullptr;
    if(!handler)return kIOReturnBadArgument;
    // Record who reaches the driver, for markers and connections alike; the
    // discovery and the plugin can live in different processes.
    proc_selfname(native_adreno_client_name,sizeof native_adreno_client_name);
    // Only 0x4147xxxx with a nonzero low half is a progress marker. Every
    // other type is a connection: the original IOGPU library opens type 1 and
    // the owner plugin opens 0x4147, and the fixed record ABI does not depend
    // on it (the upstream client accepts any type the same way).
    if((type>>16)==OpenType&&(type&0xffff)){
        native_adreno_service_stats[16]=native_adreno_service_stats[16]+1;
        native_adreno_service_stats[17]=type&0xffff;
        return kIOReturnUnsupported;
    }
    if(cheeseburger_gpu_engine_failed()){
        // A failed submission may have stalled the CP/bus; refuse new work until
        // reboot. Busy makes the plugin exit instead of retrying for minutes.
        native_adreno_open_stage=7;
        native_adreno_service_stats[19]=native_adreno_service_stats[19]+1;
        return kIOReturnBusy;
    }
    if(!native_adreno_service_ready){
        open_trail("refuse",1,type,type==OpenType);
        native_adreno_open_stage=1;
        native_adreno_service_stats[19]=native_adreno_service_stats[19]+1;
        return kIOReturnNotReady;
    }
    // The original IOGPU device client (the discovery's type-1 open) may stay
    // connected while the Metal plugin runs; it must not take the ABI gate,
    // because the ring/SMMU/BOs belong to the one 0x4147 client. Such a client
    // is created but every ABI method and memory view is refused.
    bool abi=(type==OpenType);
    uint64_t generation=0;
    if(abi) {
        // One ABI client at a time: the ring, the SMMU mappings and the CPU
        // views are shared, so a second connection could overwrite an IOVA or
        // race a submit with a teardown. Fail closed instead. The generation
        // makes a late close of an older client harmless to a newer one.
        if(!gGateLock){native_adreno_service_stats[19]=native_adreno_service_stats[19]+1;return kIOReturnNotReady;}
        IOLockLock(gGateLock);
        bool retained=gRetainedCount!=0;
        bool acquired=retained?false:gGate.acquire(generation);
        IOLockUnlock(gGateLock);
        if(retained){
            // An earlier teardown kept a leaf entry alive; a new client could
            // map the same IOVA and replace a translation still in use.
            open_trail("refuse",8,type,true);
            native_adreno_open_stage=8;
            native_adreno_service_stats[19]=native_adreno_service_stats[19]+1;
            return kIOReturnBusy;
        }
        if(!acquired){
            open_trail("refuse",2,type,true);
            native_adreno_open_stage=2;
            native_adreno_service_stats[19]=native_adreno_service_stats[19]+1;
            return kIOReturnBusy;
        }
    }
    AGXDeviceUserClient *client=new AGXDeviceUserClient;
    if(!client){
        open_trail("refuse",3,type,abi);
        native_adreno_open_stage=3;
        if(abi){IOLockLock(gGateLock);gGate.release(generation);IOLockUnlock(gGateLock);}
        return kIOReturnNoMemory;
    }
    if(abi) client->setGeneration(generation);
    client->setAbi(abi);
    if(!client->initWithTask(owningTask,securityID,type,properties)) {
        open_trail("refuse",4,type,abi);
        native_adreno_open_stage=4;
    } else if(!client->attach(provider)) {
        open_trail("refuse",5,type,abi);
        native_adreno_open_stage=5;
    } else if(!client->start(provider)) {
        open_trail("refuse",6,type,abi);
        native_adreno_open_stage=6;
    } else {
        native_adreno_open_stage=0;
        if(abi) native_adreno_abi_opens=native_adreno_abi_opens+1;
        open_trail("ok",0,type,abi);
        *handler=client;
        return kIOReturnSuccess;
    }
    client->release();
    if(abi){IOLockLock(gGateLock);gGate.release(generation);IOLockUnlock(gGateLock);}
    native_adreno_service_stats[19]=native_adreno_service_stats[19]+1;
    return kIOReturnNoResources;
}

bool AGXDeviceUserClient::initWithTask(task_t ownerTask,void *security,UInt32 type,OSDictionary *properties) {
    if(!ownerTask)return false;
    // The factory job that starts the owner probe runs as mobile (501), while
    // the original policy only accepted the administrator token. Keep root as
    // the general rule and admit exactly that device user; the single-client
    // gate below still serializes every open.
    uint64_t uid=~0ull;
    proc_t owner=(proc_t)get_bsdtask_info(ownerTask);
    kauth_cred_t cred=owner?kauth_cred_proc_ref(owner):nullptr;
    if(cred){uid=kauth_cred_getuid(cred);kauth_cred_unref(&cred);}
    native_adreno_client_uid=uid;
    bool privileged=clientHasPrivilege(security,kIOClientPrivilegeAdministrator)==kIOReturnSuccess;
    if((!privileged&&uid!=501)||
       !IOUserClient::initWithTask(ownerTask,security,type,properties))return false;
    task_=ownerTask;task_reference(task_);
    return true;
}
bool AGXDeviceUserClient::start(IOService *provider) {
    if(!IOUserClient::start(provider))return false;
    if(!abi_) {
        // The discovery's own IOGPU device client: connected, no ring state.
        live_=true;
        native_adreno_service_stats[0]=native_adreno_service_stats[0]+1;
        IOLog("CB GPU service: observer client open task=%p\n",(void *)task_);
        return true;
    }
    lock_=IOLockAlloc();
    bos_=IONewZero(Bo,MaxBo);
    if(!lock_||!bos_)return false;
    live_=true;
    native_adreno_service_stats[0]=native_adreno_service_stats[0]+1;
    IOLog("CB GPU service: client open task=%p\n",(void *)task_);
    return true;
}
void AGXDeviceUserClient::stop(IOService *provider) {
    live_=false;
    IOUserClient::stop(provider);
}
void AGXDeviceUserClient::dropView(Bo &bo) {
    if(!bo.desc)return;
    while(removeMappingForDescriptor(bo.desc)) {
        native_adreno_service_stats[9]=native_adreno_service_stats[9]+1;
    }
}
bool AGXDeviceUserClient::freeBo(Bo &bo) {
    if(!bo.handle)return true;
    dropView(bo);
    auto idle=[](){ return cheeseburger_gpu_engine_idle(); };
    auto unmap=[&](){ return cheeseburger_gpu_smmu_unmap(bo.iova,unsigned(bo.bytes/4096)); };
    if(!CheeseburgerGpu::releaseMappedBuffer(bo.mapped,idle,unmap)){
        // The pages may still be reachable from the GPU, or the SMMU could not
        // be updated: keep the object and its mapping, and retire the IOVA for
        // the rest of the boot so no later client can overwrite its leaf PTE.
        if(bo.mapped)retainIova(bo.iova,bo.bytes);
        native_adreno_service_stats[9]=native_adreno_service_stats[9]+1;
        return false;
    }
    if(bo.mapped)native_adreno_service_stats[5]=native_adreno_service_stats[5]+1;
    OSSafeReleaseNULL(bo.desc);
    // Restore the VM's default page attributes only after the GPU can no
    // longer access this BO. This mirrors sheng's owned BO lifecycle.
    if(bo.physical)
        for(uint64_t off=0;off<bo.bytes;off+=4096)
            pmap_set_cache_attributes((ppnum_t)((bo.physical+off)>>12),VM_WIMG_USE_DEFAULT);
    OSSafeReleaseNULL(bo.buf);
    bo=Bo();
    return true;
}
void AGXDeviceUserClient::free() {
    if(ownsClient_){
        if(gGateLock){IOLockLock(gGateLock);gGate.release(generation_);IOLockUnlock(gGateLock);}
        ownsClient_=false;
    }
    if(task_){task_deallocate(task_);task_=TASK_NULL;}
    if(bos_) {
        for(unsigned i=0;i<MaxBo;++i)freeBo(bos_[i]);
        IODelete(bos_,Bo,MaxBo);bos_=nullptr;
    }
    if(lock_){IOLockFree(lock_);lock_=nullptr;}
    IOUserClient::free();
}
IOReturn AGXDeviceUserClient::clientClose() {
    if(bos_)for(unsigned i=0;i<MaxBo;++i)freeBo(bos_[i]);
    if(live_){live_=false;native_adreno_service_stats[1]=native_adreno_service_stats[1]+1;}
    if(ownsClient_){
        if(gGateLock){IOLockLock(gGateLock);gGate.release(generation_);IOLockUnlock(gGateLock);}
        ownsClient_=false;
    }
    terminate();
    return kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::clientMemoryForType(UInt32 type,IOOptionBits *options,IOMemoryDescriptor **memory) {
    if(memory)*memory=nullptr;
    if(!abi_)return kIOReturnUnsupported;
    if(!memory||!options||!lock_)return kIOReturnBadArgument;
    IOLockLock(lock_);
    Bo *bo=nullptr;
    for(unsigned i=0;i<MaxBo;++i)
        if(bos_[i].handle && bos_[i].token==type){bo=&bos_[i];break;}
    if(!bo||!bo->desc){IOLockUnlock(lock_);return kIOReturnNotFound;}
    // The BO pages are cached in the kernel and the GPU's PTEs are
    // non-cacheable, so the client gets an uncached view of the same
    // descriptor. Returning the same descriptor instance matters:
    // IOConnectUnmapMemory64 asks for it again to find the mapping.
    bo->desc->retain();
    *memory=bo->desc;
    *options=kIOMapAnywhere|kIOMapInhibitCache;
    native_adreno_service_stats[18]=*options;
    bo->cpuMapped=true;
    IOLockUnlock(lock_);
    return kIOReturnSuccess;
}

AGXDeviceUserClient::Bo *AGXDeviceUserClient::find(uint64_t handle) {
    if(!handle||!bos_)return nullptr;
    for(unsigned i=0;i<MaxBo;++i)
        if(bos_[i].handle==handle)return &bos_[i];
    return nullptr;
}
IOReturn AGXDeviceUserClient::query(NativeAdrenoAbiParams &p) {
    memset(&p,0,sizeof p);
    p.abi_version=NativeAdrenoAbiVersion;
    p.input_bits=AbiInputBits;p.output_bits=AbiOutputBits;
    p.flags=NativeAdrenoDeviceClientIova;
    p.max_bo_bytes=0; // filled by the caller's window below
    uint64_t start=0,bytes=0;
    if(cheeseburger_gpu_engine_window(&start,&bytes)==0)p.max_bo_bytes=bytes;
    return kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::deviceInfo(NativeAdrenoAbiDeviceInfo &d) {
    uint64_t start=0,bytes=0;
    if(cheeseburger_gpu_engine_window(&start,&bytes))return kIOReturnNotReady;
    memset(&d,0,sizeof d);
    // A540. GMEM/UBWC fields stay zero until they are read from the vendor
    // configuration; the submission test uses only the VA window, and the
    // Metal step must fill the rest from the pinned A540 catalog.
    d.chip_id=0x05040001u;
    d.va_start=start;
    d.va_size=bytes;
    d.flags=NativeAdrenoDeviceClientIova;
    return kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::boAlloc(const NativeAdrenoAbiBoAlloc &a,NativeAdrenoAbiBoAllocReply &r) {
    uint64_t start=0,window=0;
    if(cheeseburger_gpu_engine_window(&start,&window)||!window)return kIOReturnNotReady;
    if(!a.bytes||(a.bytes&4095)||a.bytes>window||a.align_log2<12||a.align_log2>24||!a.contiguous)
        return kIOReturnBadArgument;
    unsigned index=MaxBo;
    for(unsigned i=0;i<MaxBo;++i)if(!bos_[i].handle){index=i;break;}
    if(index==MaxBo)return kIOReturnNoResources;
    IOBufferMemoryDescriptor *buf=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
        kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,(vm_size_t)a.bytes,4096);
    if(!buf)return kIOReturnNoMemory;
    IOByteCount span=0;
    uint64_t pa=buf->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
    if(!pa||(pa&4095)||span<a.bytes||pa+a.bytes>(1ull<<36)){buf->release();return kIOReturnNoMemory;}
    void *cpu=buf->getBytesNoCopy();
    if(!cpu){buf->release();return kIOReturnNoMemory;}
    // Publish initialized cache lines before the pages are GPU-visible. The
    // WCOMB attribute change from b9f698f is removed: a user-owned page that is
    // not GPU-writable (GPAT rc=-4, boot 0x561a49ee) is the last difference
    // from a working kernel page, and the SMMU leaf attributes are identical.
    flush_dcache((vm_offset_t)cpu,(unsigned)a.bytes,FALSE);
    Bo &bo=bos_[index];
    bo=Bo();
    bo.handle=uint64_t(index)+1;
    bo.bytes=a.bytes;
    bo.physical=pa;
    bo.buf=buf;
    // One descriptor object per BO: clientMemoryForType must return this same
    // instance so IOConnectUnmapMemory64 can find and remove the mapping. The
    // descriptor describes the same raw physical range the SMMU maps; its
    // pages stay alive through bo.buf.
    IOAddressRange range={pa,a.bytes};
    bo.desc=IOMemoryDescriptor::withAddressRanges(&range,1,
        static_cast<IOOptionBits>(kIODirectionInOut)|static_cast<IOOptionBits>(kIOMemoryTypePhysical64),nullptr);
    if(!bo.desc){
        buf->release();bo=Bo();return kIOReturnNoMemory;
    }
    bo.token=BoTokenBase+index;
    r.handle=bo.handle;
    native_adreno_service_stats[2]=native_adreno_service_stats[2]+1;
    native_adreno_service_stats[12]=native_adreno_service_stats[12]+a.bytes;
    return kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::boMap(const NativeAdrenoAbiBoMap &m,NativeAdrenoAbiBoMapReply &r) {
    uint64_t start=0,window=0;
    if(cheeseburger_gpu_engine_window(&start,&window))return kIOReturnNotReady;
    Bo *bo=find(m.handle);
    if(!bo||!bo->buf)return kIOReturnBadArgument;
    if(!(m.permissions&1)||m.reserved||(m.iova&4095)||m.iova<start||m.iova+bo->bytes>start+window)
        return kIOReturnBadArgument;
    if(bo->mapped)return bo->iova==m.iova?kIOReturnSuccess:kIOReturnBusy;
    // No two live BOs may claim the same GPU addresses: the SMMU leaf entries
    // are updated under this client's lock, and a duplicate range would let a
    // later free tear down another object's mapping.
    for(unsigned i=0;i<MaxBo;++i){
        const Bo &o=bos_[i];
        if(!o.handle||!o.mapped)continue;
        if(m.iova<o.iova+o.bytes&&o.iova<m.iova+bo->bytes)return kIOReturnBusy;
    }
    IOByteCount span=0;
    uint64_t pa=bo->buf->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
    if(!pa||span<bo->bytes)return kIOReturnNoMemory;
    int rc=cheeseburger_gpu_smmu_map(m.iova,pa,unsigned(bo->bytes/4096));
    if(rc){
        // A failed map should leave no entry, but if the rollback cannot sync
        // the TLB the IOVA is retired rather than offered to another map.
        if(cheeseburger_gpu_smmu_unmap(m.iova,unsigned(bo->bytes/4096)))
            retainIova(m.iova,bo->bytes);
        native_adreno_service_stats[9]=native_adreno_service_stats[9]+1;
        return kIOReturnNoMemory;
    }
    bo->iova=m.iova;bo->mapped=true;
    r.iova=m.iova;
    native_adreno_service_stats[4]=native_adreno_service_stats[4]+1;
    native_adreno_service_stats[13]=native_adreno_service_stats[13]+bo->bytes/4096;
    return kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::boFree(uint64_t handle) {
    Bo *bo=find(handle);
    if(!bo)return kIOReturnBadArgument;
    // freeBo applies the idle/stop and SMMU/TLB rule and keeps the buffer on
    // failure; a busy engine returns kIOReturnIOError without freeing.
    if(!freeBo(*bo)){
        native_adreno_service_stats[9]=native_adreno_service_stats[9]+1;
        return kIOReturnIOError;
    }
    native_adreno_service_stats[3]=native_adreno_service_stats[3]+1;
    return kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::boCpuMap(uint64_t handle,NativeAdrenoAbiBoCpuMapReply &r) {
    Bo *bo=find(handle);
    if(!bo||!bo->desc)return kIOReturnBadArgument;
    r.token=bo->token;r.reserved=0;r.bytes=bo->bytes;
    return kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::boCpuUnmap(uint64_t handle,uint32_t token) {
    Bo *bo=find(handle);
    if(!bo||bo->token!=token)return kIOReturnBadArgument;
    dropView(*bo);
    bo->cpuMapped=false;
    return kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::submit(const NativeAdrenoAbiSubmit &s,NativeAdrenoAbiSubmitReply &r) {
    if(s.buffer_count<1||s.buffer_count>NativeAdrenoAbiMaxBuffers||s.reserved)
        return kIOReturnBadArgument;
    if(s.wait_fence && cheeseburger_gpu_engine_wait(uint32_t(s.wait_fence),s.wait_timeout_ns?s.wait_timeout_ns:1000000000ull))
        return kIOReturnTimeout;
    uint64_t vas[NativeAdrenoAbiMaxBuffers];
    uint32_t dwords[NativeAdrenoAbiMaxBuffers];
    for(unsigned i=0;i<s.buffer_count;++i) {
        const NativeAdrenoAbiSubmitBuffer &b=s.buffers[i];
        Bo *bo=find(b.handle);
        if(!bo||!bo->mapped||b.iova!=bo->iova||!b.bytes||(b.bytes&3)||b.bytes>bo->bytes)
            return kIOReturnBadArgument;
        vas[i]=b.iova;dwords[i]=uint32_t(b.bytes/4);
    }
    // Read-only hardware walk of every mapped BO (ATOS halts/resumes the
    // micro-MMU but rings no doorbell). This covers the write target, which is
    // not part of the submission and was never translated on hardware before.
    { unsigned n=0;
      for(unsigned i=0;i<MaxBo&&n<2;++i){ Bo &b=bos_[i];
        if(!b.handle||!b.mapped)continue;
        uint64_t pte=0,par=0;int lk=cheeseburger_gpu_smmu_lookup(b.iova,&pte);
        int rc=lk?lk:cheeseburger_gpu_smmu_atos(b.iova,&par,nullptr);
        volatile uint32_t *r=&native_adreno_bo_atos[n*8];
        r[0]=uint32_t(b.iova);r[1]=uint32_t(b.physical);r[2]=uint32_t(b.physical>>32);
        r[3]=uint32_t(pte);r[4]=uint32_t(pte>>32);r[5]=uint32_t(par);r[6]=uint32_t(par>>32);
        r[7]=uint32_t(uint16_t(uint32_t(rc)));
        IOLog("CB GPU engine: bo atos%d iova=%llx pa=%llx pte=%llx rc=%d par=%llx\n",
         n,(unsigned long long)b.iova,(unsigned long long)b.physical,
         (unsigned long long)pte,rc,(unsigned long long)par);
        ++n; }
      native_adreno_bo_atos[15]=n; }
    uint32_t seq=0;
    // Record the first IB from the kernel's own view. The submit itself uses
    // the client's mapping again: the staging copy has served its purpose and
    // the user path must run unmodified.
    for(unsigned i=0;i<8;++i)native_adreno_ib_head[i]=0;
    native_adreno_submit_staged=0;
    { Bo *ib=find(s.buffers[0].handle);
      const uint32_t *src=ib&&ib->buf?(const uint32_t*)ib->buf->getBytesNoCopy():nullptr;
      __asm__ volatile("dsb sy":::"memory");
      if(src)for(unsigned i=0;i<8;++i)native_adreno_ib_head[i]=src[i];
    }
    // Publish all mapped objects, not just IBs: an IB can reference another
    // BO as data or as its GPU writeback destination.
    __asm__ volatile("dsb sy":::"memory");
    int rc=cheeseburger_gpu_engine_submit(vas,dwords,s.buffer_count,&seq);
    // Kernel-side readback of every mapped BO: the IB's write target is one of
    // them, and the values show whether the commands ran even without a fence.
    { unsigned n=0;
      for(unsigned i=0;i<MaxBo&&n<4;++i){ Bo &b=bos_[i];
        if(!b.handle||!b.mapped||!b.buf)continue;
        const uint32_t *p=(const uint32_t*)b.buf->getBytesNoCopy(); if(!p)continue;
        __asm__ volatile("dsb sy":::"memory");
        flush_dcache((vm_offset_t)p,64,FALSE);
        __asm__ volatile("dsb sy":::"memory");
        native_adreno_bo_dump[n*4]=i;native_adreno_bo_dump[n*4+1]=p[0];
        native_adreno_bo_dump[n*4+2]=p[1];native_adreno_bo_dump[n*4+3]=p[2];++n; }
      native_adreno_bo_dump[15]=n; }
    native_adreno_service_stats[6]=native_adreno_service_stats[6]+1;
    if(rc){native_adreno_service_stats[9]=native_adreno_service_stats[9]+1;return kIOReturnIOError;}
    r.fence=seq;
    native_adreno_service_stats[8]=native_adreno_service_stats[8]+1;
    return kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::waitFence(const NativeAdrenoAbiWaitFence &w) {
    native_adreno_service_stats[7]=native_adreno_service_stats[7]+1;
    if(!w.fence)return kIOReturnBadArgument;
    uint64_t timeout=w.timeout_ns?w.timeout_ns:1000000ull;
    int rc=cheeseburger_gpu_engine_wait(uint32_t(w.fence),timeout);
    return rc?kIOReturnTimeout:kIOReturnSuccess;
}
IOReturn AGXDeviceUserClient::run(uint32_t method,const void *input,size_t input_size,void *output,size_t output_size) {
    if(!live_||!lock_)return kIOReturnNotReady;
    if(cheeseburger_gpu_engine_failed())return kIOReturnNotReady;
    native_adreno_service_stats[15]=native_adreno_service_stats[15]+1;
    native_adreno_service_stats[11]=method;
    IOLockLock(lock_);
    IOReturn status=kIOReturnBadArgument;
    switch(method) {
    case NativeAdrenoMethodQuery: {
        if(input_size||output_size!=sizeof(NativeAdrenoAbiParams))break;
        NativeAdrenoAbiParams p{};status=query(p);
        if(!status)memcpy(output,&p,sizeof p);
        break;}
    case NativeAdrenoMethodGetDeviceInfo: {
        if(input_size||output_size!=sizeof(NativeAdrenoAbiDeviceInfo))break;
        NativeAdrenoAbiDeviceInfo d{};status=deviceInfo(d);
        if(!status)memcpy(output,&d,sizeof d);
        break;}
    case NativeAdrenoMethodBoAlloc: {
        if(input_size!=sizeof(NativeAdrenoAbiBoAlloc)||output_size!=sizeof(NativeAdrenoAbiBoAllocReply))break;
        NativeAdrenoAbiBoAlloc a{};NativeAdrenoAbiBoAllocReply r{};
        memcpy(&a,input,sizeof a);status=boAlloc(a,r);
        if(!status)memcpy(output,&r,sizeof r);
        break;}
    case NativeAdrenoMethodBoMap: {
        if(input_size!=sizeof(NativeAdrenoAbiBoMap)||output_size!=sizeof(NativeAdrenoAbiBoMapReply))break;
        NativeAdrenoAbiBoMap m{};NativeAdrenoAbiBoMapReply r{};
        memcpy(&m,input,sizeof m);status=boMap(m,r);
        if(!status)memcpy(output,&r,sizeof r);
        break;}
    case NativeAdrenoMethodBoFree: {
        if(input_size!=sizeof(NativeAdrenoAbiBoFree)||output_size)break;
        NativeAdrenoAbiBoFree f{};memcpy(&f,input,sizeof f);status=boFree(f.handle);
        break;}
    case NativeAdrenoMethodBoCpuMap: {
        if(input_size!=sizeof(NativeAdrenoAbiBoCpuMap)||output_size!=sizeof(NativeAdrenoAbiBoCpuMapReply))break;
        NativeAdrenoAbiBoCpuMap m{};NativeAdrenoAbiBoCpuMapReply r{};
        memcpy(&m,input,sizeof m);status=boCpuMap(m.handle,r);
        if(!status)memcpy(output,&r,sizeof r);
        break;}
    case NativeAdrenoMethodBoCpuUnmap: {
        if(input_size!=sizeof(NativeAdrenoAbiBoCpuUnmap)||output_size)break;
        NativeAdrenoAbiBoCpuUnmap u{};memcpy(&u,input,sizeof u);status=boCpuUnmap(u.handle,u.token);
        break;}
    case NativeAdrenoMethodSubmit: {
        if(input_size!=sizeof(NativeAdrenoAbiSubmit)||output_size!=sizeof(NativeAdrenoAbiSubmitReply))break;
        NativeAdrenoAbiSubmit s{};NativeAdrenoAbiSubmitReply r{};
        memcpy(&s,input,sizeof s);status=submit(s,r);
        if(!status)memcpy(output,&r,sizeof r);
        break;}
    case NativeAdrenoMethodWaitFence: {
        if(input_size!=sizeof(NativeAdrenoAbiWaitFence)||output_size)break;
        NativeAdrenoAbiWaitFence w{};memcpy(&w,input,sizeof w);status=waitFence(w);
        break;}
    case NativeAdrenoMethodGetTimestamp: {
        if(input_size||output_size!=sizeof(NativeAdrenoAbiTimestamp))break;
        NativeAdrenoAbiTimestamp t{};t.ticks=mach_absolute_time();
        memcpy(output,&t,sizeof t);status=kIOReturnSuccess;
        break;}
    case NativeAdrenoMethodGetStatus: {
        if(input_size||output_size!=sizeof(NativeAdrenoAbiStatus))break;
        NativeAdrenoAbiStatus s{};s.faults=native_adreno_service_stats[9];s.suspends=0;
        memcpy(output,&s,sizeof s);status=kIOReturnSuccess;
        break;}
    case NativeAdrenoMethodSetMetadata: {
        if(input_size!=sizeof(NativeAdrenoAbiMetadata)||output_size)break;
        NativeAdrenoAbiMetadata m{};memcpy(&m,input,sizeof m);
        uint32_t n=m.bytes<sizeof(native_adreno_service_text)-1?m.bytes:uint32_t(sizeof(native_adreno_service_text)-1);
        memset(native_adreno_service_text,0,sizeof native_adreno_service_text);
        if(m.text&&n)memcpy(native_adreno_service_text,m.text,n);
        native_adreno_service_stats[14]=n;
        status=kIOReturnSuccess;
        break;}
    case NativeAdrenoMethodGetMetadata: {
        if(input_size!=sizeof(NativeAdrenoAbiMetadata)||output_size!=sizeof(NativeAdrenoAbiMetadata))break;
        NativeAdrenoAbiMetadata m{};memcpy(&m,input,sizeof m);
        uint32_t n=m.bytes<sizeof(native_adreno_service_text)?m.bytes:uint32_t(sizeof(native_adreno_service_text));
        m.bytes=0;memset(m.text,0,sizeof m.text);
        if(n){memcpy(m.text,native_adreno_service_text,n);m.bytes=n;}
        memcpy(output,&m,sizeof m);status=kIOReturnSuccess;
        break;}
    case NativeAdrenoMethodCreateQueue: {
        if(input_size!=sizeof(NativeAdrenoAbiQueue)||output_size!=sizeof(NativeAdrenoAbiQueueReply))break;
        NativeAdrenoAbiQueueReply r{};r.queue=1;
        memcpy(output,&r,sizeof r);status=kIOReturnSuccess;
        break;}
    case NativeAdrenoMethodCloseQueue: {
        if(input_size!=sizeof(NativeAdrenoAbiQueue)||output_size)break;
        NativeAdrenoAbiQueue q{};memcpy(&q,input,sizeof q);
        status=q.queue==1?kIOReturnSuccess:kIOReturnBadArgument;
        break;}
    case NativeAdrenoMethodBoImport:
        status=kIOReturnUnsupported;
        break;
    default:
        status=kIOReturnUnsupported;
        break;
    }
    IOLockUnlock(lock_);
    native_adreno_service_stats[10]=uint32_t(status);
    return status;
}
IOReturn AGXDeviceUserClient::action(OSObject *target,void *,IOExternalMethodArguments *arguments) {
    AGXDeviceUserClient *client=OSDynamicCast(AGXDeviceUserClient,target);
    if(!client||!arguments)return kIOReturnBadArgument;
    return client->run(arguments->selector,arguments->structureInput,arguments->structureInputSize,
        arguments->structureOutput,arguments->structureOutputSize);
}
IOReturn AGXDeviceUserClient::externalMethod(uint32_t selector,IOExternalMethodArguments *arguments,
    IOExternalMethodDispatch *,OSObject *target,void *reference) {
    // Only the 0x4147 client owns the ring, the BOs and the mappings; the
    // discovery's other-type device client stays connected but sees no ABI.
    if(!abi_)return kIOReturnUnsupported;
    static const IOExternalMethodDispatch table[]={
        {&action,0,0,0,sizeof(NativeAdrenoAbiParams)},
        {&action,0,sizeof(NativeAdrenoAbiBoAlloc),0,sizeof(NativeAdrenoAbiBoAllocReply)},
        {&action,0,sizeof(NativeAdrenoAbiBoMap),0,sizeof(NativeAdrenoAbiBoMapReply)},
        {&action,0,sizeof(NativeAdrenoAbiBoFree),0,0},
        {&action,0,sizeof(NativeAdrenoAbiSubmit),0,sizeof(NativeAdrenoAbiSubmitReply)},
        {&action,0,sizeof(NativeAdrenoAbiWaitFence),0,0},
        {&action,0,0,0,sizeof(NativeAdrenoAbiTimestamp)},
        {&action,0,0,0,sizeof(NativeAdrenoAbiStatus)},
        {&action,0,sizeof(NativeAdrenoAbiMetadata),0,0},
        {&action,0,sizeof(NativeAdrenoAbiMetadata),0,sizeof(NativeAdrenoAbiMetadata)},
        {&action,0,sizeof(NativeAdrenoAbiQueue),0,sizeof(NativeAdrenoAbiQueueReply)},
        {&action,0,sizeof(NativeAdrenoAbiQueue),0,0},
        {&action,0,sizeof(NativeAdrenoAbiBoCpuMap),0,sizeof(NativeAdrenoAbiBoCpuMapReply)},
        {&action,0,sizeof(NativeAdrenoAbiBoCpuUnmap),0,0},
        {&action,0,0,0,sizeof(NativeAdrenoAbiDeviceInfo)},
        {&action,0,sizeof(NativeAdrenoAbiBoImport),0,sizeof(NativeAdrenoAbiBoAllocReply)}};
    if(selector<1||selector>sizeof(table)/sizeof(table[0]))return kIOReturnUnsupported;
    return IOUserClient::externalMethod(selector,arguments,
        const_cast<IOExternalMethodDispatch *>(&table[selector-1]),target?target:this,reference);
}

// The metaclass initializer is staged out of __mod_init_func like every
// board IOKit class; running it during early kernel init panicked before the
// persistent log writer existed. It runs here, when the service is published.
extern "C" void cheeseburger_gpu_service_static_init(void)
    __asm__("__GLOBAL__sub_I_CheeseburgerGpuService.cpp");
extern "C" void cheeseburger_gpu_accelerator_static_init(void)
    __asm__("__GLOBAL__sub_I_CheeseburgerGpuAccelerator.cpp");
extern "C" IOService *cheeseburger_gpu_accelerator_allocate(void);
extern "C" kern_return_t OSKextLoadKextWithIdentifier(const char *);
extern "C" kmod_info_t g_kernel_kmod_info;
static IOService *gAccelerator = nullptr;
static int gAcceleratorResult = -99;

// Publish the registry-visible accelerator. IOGPUFamily is prelinked but not
// started on this board, so its IOAcceleratorES metaclass does not exist until
// the kext is loaded explicitly (the upstream GpuPowerProbe does the same).
// The owner class is registered through preModLoad/postModLoad after the parent
// exists, then attached to the platform like the original accelerator.
extern "C" int cheeseburger_gpu_accelerator_publish(void) {
    if (gAcceleratorResult != -99) return gAcceleratorResult;
    int result = -1;
    const OSSymbol *name=OSSymbol::withCString("IOAcceleratorES");
    const OSMetaClass *meta=name?OSMetaClass::getMetaClassWithName(name):nullptr;
    if(name)name->release();
    if(!meta) {
        kern_return_t kr=OSKextLoadKextWithIdentifier("com.apple.iokit.IOGPUFamily");
        IOLog("CB GPU IOGPU: load IOGPUFamily rc=%x\n",(unsigned)kr);
        name=OSSymbol::withCString("IOAcceleratorES");
        meta=name?OSMetaClass::getMetaClassWithName(name):nullptr;
        if(name)name->release();
    }
    IOLog("CB GPU IOGPU: IOAcceleratorES class=%d size=%u\n",meta?1:0,
        meta?meta->getClassSize():0);
    if(!meta) { result = -2; goto done; }
    if(meta->getClassSize()!=136) { result = -3; goto done; }
    {
        void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
        if(!handle) { result = -4; goto done; }
        cheeseburger_gpu_accelerator_static_init();
        int moderr=OSMetaClass::postModLoad(handle);
        IOLog("CB GPU accelerator: modload rc=%d\n",moderr);
        if(moderr) { result = -5; goto done; }
    }
    {
        IOService *accel=cheeseburger_gpu_accelerator_allocate();
        if(!accel) { result = -6; goto done; }
        if(!accel->attach(IOService::getPlatform())) {
            accel->release();
            result = -7;
            goto done;
        }
        accel->registerService();
        gAccelerator=accel;
        result = 0;
    }
done:
    gAcceleratorResult=result;
    IOLog("CB GPU accelerator: publish rc=%d\n",result);
    return result;
}

extern "C" int cheeseburger_gpu_service_publish(void) {
    if(gService)return 0;
    // Register NativeAdrenoGpuService and AGXDeviceUserClient the same way as
    // the accelerator: without preModLoad/postModLoad OSMetaClass reports an
    // internal error and a user-space class query can read a bad metaclass.
    {
        void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
        if(!handle)return -5;
        cheeseburger_gpu_service_static_init();
        int moderr=OSMetaClass::postModLoad(handle);
        IOLog("CB GPU service: modload rc=%d\n",moderr);
        if(moderr)return -6;
    }
    if(!gGateLock){gGateLock=IOLockAlloc();if(!gGateLock)return -4;}
    uint64_t start=0,bytes=0;
    if(cheeseburger_gpu_engine_window(&start,&bytes))return -1;
    IOService *parent=IOService::getResourceService();
    if(!parent)return -2;
    NativeAdrenoGpuService *service=new NativeAdrenoGpuService;
    if(!service||!service->init()||!service->attach(parent)||!service->start(parent)) {
        if(service)service->release();
        return -3;
    }
    service->setName("NativeAdrenoGpu");
    gService=service;
    native_adreno_service_ready=1;
    service->registerService();
    // Fallback if the probe did not run it earlier; idempotent.
    cheeseburger_gpu_accelerator_publish();
    return 0;
}
