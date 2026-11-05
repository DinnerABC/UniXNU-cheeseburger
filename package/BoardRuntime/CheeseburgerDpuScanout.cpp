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
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <kern/task.h>
#include <string.h>
#include <pexpert/pexpert.h>
#include "NativeAdrenoSmmuContext.h"
#include "CheeseburgerSmmuId.h"

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

// Read-only capture of the firmware's display SMMU context, following sagit's
// Display::MMU::capture: no SMR/S2CR/SCTLR or pipe writes. It records the
// selected context, its TTBR/TCR/MAIR, and the translation of the address the
// MDP pipe is currently programmed with, so a later step can adopt the
// existing window instead of replacing the firmware's route.
extern "C" { volatile uint32_t native_adreno_dpu_ext[16]; }

namespace {
uint64_t readPhys64(uint64_t pa,unsigned index){
 IODeviceMemory *m=IODeviceMemory::withRange(pa&~0xfffull,4096);
 IOMemoryMap *map=m?m->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
 OSSafeReleaseNULL(m);
 uint64_t value=0;
 if(map){const volatile uint64_t *v=(const volatile uint64_t*)map->getVirtualAddress();
  if(v)value=v[index&511];map->release();}
 return value;
}
uint64_t maskWidth(unsigned bits){return bits>=64?~0ull:((1ull<<bits)-1);}
}

// Returns 0 when the firmware context and the pipe address's translation were
// both read. Writes nothing to hardware.
extern "C" int cheeseburger_dpu_capture(uint64_t address){
 auto &d=native_adreno_dpu;auto &x=native_adreno_dpu_ext;
 for(unsigned i=0;i<16;++i){d[i]=0;x[i]=0;}
 if(!gV){
  IODeviceMemory *m=IODeviceMemory::withRange(MmssBase,MmssBytes);
  gMap=m?m->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
  OSSafeReleaseNULL(m);
  gV=gMap?(volatile uint32_t*)gMap->getVirtualAddress():nullptr;
 }
 if(!gV){d[0]=0xfffffffdu;return -2;}
 const uint32_t id0=rd(0x20),id1=rd(0x24),id2=rd(0x28);
 if(id0==0xffffffffu||id1==0xffffffffu||id2==0xffffffffu){d[0]=0xfffffffcu;return -3;}
 d[2]=rd(0x0);d[3]=rd(0x800);d[4]=rd(0xc00);
 // The stream the MDP uses: the valid SMR whose S2CR names a context bank.
 unsigned bank=0xffffffffu;
 for(unsigned n=0;n<(id0&0xffu);++n){
  const uint32_t smr=rd(0x800+4*n),s2cr=rd(0xc00+4*n);
  if(smr==0xffffffffu||s2cr==0xffffffffu){d[0]=0xfffffffbu;return -4;}
  if((smr&(1u<<31))&&((smr^0u)&0xffffu)==0u&&!(s2cr&0x30000u)){bank=s2cr&0xffu;break;}
 }
 if(bank==0xffffffffu){d[1]=1;d[0]=0xfffffffau;return -5;}
 const unsigned stride=(id1>>31)?0x10000u:0x1000u;
 const unsigned pages=1u<<(((id1>>28)&7)+1);
 const uint64_t cb=(uint64_t(pages)+bank)*stride;
 if(cb+0x7f8>MmssBytes){d[1]=2;d[0]=0xfffffff9u;return -6;}
 d[5]=bank;d[6]=uint32_t(cb);
 d[7]=rd(uint32_t(cb));d[8]=rd(uint32_t(cb)+0x30);d[9]=rd(uint32_t(cb)+0x10);
 x[0]=rd(uint32_t(cb)+0x20);x[1]=rd(uint32_t(cb)+0x24);
 x[2]=rd(uint32_t(cb)+0x38);x[3]=rd(uint32_t(cb)+0x58);
 const unsigned inputBits=64-(d[8]&63),output=(d[9]&7);
 const unsigned outputBits=output==0?32:output==1?36:output==2?40:0;
 x[4]=(inputBits&0xffu)|(outputBits<<8);
 if(!x[0]||inputBits<32||inputBits>40||!outputBits){d[1]=3;d[0]=0xfffffff8u;return -7;}
 x[5]=uint32_t(address);x[6]=uint32_t(address>>32);
 // Walk the firmware's own tables for the pipe address.
 const unsigned startLevel=inputBits>39?0:1;
 uint64_t table=x[0]&0x0000fffffffff000ull,pa=0;
 unsigned result=1;
 for(unsigned level=startLevel;level<3&&result==1;++level){
  const unsigned shift=39-9*level;
  const uint64_t e=readPhys64(table,unsigned((address>>shift)&511));
  if(!(e&1)){result=2;break;}
  if((e&3)==3)table=e&0x0000fffffffff000ull;
  else if((e&3)==1){
   const uint64_t blockMask=level==1?0x3fffffffull:0x1fffffull;
   pa=(e&~blockMask&maskWidth(outputBits))|(address&blockMask);result=0;
  }else result=3;
 }
 if(result==1){
  const uint64_t e=readPhys64(table,unsigned((address>>12)&511));
  if((e&3)==3){pa=(e&0x0000fffffffff000ull)&maskWidth(outputBits);result=0;}
  else if((e&3)==1)result=4;
 }
 x[7]=pa;x[8]=uint32_t(pa>>32);x[9]=result;
 d[1]=3;d[0]=result==0?0:0xfffffff7u;
 return result==0?0:-8;
}

// sagit DisplayDMAHalt port for MSM8998 MDP5. Offsets are relative to MDSS
// (0xc900000): MDP at 0x1000, VIG0 at 0x5000, CTL0 at 0x2000, VBIF XIN halt
// at 0xb0200/0xb0204 and the MDP clock control at 0x12ac. The validation and
// halt/resume order are copied from drivers/display/QcomDisplay/DisplayDMAHalt.h.
namespace dpu_halt {
constexpr uint64_t Mdss=0xc900000;
constexpr uint32_t Span=0xb2000;
IOMemoryMap *map=nullptr;
volatile uint32_t *v=nullptr;
uint32_t rd(uint32_t off){uint32_t x=v[off/4];__asm__ volatile("dmb sy":::"memory");return x;}
bool write(uint32_t off,uint32_t value,uint32_t mask){
 __asm__ volatile("dsb sy":::"memory");v[off/4]=value;__asm__ volatile("dsb sy":::"memory");
 const uint32_t after=rd(off);
 return after!=0xffffffffu&&(after&mask)==(value&mask);
}
struct Halt {
 uint32_t source=0,xin=0,clockBit=0,clockBefore=0,haltBefore=0;
 uint32_t framebuffer=0,size=0,layer=0,top=0,stride=0;
 bool configured=false,touched=false,held=false;
 bool solePipeline(){
  if(v==nullptr||(rd(0)&0xffff0000u)!=0x30000000u)return false;
  static const uint32_t pipes[8]={0x4000,0x6000,0x8000,0xa000,0x24000,0x26000,0x28000,0x2a000};
  for(auto pipe:pipes){
   const uint32_t at=0x1000u+pipe;
   if(pipe!=source){if(rd(at))return false;continue;}
   if(rd(at)!=size||rd(at+0xc)!=size||rd(at+8)||rd(at+0x10)||
      rd(at+0x14)!=framebuffer||rd(at+0x24)!=stride)return false;
  }
  static const unsigned mixers[4]={0,1,2,5};
  for(unsigned ctl=0;ctl<5;++ctl){
   const uint32_t at=0x2000u+ctl*0x200u;
   for(auto lm:mixers){
    const uint32_t mix=rd(at+(lm==5?0x24u:4u*lm));
    if(!ctl&&!lm){if(mix!=layer)return false;}
    else if(mix&~(1u<<24))return false;
    if(rd(at+0x40+4*lm)||rd(at+0x70+4*lm))return false;
   }
   const uint32_t mode=rd(at+0x14);
   if(!ctl){if(mode!=top||(rd(at+0x18)&1u))return false;}
   else if((mode&&mode!=0x1f00u)||rd(at+0x18))return false;
  }
  const unsigned output=((top>>4)&7)-1;
  for(unsigned n=0;n<4;++n)if(n!=output&&rd(0x1000u+0x6a000u+n*0x800u))return false;
  return true;
 }
 bool prepare(uint32_t address,uint32_t width,uint32_t height){
  if(configured||v==nullptr||!address||!width||width>4096||!height||height>4096)return false;
  framebuffer=address;size=width|(height<<16);
  unsigned matches=0;
  static const uint32_t sources[2]={0x4000,0x24000};
  for(auto pipe:sources)if(rd(0x1000u+pipe)==size&&rd(0x1000u+pipe+0x14)==framebuffer){source=pipe;++matches;}
  if(matches!=1)return false;
  xin=source==0x4000?0:1;clockBit=source==0x4000?1u:1u<<8;
  stride=rd(0x1000u+source+0x24);layer=rd(0x2000);top=rd(0x2014);
  const uint32_t mask=7u<<(source==0x4000?0:18),output=((top>>4)&7)-1;
  if(!(layer&mask)||(layer&~(mask|(1u<<24)))||!(top&(1u<<17))||
     (top&~0x20070u)||output<1||output>2||!solePipeline())return false;
  clockBefore=rd(0x12ac);haltBefore=rd(0xb0200);
  if(clockBefore==0xffffffffu||haltBefore==0xffffffffu||(haltBefore&(1u<<xin)))return false;
  configured=true;return true;
 }
 bool halt(){
  if(!configured||touched||!solePipeline())return false;
  const uint32_t clock=rd(0x12ac);
  if(clock==0xffffffffu||(clock&clockBit)!=(clockBefore&clockBit)||rd(0xb0200)!=haltBefore)return false;
  touched=true;
  if(!write(0x12ac,clock|clockBit,clockBit)||!write(0xb0200,haltBefore|(1u<<xin),0xffffffffu))return false;
  for(unsigned n=0;n<2000;++n){
   const uint32_t ack=rd(0xb0204);
   if(ack==0xffffffffu)return false;
   if(ack&(1u<<xin)){held=true;return solePipeline();}
   IODelay(10);
  }
  return false;
 }
 bool resume(){
  if(!touched)return configured;
  if(!write(0xb0200,haltBefore,0xffffffffu))return false;
  for(unsigned n=0;n<2000;++n){
   if(!(rd(0xb0204)&(1u<<xin)))break;
   IODelay(10);
  }
  const uint32_t clock=rd(0x12ac);
  if(clock==0xffffffffu||!write(0x12ac,(clock&~clockBit)|(clockBefore&clockBit),clockBit))return false;
  held=false;touched=false;return solePipeline();
 }
};
bool open(){
 if(v)return true;
 IODeviceMemory *m=IODeviceMemory::withRange(Mdss,Span);
 map=m?m->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
 OSSafeReleaseNULL(m);
 v=map?(volatile uint32_t*)map->getVirtualAddress():nullptr;
 return v!=nullptr;
}
}

// Kernel-side equivalent of sagit's qcom,display-smmu-handoff: a disabled
// context bank, a published root table, a 32 MiB reserved window below 4 GiB
// and the framebuffer's physical address identity-mapped inside it. This round
// only builds and validates the contract while the display DMA is halted; the
// context is not written, so the visible picture cannot change.
namespace dpu_handoff {
struct Handoff {
 uint32_t version=1,bytes=0,flags=0,tableCount=0,cachePolicy=1,reserved=0;
 uint64_t first=0,end=0,framebuffer=0,deviceAddress=0,framebufferBytes=0;
 uint64_t table=0;
 uint32_t smmu=0x0cd00000,controller=0x0c901000,stream=0,inputBits=32,outputBits=36;
 uint32_t sctlr=0x1027,tcr=0,tcr2=0,ttbr0Lo=0,ttbr0Hi=0,mair0=0x44,bank=0,slot=0,smr=0,s2cr=0;
};
bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){
 return n&&m&&a<=UINT64_MAX-n&&b<=UINT64_MAX-m&&a<b+m&&b<a+n;
}
uint32_t validWhy(const Handoff &h){
 if(h.version!=1||h.bytes!=sizeof(Handoff)||h.flags!=31||!h.tableCount||h.tableCount>4||
    h.cachePolicy!=1||h.reserved)return 1;
 if(h.first<0x1000000||(h.first&4095)||(h.end&4095)||h.end<=h.first||h.end>0x100000000ull||
    h.end-h.first<0x2000000||h.end-h.first>0x20000000)return 2;
 if(!h.framebuffer||(h.framebuffer&4095)||!h.framebufferBytes||(h.framebufferBytes&4095))return 3;
 if(h.framebuffer>=(1ull<<h.outputBits)||h.framebufferBytes>(1ull<<h.outputBits)-h.framebuffer)return 4;
 if(!h.deviceAddress||(h.deviceAddress&4095)||h.deviceAddress>=0x100000000ull||
    h.framebufferBytes>0x100000000ull-h.deviceAddress)return 5;
 if(overlap(h.first,h.end-h.first,h.deviceAddress,h.framebufferBytes))return 6;
 if(!h.table||(h.table&4095)||h.table>=(1ull<<h.outputBits)||
    overlap(h.table&~0x3fffull,16384,h.framebuffer,h.framebufferBytes))return 7;
 if((h.sctlr&0x1027)!=0x1027||(h.sctlr&~0x1167u)||(h.mair0&255)!=0x44)return 8;
 const uint32_t tcr2=(7u<<15)|(1u<<4)|(h.outputBits==40?2u:h.outputBits==36?1u:0u);
 if(h.tcr2!=tcr2||(h.tcr&~0x3f00u)!=((1u<<23)|(64-h.inputBits))||
    ((h.tcr>>8)&3)>1||((h.tcr>>10)&3)>1||(((h.tcr>>12)&3)!=2&&((h.tcr>>12)&3)!=3))return 9;
 if((h.ttbr0Lo&4095)||h.ttbr0Lo!=uint32_t(h.table))return 10;
 return 0;
}
bool valid(const Handoff &h){return validWhy(h)==0;}
}

namespace dpu_handoff {
// The same 4 KiB table construction used by the GPU SMMU path; memory only.
unsigned tabCount=0;
struct T {uint64_t pa;volatile uint64_t *va;IOBufferMemoryDescriptor *buf;};
T tabs[32];
volatile uint64_t *root=nullptr;
unsigned startLevel=1;
uint32_t oas=36;
void clean(volatile uint64_t *t){flush_dcache((vm_offset_t)t,4096,FALSE);__asm__ volatile("dsb sy":::"memory");}
volatile uint64_t *find(uint64_t pa){
 for(unsigned i=0;i<tabCount;++i)if(tabs[i].pa==pa)return tabs[i].va;
 return nullptr;
}
volatile uint64_t *alloc(){
 if(tabCount>=32)return nullptr;
 IOBufferMemoryDescriptor *b=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
  kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,4096,0x00000000ffffffffull);
 if(!b)return nullptr;
 IOByteCount span=0;
 const uint64_t pa=b->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
 void *va=b->getBytesNoCopy();
 if(!pa||span<4096||!va||(pa>>oas)){b->release();return nullptr;}
 T &t=tabs[tabCount++];t={pa,(volatile uint64_t*)va,b};
 memset(va,0,4096);clean(t.va);return t.va;
}
bool mapPage(uint64_t iova,uint64_t pa){
 if(!root||(iova&4095)||(pa&4095)||pa>=(1ull<<36))return false;
 volatile uint64_t *t=root;
 for(unsigned level=startLevel;level<3;++level){
  const unsigned shift=39-9*level,idx=unsigned((iova>>shift)&511);
  uint64_t e=t[idx];
  if(!e){
   volatile uint64_t *n=alloc();
   if(!n)return false;
   t[idx]=tabs[tabCount-1].pa|3;clean(t);t=n;
  }else{
   if((e&0xfffull)!=3)return false;
   t=find(e&~0xfffull);
   if(!t)return false;
  }
 }
 const uint64_t attrs=3ULL|(1ULL<<6)|(2ULL<<8)|(1ULL<<10)|(1ULL<<11)|(3ULL<<53);
 t[(iova>>12)&511]=pa|attrs;clean(t);return true;
}
}
extern "C" int cheeseburger_dpu_halt_handoff(uint64_t pa,uint32_t stride,uint32_t width,uint32_t height){
 auto &d=native_adreno_dpu;auto &x=native_adreno_dpu_ext;
 static bool done=false;
 if(done)return -101;
 done=true;
 if(!pa||!stride||!width||!height||(pa&4095)||pa+uint64_t(stride)*height>0x100000000ull){
  d[0]=0xffffffffu;return -1;
 }
 if(!dpu_halt::open()){d[0]=0xfffffffdu;return -2;}
 dpu_halt::Halt h;
 if(!h.prepare(uint32_t(pa),width,height)){d[1]=1;d[0]=0xfffffffcu;return -3;}
 d[5]=h.source;d[6]=h.size;d[7]=h.stride;d[8]=h.xin;
 if(!h.halt()){
  d[1]=2;d[0]=0xfffffffbu;
  h.resume();
  return -4;
 }
 d[1]=3;
 // Build the identity tables and a handoff with sagit's attributes while the
 // display DMA is really halted. No SMMU register is written in this step.
 using namespace dpu_handoff;
 bool ok=true;
 const uint32_t id2=dpu_halt::rd(0x28);
 CheeseburgerGpu::IdWidths iw;
 const bool widths=ok&&CheeseburgerGpu::decodeIdWidths(id2,iw);
 unsigned ias=widths?(iw.ias>40?40:iw.ias):32;oas=widths?(iw.oas>40?40:iw.oas):36;
 if(ias<32||oas<32)ok=false;
 startLevel=ias>39?0:1;

 if(ok){tabCount=0;root=alloc();if(!root)ok=false;}
 const uint64_t bytes=uint64_t(stride)*height;
 if(ok)for(uint64_t off=0;off<bytes;off+=4096)
  if(!mapPage(pa+off,pa+off)){ok=false;break;}
 Handoff hd;
 if(ok){
  hd.bytes=sizeof(hd);hd.flags=31;hd.tableCount=1;hd.table=tabs[0].pa;
  hd.first=(pa&~0x1fffffull)+0x2000000ull;hd.end=hd.first+0x2000000ull;
  hd.framebuffer=pa;hd.deviceAddress=pa;
  hd.framebufferBytes=(bytes+4095)&~4095ull;
  hd.inputBits=ias;hd.outputBits=oas;
  hd.sctlr=0x1027;hd.tcr=(1u<<23)|(2u<<12)|(64-ias);
  hd.tcr2=(7u<<15)|(1u<<4)|(oas==40?2u:oas==36?1u:0u);
  hd.ttbr0Lo=uint32_t(tabs[0].pa);hd.ttbr0Hi=uint32_t(tabs[0].pa>>32);
  hd.bank=dpu_halt::rd(0xc00)&0xffu;hd.slot=0;
  hd.smr=dpu_halt::rd(0x800);hd.s2cr=dpu_halt::rd(0xc00);
  if(!hd.first||hd.end>0x100000000ull||hd.end-hd.first<0x2000000)ok=false;
  const uint32_t why=dpu_handoff::validWhy(hd);
  if(why)ok=false;
  x[5]=uint32_t(hd.first);x[6]=uint32_t(hd.end);x[7]=uint32_t(hd.framebuffer);
  x[8]=uint32_t(hd.deviceAddress);x[9]=uint32_t(hd.table);
  x[10]=hd.inputBits|(hd.outputBits<<8);x[11]=hd.sctlr;x[12]=hd.tcr;x[13]=hd.tcr2;
  x[14]=why;x[15]=(hd.flags&0xffu)|((hd.tableCount&0xffu)<<8)|((hd.bytes&0xffu)<<16)|((hd.cachePolicy&0xffu)<<24);
 }
 d[9]=ok?1u:0u;d[10]=uint32_t(tabCount);d[11]=uint32_t(bytes);
 const bool resumed=h.resume();
 d[12]=resumed?1u:0u;
 d[13]=dpu_halt::rd(0x0);d[14]=dpu_halt::rd(0x12ac);d[15]=dpu_halt::rd(0xb0200);
 d[1]=ok?4u:3u;d[0]=ok?0u:0xfffffffau;
 return ok?0:-10;
}
