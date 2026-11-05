// SPDX-License-Identifier: GPL-2.0-only
// Display-bank SMMU hand-off, ported from UniXNU-sheng's sheng-dpu-scanout
// model: the MDP fetches through a stage-1 context of the MMSS SMMU instead of
// a physical address. This first step only proves the takeover: it programs
// the MDP's non-secure context (SID 0), identity-maps the firmware's own GOP
// buffer, re-commits the pipe that already scans that buffer, and restores the
// firmware state. The pipe address never changes, so a wrong translation can
// stall the pipe but cannot point it at unrelated pixels; every step keeps the
// previous register image and restores it on failure.
//
// The context, tables and stream routing follow the vendored sheng kit
// (ArmSmmu4K) that already owns the GPU's kgsl SMMU. The device tree names the
// MMSS SMMU at 0xcd00000 with qcom,smmu_mdp_unsec_cb (SID 0) and
// qcom,smmu_mdp_sec_cb (SID 1).
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <pexpert/pexpert.h>
#include "NativeAdrenoSmmuContext.h"

extern "C" {
volatile uint32_t native_adreno_dpu[16];
// NativeLegacyDisplay.cpp: re-commit the pipe that scans the firmware buffer.
int native_legacy_mdp_flush_latch(uint32_t *addr,uint32_t *latchUs);
}

namespace {
constexpr uint64_t MmssBase=0xcd00000;
constexpr uint32_t MmssBytes=0x40000;
IOMemoryMap *gMap=nullptr;
volatile uint32_t *gV=nullptr;

uint32_t rd(uint32_t off){uint32_t v=gV[off/4];__asm__ volatile("dmb sy":::"memory");return v;}
void wr(uint32_t off,uint32_t v){__asm__ volatile("dsb sy":::"memory");gV[off/4]=v;__asm__ volatile("dsb sy":::"memory");}
int ioRead32(void *,size_t off,uint32_t *out){if(!out||off+4>MmssBytes)return 1;*out=rd((uint32_t)off);return 0;}
int ioRead64(void *,size_t off,uint64_t *out){
 if(!out||off+8>MmssBytes)return 1;
 *out=uint64_t(rd((uint32_t)off))|(uint64_t(rd((uint32_t)off+4))<<32);return 0;
}
int ioWrite32(void *,size_t off,uint32_t v){if(off+4>MmssBytes)return 1;wr((uint32_t)off,v);return 0;}
int ioWrite64(void *,size_t off,uint64_t v){
 if(off+8>MmssBytes)return 1;
 wr((uint32_t)off,uint32_t(v));wr((uint32_t)off+4,uint32_t(v>>32));return 0;
}
int ioBarrier(void *){__asm__ volatile("dsb sy":::"memory");return 0;}

NativeAdrenoMemory *gTables=nullptr;
NativeAdrenoSmmuContext *gCtx=nullptr;
ArmSmmu4KIo gIo{};
ArmSmmu4KLiveVisibility gVis{};
ArmSmmu4KRange gRuns[256];

// The kit's live update needs a range ledger; this identity mapping keeps it.
ArmSmmu4KRange gRanges[256];
size_t gRangeCount=0;
}

extern "C" int cheeseburger_dpu_smmu_identity(uint64_t pa,uint32_t bytes){
 auto &d=native_adreno_dpu;
 for(unsigned i=0;i<16;++i)d[i]=0;
 if(!pa||!bytes||(pa&4095)||pa+bytes>0x100000000ull){d[0]=0xffffffffu;return -1;}
 if(!gV){
  IODeviceMemory *m=IODeviceMemory::withRange(MmssBase,MmssBytes);
  gMap=m?m->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
  OSSafeReleaseNULL(m);
  gV=gMap?(volatile uint32_t*)gMap->getVirtualAddress():nullptr;
 }
 if(!gV){d[0]=0xfffffffdu;return -2;}
 const uint32_t sctlr0=rd(0x0),smr0=rd(0x800),s2cr0=rd(0xc00);
 const uint32_t id1=rd(0x24);
 d[2]=sctlr0;d[3]=smr0;d[4]=s2cr0;
 if(sctlr0==0xffffffffu||id1==0xffffffffu){d[0]=0xfffffffcu;return -3;}
 // Never take over a stream table the firmware already uses.
 for(unsigned i=0;i<8;++i)if(rd(0x800+4*i)&0x80000000u){d[1]=1;d[0]=0xfffffffbu;return -4;}
 const unsigned banks=id1&0xffu,secure=(id1>>16)&0xffu;
 if(!banks||secure>=banks){d[1]=2;d[0]=0xfffffffau;return -5;}
 const unsigned bank=secure;   // the kit refuses bank < S2
 const uint32_t s2crBank=uint32_t(bank);
 // A 2 MiB-aligned window covers the whole firmware buffer.
 const uint64_t base=pa&~0x1fffffull;
 const uint32_t span=uint32_t((pa-base)+bytes);
 const uint32_t spanRounded=(span+0x1fffffu)&~0x1fffffu;
 if(!gTables){
  gTables=new NativeAdrenoMemory;
  if(!gTables||gTables->allocate(128*4096,36,true,65536,false)!=kIOReturnSuccess){
   d[1]=3;d[0]=0xfffffff9u;native_adreno_dpu[0]=d[0];return -6;}
 }
 if(!gCtx)gCtx=new NativeAdrenoSmmuContext;
 if(!gCtx){d[1]=4;d[0]=0xfffffff8u;return -7;}
 const IOReturn prepared=gCtx->prepare(gTables,0,base,spanRounded,spanRounded/4096,
   bank,1,1024,36);
 if(prepared!=kIOReturnSuccess){d[1]=5;d[0]=0xfffffff7u;return -8;}
 gCtx->configHandle()->coherent_walk=0;
 gIo=(ArmSmmu4KIo){.cookie=nullptr,.register_bytes=MmssBytes,
   .read32=ioRead32,.read64=ioRead64,.write32=ioWrite32,.write64=ioWrite64,.barrier=ioBarrier};
 // Route SID 0 to this bank before the context is enabled: with the context
 // still off the MDP keeps fetching the physical buffer.
 wr(0xc00,s2crBank);
 if(rd(0xc00)!=s2crBank){d[1]=6;d[0]=0xfffffff6u;return -9;}
 wr(0x800,0x80000000u);
 if(rd(0x800)!=0x80000000u){d[1]=7;d[0]=0xfffffff5u;wr(0xc00,s2cr0);return -10;}
 // The same global-enable image the kgsl SMMU path proved on this board.
 wr(0x0,(rd(0x0)&~(1u|(3u<<14)|(1u<<13)))|0x1c36u);
 const bool programmed=gCtx->program(&gIo)==kIOReturnSuccess;
 if(!programmed){d[1]=8;d[0]=uint32_t(gCtx->programStatus())|0x80000000u;goto restore;}
 if(NativeAdrenoBindSmmuVisibility(gTables,&gVis)!=kIOReturnSuccess){
  d[1]=9;d[0]=0xfffffff4u;goto restore;}
 {
  // Identity-map the firmware buffer a chunk at a time. The pipe already
  // fetches this address; after the context enables it resolves to the same
  // pages, so the visible frame does not change.
  uint64_t off=0;bool mapped=true;
  while(off<bytes&&mapped){
   unsigned count=0;
   for(;off<bytes&&count<256;++count,off+=4096)
    gRuns[count]={pa+off,pa+off,4096,ArmSmmu4KRead|ArmSmmu4KWrite,0,2};
   ArmSmmu4KLiveRegion lower{gCtx->arenaHandle(),gCtx->stateHandle(),gRanges,gRangeCount};
   ArmSmmu4KLiveResult res{};
   const auto status=ArmSmmu4KLiveUpdateContextBatch(&gIo,bank,gCtx->configHandle(),&lower,nullptr,0,
     &gVis,gRuns,count,1u,1024,&res);
   if(status!=ArmSmmu4KSuccess){mapped=false;break;}
   for(unsigned i=0;i<count&&gRangeCount<256;++i)gRanges[gRangeCount++]=gRuns[i];
  }
  if(!mapped){d[1]=10;d[0]=0xfffffff3u;goto restore;}
 }
 {
  uint32_t latched=0,latchUs=0;
  const int mrc=native_legacy_mdp_flush_latch(&latched,&latchUs);
  d[7]=latched;d[8]=latchUs;d[11]=mrc==0?1u:0u;
  IOSleep(2000);
  if(gCtx->observe()==kIOReturnSuccess){const auto *p=gCtx->programResult();d[9]=p?p->fault_status:0;}
 }
restore:
 {
  // Turn our context off before restoring the global image, then put the
  // firmware's stream table back exactly as it was.
  const auto *p=gCtx->programResult();
  if(p&&p->readback_valid&&p->context_offset&&p->context_offset<MmssBytes)
   wr(uint32_t(p->context_offset),0u);
  wr(0x0,sctlr0);
  wr(0x800,smr0);
  wr(0xc00,s2cr0);
  __asm__ volatile("dsb sy":::"memory");
  uint32_t latched=0,latchUs=0;
  const int mrc=native_legacy_mdp_flush_latch(&latched,&latchUs);
  d[5]=latched;d[6]=latchUs;
  d[12]=mrc==0?1u:0u;
  d[13]=rd(0x0);d[14]=rd(0x800);d[15]=rd(0xc00);
 }
 if(d[0]==0)d[0]=0;
 return int(d[0]);
}
