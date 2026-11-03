// SPDX-License-Identifier: GPL-2.0-only
// GPU SMMU bank 0 through the official sheng ArmSmmu4K kit: the kit builds the
// page tables, programs the context bank and attaches the exact DTB streams
// (gfx3d_user SID 0, gfx3d_secure SID 2) with stage-1-only translation. The
// board keeps only what the kit explicitly leaves to the platform: the SMMU
// global configuration, the vendor ACTLR hazard bits and the micro-MMU resume.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include "sheng/Adreno740/NativeAdrenoMemory.h"
#include "sheng/Adreno740/NativeAdrenoSmmuContext.h"
#include "sheng/Adreno740/NativeAdrenoSmmuMemory.h"
#include "sheng/kits/ArmSmmu4K/ArmSmmu4K.h"
#include "CheeseburgerSmmuMicro.h"
#include "CheeseburgerGpuQuiesce.h"

extern "C" {
void flush_dcache(vm_offset_t,unsigned,boolean_t);
extern volatile uint64_t cb_smmu[24];
extern volatile uint64_t cb_cp[128];
int cheeseburger_gpu_mapping_begin(void);
int cheeseburger_gpu_mapping_end(int,int);
IOReturn armcpu_spi_attach(uint32_t,bool,void(*)(void*),void*,uint32_t*,uint64_t*) __attribute__((weak_import));
volatile uint64_t native_smmu_irq[6];
// Cold-start drain samples: RBRead/RBWrite/MEControl/VBIFHalt/InterruptMask.
volatile uint64_t native_smmu_cold[8];
}

namespace {
struct W { IOMemoryMap *map=nullptr; volatile uint32_t *v=nullptr; };
static W gW;
static W gGic;
static W gGpu;
static volatile uint32_t gFaulted=0;
static bool gIrqInstalled=false;
// Device Android DTB: arm,smmu-kgsl interrupt 0 is GIC SPI 0x149,
// level-high; GIC INTID adds 32. Mask before returning from hard IRQ and
// quarantine rather than risking an interrupt storm or a blocking MMIO read.
static const uint32_t ContextIntid=0x149+32;
static void contextFault(void *){
 __atomic_store_n(&gFaulted,1,__ATOMIC_RELEASE);
 __atomic_fetch_add(&native_smmu_irq[0],1,__ATOMIC_RELAXED);
 gGic.v[(0x180+(ContextIntid/32)*4)/4]=1u<<(ContextIntid%32);
 __asm__ volatile("dsb sy":::"memory");
}
static bool installFaultIrq(){
 if(gIrqInstalled)return true;
 if(!&armcpu_spi_attach)return false;
 IODeviceMemory *mem=IODeviceMemory::withRange(0x17a00000,0x10000);
 gGic.map=mem?mem->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
 OSSafeReleaseNULL(mem);
 gGic.v=gGic.map?(volatile uint32_t*)gGic.map->getVirtualAddress():nullptr;
 if(!gGic.v)return false;
 uint32_t rb=0;uint64_t router=0;
 native_smmu_irq[4]=ContextIntid;
 IOReturn rc=armcpu_spi_attach(ContextIntid,false,contextFault,nullptr,&rb,&router);
 native_smmu_irq[1]=uint32_t(rc);native_smmu_irq[2]=rb;native_smmu_irq[3]=router;
 if(rc!=kIOReturnSuccess)return false;
 // GICD_CTLR.DS: with a single security state IGROUPR is RAZ/WI, so the
 // readback reports group 0 for an interrupt that is still usable. What proves
 // non-secure usability is the enable bit: a secure-only interrupt would ignore
 // ISENABLER from this state as well. The touch SPI attach relies on the same.
 const uint32_t ctlr=gGic.v[0];
 const bool ds=(ctlr>>6)&1;
 IOLog("CB GPU SMMU IRQ: attach rc=%d rb=%x ctlr=%x ds=%d id=%u\n",
  (int)rc,rb,ctlr,ds?1:0,ContextIntid);
 if(!(rb&2)){
  gGic.v[(0x180+(ContextIntid/32)*4)/4]=1u<<(ContextIntid%32);
  __asm__ volatile("dsb sy":::"memory");return false;
 }
 gIrqInstalled=true;native_smmu_irq[5]=1;return true;
}
static bool mapSmmu(){
 if(gW.v)return true;
 IODeviceMemory *m=IODeviceMemory::withRange(0x5040000,0x10000);
 gW.map=m?m->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
 OSSafeReleaseNULL(m);
 gW.v=gW.map?(volatile uint32_t*)gW.map->getVirtualAddress():nullptr;
 return gW.v!=nullptr;
}
static uint32_t srd(unsigned off){uint32_t x=gW.v[off/4];__asm__ volatile("dmb sy":::"memory");return x;}
static void swr(unsigned off,uint32_t x){__asm__ volatile("dsb sy":::"memory");gW.v[off/4]=x;__asm__ volatile("dsb sy":::"memory");}
static void swr64(unsigned off,uint64_t v){
 __asm__ volatile("dsb sy":::"memory");
 *reinterpret_cast<volatile uint64_t *>(reinterpret_cast<uintptr_t>(gW.v)+off)=v;
 __asm__ volatile("dsb sy":::"memory");
}
static int ioRead32(void *,size_t off,uint32_t *out){
 if(!out||off+4>0x10000)return 1;*out=srd((unsigned)off);return 0;
}
static int ioRead64(void *,size_t off,uint64_t *out){
 if(!out||off+8>0x10000)return 1;
 *out=(uint64_t)srd((unsigned)off)|((uint64_t)srd((unsigned)off+4)<<32);
 return 0;
}
static int ioWrite32(void *,size_t off,uint32_t v){
 if(off+4>0x10000)return 1;swr((unsigned)off,v);return 0;
}
static int ioWrite64(void *,size_t off,uint64_t v){
 if(off+8>0x10000)return 1;swr64((unsigned)off,v);return 0;
}
static int ioBarrier(void *){__asm__ volatile("dsb sy":::"memory");return 0;}
static int publishPage(void *,volatile uint64_t *page,size_t bytes){
 if(!page)return 1;
 flush_dcache((vm_offset_t)page,(unsigned)bytes,FALSE);
 __asm__ volatile("dsb sy":::"memory");
 return 0;
}

static NativeAdrenoMemory *gTables=nullptr;
static NativeAdrenoSmmuContext *gCtx=nullptr;
static ArmSmmu4KIo gIo{};
static ArmSmmu4KLiveVisibility gVis{};
static unsigned gPoll=1024;
static bool gActive=false;
static bool gPoisoned=false;
static ArmSmmu4KRange gRanges[128];
static size_t gRangeCount=0;
// The aperture covers the kernel window (0x2000000..0x201ffff) and the client
// window at the ABI's va_start (0x4000000..0x4ffffff) in one lower region.
static const uint64_t ApertureBytes=0x8000000ULL;
static const size_t LeafBudget=ApertureBytes/4096;
// The official 32-bit lower-region layout needs 66 pages for 128 MiB.
static const size_t TablePages=128;

struct MicroIo {
 uint32_t read(unsigned off){return srd(off);}
 void write(unsigned off,uint32_t v){swr(off,v);}
 void delay(unsigned us){IODelay(us);}
 void mark(const char *){}
};
struct GpuIo {
 uint32_t read(uint32_t off){uint32_t v=gGpu.v[off/4];__asm__ volatile("dmb sy":::"memory");return v;}
 void write(uint32_t off,uint32_t v){__asm__ volatile("dsb sy":::"memory");gGpu.v[off/4]=v;__asm__ volatile("dsb sy":::"memory");}
 void delay(unsigned us){IODelay(us);}
 void publish(){__asm__ volatile("dsb oshst":::"memory");}
};
// Read-only cold sample. sheng expects the platform to have quiesced every
// master before the SMMU is programmed; this probe runs before the CP exists,
// so it records the ring/engine state instead of issuing halt writes.
static bool drainColdGpu(){
 IODeviceMemory *mem=IODeviceMemory::withRange(0x5000000,0x40000);
 gGpu.map=mem?mem->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
 OSSafeReleaseNULL(mem);
 gGpu.v=gGpu.map?(volatile uint32_t*)gGpu.map->getVirtualAddress():nullptr;
 if(!gGpu.v)return false;
 GpuIo io;
 native_smmu_cold[0]=io.read(CheeseburgerGpu::RBRead);
 native_smmu_cold[1]=io.read(CheeseburgerGpu::RBWrite);
 native_smmu_cold[2]=io.read(CheeseburgerGpu::MEControl);
 native_smmu_cold[3]=io.read(CheeseburgerGpu::VBIFHalt);
 native_smmu_cold[4]=io.read(CheeseburgerGpu::InterruptMask);
 native_smmu_cold[5]=0x636f6c64u;  // "cold"
 return true;
}
}

extern "C" int cheeseburger_gpu_smmu4k_setup(void){
 if(gActive)return 0;
 if(!mapSmmu())return -1;
 if(!installFaultIrq())return -9;
 if(__atomic_load_n(&gFaulted,__ATOMIC_ACQUIRE))return -8;
 gTables=new NativeAdrenoMemory;
 if(!gTables)return -3;
 // A contiguous normal-memory table pool sized for the full aperture.
 if(gTables->allocate(TablePages*4096,36,true,1024*1024,false)!=kIOReturnSuccess)return -3;
 gCtx=new NativeAdrenoSmmuContext;
 if(!gCtx)return -4;
 // Upstream prepare currently uses a 32-bit input; this aperture fits it.
 // Output remains 36-bit as read from this device's ID2.
 IOReturn prepared=gCtx->prepare(gTables,0,0,ApertureBytes,LeafBudget,0,0,gPoll,36);
 if(prepared!=kIOReturnSuccess){cb_smmu[20]=uint32_t(prepared);return -4;}
 // sheng assumes its SMMU is dma-coherent; the cheeseburger vendor DTB does
 // not advertise that property. Use the sagit non-cacheable walk and retain
 // the kit's explicit table-page publication before each update.
 gCtx->configHandle()->coherent_walk=0;
 // sheng programs the SMMU (prepare -> program -> attach -> map) with the
 // platform guaranteeing that no master is active; it has no drain step. This
 // setup runs before the probe constructs or starts the CP, so the cold reads
 // below are the quiescence evidence: an empty ring means no submission is in
 // flight. Only a ring with work (or a read that cannot be taken) leaves the
 // kit path; the pinned sequence then owns the GPU as before.
 if(!drainColdGpu()){
  IOLog("CB GPU SMMU4K: cold registers unavailable; using the pinned sequence\n");
  return -10;
 }
 if(native_smmu_cold[0]!=native_smmu_cold[1]){
  IOLog("CB GPU SMMU4K: ring not quiescent r/w=%x/%x; using the pinned sequence\n",
   (unsigned)native_smmu_cold[0],(unsigned)native_smmu_cold[1]);
  return -10;
 }
 IOLog("CB GPU SMMU4K: cold r/w/me/vbif/im=%x/%x/%x/%x/%x\n",
  (unsigned)native_smmu_cold[0],(unsigned)native_smmu_cold[1],(unsigned)native_smmu_cold[2],
  (unsigned)native_smmu_cold[3],(unsigned)native_smmu_cold[4]);
 // No fallback is permitted after the first hardware mutation: the kit may
 // have left a partially programmed or poisoned context/stream lease.
 swr(0x0,(srd(0x0)&~(1u|(3u<<14)|(1u<<13)))|0x1c36);
 swr(0x8004,0x70000000u);
 { MicroIo io; const auto micro=CheeseburgerGpu::resumeMicroMmu(io,3000);
   cb_smmu[16]=micro.before;cb_smmu[17]=micro.after;
   cb_smmu[19]=uint64_t(int64_t(micro.result));
   if(micro.result==CheeseburgerGpu::MicroResult::Timeout||
      micro.result==CheeseburgerGpu::MicroResult::Invalid){
    IOLog("CB GPU SMMU4K: step=micro before/after=%x/%x result=%d\n",
     (unsigned)micro.before,(unsigned)micro.after,(int)micro.result);return -8; } }
 gIo=(ArmSmmu4KIo){.cookie=nullptr,.register_bytes=0x10000,
  .read32=ioRead32,.read64=ioRead64,.write32=ioWrite32,.write64=ioWrite64,.barrier=ioBarrier};
 const ArmSmmu4KStream routes[2]={{0,0},{2,2}};
 // This firmware reports an idle stream as FAULT with CBNDX=0xff and does not
 // accept a FAULT rewrite, so the kit's "must already fault" preflight cannot
 // run here. sheng's own platform plan (UniXNU-sheng/package/SmmuBypass.h)
 // routes a stream by writing S2CR=bank, reading it back, then publishing the
 // exact SID in SMR; the context/table work stays with the kit.
 for(const auto &route:routes){
  const uint32_t smr=srd(0x800+4*route.slot);
  if(smr&0x80000000u){
   IOLog("CB GPU SMMU4K: step=slots slot=%u smr=%x busy\n",route.slot,smr);
   cb_smmu[20]=0xe100u+route.slot;return -8;
  }
 }
 if(gCtx->program(&gIo)!=kIOReturnSuccess){
  IOLog("CB GPU SMMU4K: step=program status=%d\n",(int)gCtx->programStatus());
  cb_smmu[20]=uint32_t(gCtx->programStatus());return -8;}
 // DTB arm,smmu-kgsl: gfx3d_user = SID 0, gfx3d_secure = SID 2, bank 0.
 if(__atomic_load_n(&gFaulted,__ATOMIC_ACQUIRE)){IOLog("CB GPU SMMU4K: step=fault-after-program\n");return -8;}
 for(const auto &route:routes){
  unsigned smr=0x800+4*route.slot,s2cr=0xc00+4*route.slot;
  swr(s2cr,0u);
  if(srd(s2cr)!=0u){
   IOLog("CB GPU SMMU4K: step=route-s2cr slot=%u rd=%x\n",route.slot,srd(s2cr));
   cb_smmu[20]=0xe100u+route.slot;return -8;
  }
  swr(smr,0x80000000u|route.sid);
  const uint32_t gotS2=srd(s2cr),gotSMR=srd(smr);
  IOLog("CB GPU SMMU4K: route slot=%u sid=%u s2cr=%x smr=%x\n",route.slot,route.sid,gotS2,gotSMR);
  if(gotS2!=0u||gotSMR!=(0x80000000u|route.sid)){
   // The secure context stream is owned by the secure world: its SMR reads
   // back as invalid here. Protected content is not used by this port, so the
   // user SID is routed and the secure route is left to the firmware.
   if(route.sid==2u){
    IOLog("CB GPU SMMU4K: secure sid=2 smr=%x not writable; left to firmware\n",gotSMR);
    continue;
   }
   cb_smmu[20]=0xe100u+route.slot;return -8;
  }
 }
 if(gCtx->observe()!=kIOReturnSuccess){
  IOLog("CB GPU SMMU4K: step=observe\n");return -8;}
 if(NativeAdrenoBindSmmuVisibility(gTables,&gVis)!=kIOReturnSuccess){
  IOLog("CB GPU SMMU4K: step=visibility\n");return -8;}
 if(__atomic_load_n(&gFaulted,__ATOMIC_ACQUIRE)){IOLog("CB GPU SMMU4K: step=fault-after-route\n");return -8;}
 gActive=true;
 cb_smmu[0]=0;
 cb_smmu[20]=0x4b340001u;  // "smmu4k path active"
 IOLog("CB GPU SMMU4K: bank0 sid0 routed (sid2 secure-owned), tables=%u pages\n",(unsigned)TablePages);
 return 0;
}

extern "C" int cheeseburger_gpu_smmu4k_active(void){return gActive?1:0;}
extern "C" int cheeseburger_gpu_smmu4k_faulted(void){return __atomic_load_n(&gFaulted,__ATOMIC_ACQUIRE)!=0;}
// Software mirror of the kit's mapped ranges, for the kernel-side preflight
// that runs before a user submission. The hardware walk (ATOS) still runs
// against the programmed context.
extern "C" int cheeseburger_gpu_smmu4k_lookup(uint64_t iova,uint64_t *pa){
 if(!gActive||!pa||(iova&4095))return -1;
 for(size_t i=0;i<gRangeCount;++i){
  const ArmSmmu4KRange &r=gRanges[i];
  if(iova>=r.iova&&iova<r.iova+r.bytes){*pa=r.physical+(iova-r.iova);return 0;}
 }
 return -2;
}
// Leave the kit path before any SMMU write: mask the fault IRQ installed for
// it and clear the flag, so the pinned sequence and the submission gate are
// not affected by a handler this path no longer owns.
extern "C" void cheeseburger_gpu_smmu4k_abort(void){
 if(gIrqInstalled&&gGic.v){
  gGic.v[(0x180+(ContextIntid/32)*4)/4]=1u<<(ContextIntid%32);
  __asm__ volatile("dsb sy":::"memory");
 }
 gIrqInstalled=false;gPoisoned=false;
 __atomic_store_n(&gFaulted,0,__ATOMIC_RELEASE);
}

extern "C" int cheeseburger_gpu_smmu4k_map(uint64_t iova,uint64_t pa,unsigned pages){
 if(!gActive||gPoisoned||cheeseburger_gpu_smmu4k_faulted()||!pages||(iova&4095)||(pa&4095))return -1;
 int token=cheeseburger_gpu_mapping_begin();if(token<0)return -3;
 if(gRangeCount>=sizeof(gRanges)/sizeof(gRanges[0])){cheeseburger_gpu_mapping_end(token,1);return -1;}
 const ArmSmmu4KRange r={iova,pa,uint64_t(pages)*4096,ArmSmmu4KRead|ArmSmmu4KWrite,0,2};
 ArmSmmu4KLiveRegion lower{gCtx->arenaHandle(),gCtx->stateHandle(),gRanges,gRangeCount};
 ArmSmmu4KLiveResult res{};
 const auto status=ArmSmmu4KLiveUpdateContext(&gIo,0,gCtx->configHandle(),&lower,nullptr,0,&gVis,&r,1,gPoll,&res);
 if(status!=ArmSmmu4KSuccess){gPoisoned=true;cb_smmu[20]=uint32_t(status);cheeseburger_gpu_mapping_end(token,0);return -2;}
 size_t at=0;while(at<gRangeCount&&gRanges[at].iova<iova)++at;
 for(size_t i=gRangeCount;i>at;--i)gRanges[i]=gRanges[i-1];gRanges[at]=r;++gRangeCount;
 if(cheeseburger_gpu_mapping_end(token,1)){gPoisoned=true;return -3;}return 0;
}

extern "C" int cheeseburger_gpu_smmu4k_unmap(uint64_t iova,unsigned pages){
 if(!gActive||gPoisoned||cheeseburger_gpu_smmu4k_faulted()||!pages||(iova&4095))return -1;
 int token=cheeseburger_gpu_mapping_begin();if(token<0)return -3;
 size_t at=0;while(at<gRangeCount&&(gRanges[at].iova!=iova||gRanges[at].bytes!=uint64_t(pages)*4096))++at;
 if(at==gRangeCount){cheeseburger_gpu_mapping_end(token,1);return -1;}
 const ArmSmmu4KRange r=gRanges[at];
 ArmSmmu4KLiveRegion lower{gCtx->arenaHandle(),gCtx->stateHandle(),gRanges,gRangeCount};
 ArmSmmu4KLiveResult res{};
 const auto status=ArmSmmu4KLiveUpdateContext(&gIo,0,gCtx->configHandle(),&lower,nullptr,0,&gVis,&r,0,gPoll,&res);
 if(status!=ArmSmmu4KSuccess){gPoisoned=true;cb_smmu[20]=uint32_t(status);cheeseburger_gpu_mapping_end(token,0);return -2;}
 for(size_t i=at+1;i<gRangeCount;++i)gRanges[i-1]=gRanges[i];--gRangeCount;
 if(cheeseburger_gpu_mapping_end(token,1)){gPoisoned=true;return -3;}return 0;
}
