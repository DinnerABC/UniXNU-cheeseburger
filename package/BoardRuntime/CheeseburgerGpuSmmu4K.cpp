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

extern "C" {
void flush_dcache(vm_offset_t,unsigned,boolean_t);
extern volatile uint64_t cb_smmu[24];
extern volatile uint64_t cb_cp[128];
int cheeseburger_gpu_mapping_begin(void);
int cheeseburger_gpu_mapping_end(int,int);
IOReturn armcpu_spi_attach(uint32_t,bool,void(*)(void*),void*,uint32_t*,uint64_t*) __attribute__((weak_import));
volatile uint64_t native_smmu_irq[6];
}

namespace {
struct W { IOMemoryMap *map=nullptr; volatile uint32_t *v=nullptr; };
static W gW;
static W gGic;
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
 IOReturn rc=armcpu_spi_attach(ContextIntid,false,contextFault,nullptr,&rb,&router);
 native_smmu_irq[1]=uint32_t(rc);native_smmu_irq[2]=rb;native_smmu_irq[3]=router;
 if(rc!=kIOReturnSuccess)return false;
 if((rb&7)!=3){
  gGic.v[(0x180+(ContextIntid/32)*4)/4]=1u<<(ContextIntid%32);
  __asm__ volatile("dsb sy":::"memory");return false;
 }
 gIrqInstalled=true;return true;
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
 // No fallback is permitted after the first hardware mutation: the kit may
 // have left a partially programmed or poisoned context/stream lease.
 swr(0x0,(srd(0x0)&~(1u|(3u<<14)|(1u<<13)))|0x1c36);
 swr(0x8004,0x70000000u);
 { MicroIo io; const auto micro=CheeseburgerGpu::resumeMicroMmu(io,3000);
   cb_smmu[16]=micro.before;cb_smmu[17]=micro.after;
   cb_smmu[19]=uint64_t(int64_t(micro.result));
   if(micro.result==CheeseburgerGpu::MicroResult::Timeout||
      micro.result==CheeseburgerGpu::MicroResult::Invalid)return -8; }
 gIo=(ArmSmmu4KIo){.cookie=nullptr,.register_bytes=0x10000,
  .read32=ioRead32,.read64=ioRead64,.write32=ioWrite32,.write64=ioWrite64,.barrier=ioBarrier};
 const ArmSmmu4KStream routes[2]={{0,0},{2,2}};
 // The board's unused cold slots read FAULT|0xff. The kit requires exactly
 // FAULT; normalize only an invalid slot with that known fault encoding.
 // Never replace a live or unfamiliar route.
 for(const auto &route:routes){
  unsigned smr=0x800+4*route.slot,s2cr=0xc00+4*route.slot;
  uint32_t match=srd(smr),value=srd(s2cr);
  if(match==0&&value==0x200ffu){swr(s2cr,0x20000u);value=srd(s2cr);}
  if(match!=0||value!=0x20000u){cb_smmu[20]=0xe100u+route.slot;return -8;}
 }
 ArmSmmu4KStreamResult preflight{};
 auto check=ArmSmmu4KCheckFaultedStreams(&gIo,routes,2,0,&preflight);
 if(check!=ArmSmmu4KSuccess){cb_smmu[20]=uint32_t(check);return -8;}
 if(__atomic_load_n(&gFaulted,__ATOMIC_ACQUIRE))return -8;
 if(gCtx->program(&gIo)!=kIOReturnSuccess){cb_smmu[20]=uint32_t(gCtx->programStatus());return -8;}
 // DTB arm,smmu-kgsl: gfx3d_user = SID 0, gfx3d_secure = SID 2, one-to-one SMR slots.
 if(__atomic_load_n(&gFaulted,__ATOMIC_ACQUIRE))return -8;
 if(gCtx->attach(routes,2)!=kIOReturnSuccess){cb_smmu[20]=uint32_t(gCtx->attachStatus());return -8;}
 if(NativeAdrenoBindSmmuVisibility(gTables,&gVis)!=kIOReturnSuccess)return -8;
 if(__atomic_load_n(&gFaulted,__ATOMIC_ACQUIRE))return -8;
 gActive=true;
 cb_smmu[0]=0;
 cb_smmu[20]=0x4b340001u;  // "smmu4k path active"
 IOLog("CB GPU SMMU4K: bank0 streams sid0/sid2 attached, tables=%u pages\n",(unsigned)TablePages);
 return 0;
}

extern "C" int cheeseburger_gpu_smmu4k_active(void){return gActive?1:0;}
extern "C" int cheeseburger_gpu_smmu4k_faulted(void){return __atomic_load_n(&gFaulted,__ATOMIC_ACQUIRE)!=0;}

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
