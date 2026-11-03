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
}

namespace {
struct W { IOMemoryMap *map=nullptr; volatile uint32_t *v=nullptr; };
static W gW;
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
// The aperture covers the kernel window (0x2000000..0x201ffff) and the client
// window at the ABI's va_start (0x4000000..0x4ffffff) in one lower region.
static const uint64_t ApertureBytes=0x8000000ULL;

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
 // Global configuration and the vendor hazard bits stay board-owned: the kit
 // documents that global setup belongs to the platform.
 swr(0x0,(srd(0x0)&~(1u|(3u<<14)|(1u<<13)))|0x1c36);
 swr(0x8004,0x70000000u);                       // CB0 ACTLR: OSH/ISH/NSH
 { MicroIo io; const auto micro=CheeseburgerGpu::resumeMicroMmu(io,3000);
   cb_smmu[16]=micro.before;cb_smmu[17]=micro.after;
   cb_smmu[19]=uint64_t(int64_t(micro.result));
   if(micro.result==CheeseburgerGpu::MicroResult::Timeout||
      micro.result==CheeseburgerGpu::MicroResult::Invalid)return -2; }
 gTables=new NativeAdrenoMemory;
 if(!gTables)return -3;
 // 32 contiguous 4K pages of tables, the size NativeAdrenoPrepareSmmuMemory
 // expects, in normal (cacheable) memory as the kit requires.
 if(gTables->allocate(32*4096,36,true,256*1024,false)!=kIOReturnSuccess)return -3;
 gCtx=new NativeAdrenoSmmuContext;
 if(!gCtx)return -4;
 // Lower region, 36-bit input/output, bank 0, ASID 0, SID 0/2 attached below.
 if(gCtx->prepare(gTables,0,0,ApertureBytes,8192,0,0,gPoll,36)!=kIOReturnSuccess)return -4;
 gIo=(ArmSmmu4KIo){.cookie=nullptr,.register_bytes=0x10000,
  .read32=ioRead32,.read64=ioRead64,.write32=ioWrite32,.write64=ioWrite64,.barrier=ioBarrier};
 if(gCtx->program(&gIo)!=kIOReturnSuccess)return -5;
 // DTB arm,smmu-kgsl: gfx3d_user = SID 0, gfx3d_secure = SID 2, one-to-one SMR slots.
 const ArmSmmu4KStream routes[2]={{0,0},{2,2}};
 if(gCtx->attach(routes,2)!=kIOReturnSuccess)return -6;
 if(NativeAdrenoBindSmmuVisibility(gTables,&gVis)!=kIOReturnSuccess)return -7;
 gActive=true;
 cb_smmu[0]=0;
 cb_smmu[20]=0x4b340001u;  // "smmu4k path active"
 IOLog("CB GPU SMMU4K: bank0 streams sid0/sid2 attached, tables=%u pages\n",32u);
 return 0;
}

extern "C" int cheeseburger_gpu_smmu4k_active(void){return gActive?1:0;}

extern "C" int cheeseburger_gpu_smmu4k_map(uint64_t iova,uint64_t pa,unsigned pages){
 if(!gActive||!pages||(iova&4095)||(pa&4095))return -1;
 for(unsigned i=0;i<pages;++i){
  const ArmSmmu4KRange r={iova+uint64_t(i)*4096,pa+uint64_t(i)*4096,4096,
    ArmSmmu4KRead|ArmSmmu4KWrite,0,2};
  ArmSmmu4KLiveRegion lower{gCtx->arenaHandle(),gCtx->stateHandle()};
  ArmSmmu4KLiveResult res{};
  if(ArmSmmu4KLiveUpdateContext(&gIo,0,gCtx->configHandle(),&lower,nullptr,0,
      &gVis,&r,1/*map*/,gPoll,&res)!=ArmSmmu4KSuccess)return -2;
 }
 return 0;
}

extern "C" int cheeseburger_gpu_smmu4k_unmap(uint64_t iova,unsigned pages){
 if(!gActive||!pages||(iova&4095))return -1;
 for(unsigned i=0;i<pages;++i){
  const ArmSmmu4KRange r={iova+uint64_t(i)*4096,0,4096,ArmSmmu4KRead,0,2};
  ArmSmmu4KLiveRegion lower{gCtx->arenaHandle(),gCtx->stateHandle()};
  ArmSmmu4KLiveResult res{};
  if(ArmSmmu4KLiveUpdateContext(&gIo,0,gCtx->configHandle(),&lower,nullptr,0,
      &gVis,&r,0/*unmap*/,gPoll,&res)!=ArmSmmu4KSuccess)return -2;
 }
 return 0;
}
