// SPDX-License-Identifier: GPL-2.0-only
// Display-bank SMMU hand-off, the sheng sheng-dpu-scanout model ported to the
// MSM8998 MMSS SMMU (arm,smmu-mmss@cd00000): the MDP fetches compositor
// surfaces through a stage-1 context instead of physical addresses. The
// context programming follows the sequence this board already proved on the
// GPU's kgsl SMMU (global image, bank-style CB, TCR/TCR2 SEP 7, TTBR0, MAIR,
// ACTLR, TLB sync). The firmware's own SID 0 table is left untouched: a free
// context bank is programmed and only S2CR0 is repointed at it, so a failure
// restores the bootloader's route exactly.
//
// The firmware buffer is identity-mapped in the same context, so enabling the
// route does not change the picture. Same-size BGRA frames are then mapped
// into two fixed 16 MiB IOVA windows and the pipe is flipped to the window;
// a scaling frame or any failure puts the pipe back on the firmware buffer and
// the display driver's CPU copy runs as before.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/task.h>
#include <string.h>
#include <pexpert/pexpert.h>
#include "CheeseburgerSmmuId.h"

extern "C" {
volatile uint32_t native_adreno_dpu[16];
int native_legacy_mdp_flush_latch(uint32_t *addr,uint32_t *latchUs);
int native_legacy_mdp_commit(uint32_t addr,uint32_t stride,uint32_t *latchUs);
void flush_dcache(vm_offset_t,unsigned,boolean_t);
}

namespace {
constexpr uint64_t MmssBase=0xcd00000;
constexpr uint32_t MmssBytes=0x40000;
constexpr unsigned PoolPages=32;
constexpr uint32_t WindowSlot=0x1000000;   // 16 MiB per IOVA window
IOMemoryMap *gMap=nullptr;
volatile uint32_t *gV=nullptr;

uint32_t rd(uint32_t off){uint32_t v=gV[off/4];__asm__ volatile("dmb sy":::"memory");return v;}
void wr(uint32_t off,uint32_t v){__asm__ volatile("dsb sy":::"memory");gV[off/4]=v;__asm__ volatile("dsb sy":::"memory");}
void cleanTable(volatile uint64_t *t){flush_dcache((vm_offset_t)t,4096,FALSE);__asm__ volatile("dsb sy":::"memory");}

IOBufferMemoryDescriptor *gPool=nullptr;
volatile uint64_t *gPoolVA=nullptr;
uint64_t gPoolPA=0;
unsigned gTables=0;
struct Tab {uint64_t pa;volatile uint64_t *va;};
Tab gTab[PoolPages];
volatile uint64_t *gRoot=nullptr;
unsigned gStartLevel=1;
uint32_t gIas=32,gOas=36;

bool gActive=false;
uint32_t gBank=0,gCtx=0,gSctlr0=0,gSmr0=0,gS2cr0=0;
uint32_t gGopPa=0,gGopStride=0,gGopW=0,gGopH=0;
uint64_t gWin[2]={0,0};
int gCurrent=-1;   // -1: pipe scans the firmware buffer

volatile uint64_t *findTable(uint64_t pa){
 for(unsigned i=0;i<gTables;++i)if(gTab[i].pa==pa)return gTab[i].va;
 return nullptr;
}
volatile uint64_t *newTable(){
 if(gTables>=PoolPages)return nullptr;
 const unsigned i=gTables++;
 gTab[i]={gPoolPA+uint64_t(i)*4096,gPoolVA+uint64_t(i)*512};
 memset((void*)gTab[i].va,0,4096);cleanTable(gTab[i].va);
 return gTab[i].va;
}
bool mapPage(uint64_t iova,uint64_t pa){
 if(!gRoot||(iova&4095)||(pa&4095)||pa>=(1ull<<36))return false;
 volatile uint64_t *t=gRoot;
 for(unsigned level=gStartLevel;level<3;++level){
  const unsigned shift=39-9*level,idx=unsigned((iova>>shift)&511);
  uint64_t e=t[idx];
  if(!e){
   volatile uint64_t *n=newTable();
   if(!n)return false;
   t[idx]=gTab[gTables-1].pa|3;cleanTable(t);t=n;
  }else{
   if((e&0xfffull)!=3)return false;
   t=findTable(e&~0xfffull);
   if(!t)return false;
  }
 }
 const uint64_t attrs=3ULL|(1ULL<<6)|(2ULL<<8)|(1ULL<<10)|(1ULL<<11)|(3ULL<<53);
 t[(iova>>12)&511]=pa|attrs;cleanTable(t);return true;
}
bool tlbSync(bool allAsids){
 if(!gCtx||uint64_t(gCtx)+0x7f8>MmssBytes)return false;
 wr(gCtx+(allAsids?0x618:0x610),0);
 wr(gCtx+0x7f0,0xffffffffu);
 for(unsigned i=0;i<100000;++i){
  const uint32_t st=rd(gCtx+0x7f4);
  if(st==0xffffffffu)return false;
  if(!(st&1))return true;
  IODelay(1);
 }
 return false;
}
void restoreRegisters(){
 if(!gV)return;
 if(gCtx&&uint64_t(gCtx)+0x7f8<=MmssBytes)wr(gCtx,0);
 wr(0x0,gSctlr0);wr(0x800,gSmr0);wr(0xc00,gS2cr0);
 __asm__ volatile("dsb sy":::"memory");
}
}

extern "C" int cheeseburger_dpu_setup(uint32_t gopPa,uint32_t gopStride,uint32_t width,uint32_t height){
 auto &d=native_adreno_dpu;
 for(unsigned i=0;i<16;++i)d[i]=0;
 if(!gopPa||!gopStride||!width||!height||(gopStride&7)||(gopPa&4095)){
  d[0]=0xffffffffu;return -1;
 }
 if(!gV){
  IODeviceMemory *m=IODeviceMemory::withRange(MmssBase,MmssBytes);
  gMap=m?m->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
  OSSafeReleaseNULL(m);
  gV=gMap?(volatile uint32_t*)gMap->getVirtualAddress():nullptr;
 }
 if(!gV){d[0]=0xfffffffdu;return -2;}
 // Default: read-only probe, the device-verified behavior. The takeover that
 // writes the SMMU is an explicit opt-in until it is proven on hardware.
 { unsigned scanout=0;PE_parse_boot_argn("cheeseburger-dpu-scanout",&scanout,sizeof scanout);
  if(scanout!=1){d[2]=rd(0x0);d[3]=rd(0x800);d[4]=rd(0xc00);d[1]=0x80;d[0]=0xffffff00u;return -100;} }
 const uint32_t id0=rd(0x20),id1=rd(0x24),id2=rd(0x28);
 if(id0==0xffffffffu||id1==0xffffffffu||id2==0xffffffffu){d[0]=0xfffffffcu;return -3;}
 CheeseburgerGpu::IdWidths iw;
 if(!CheeseburgerGpu::decodeIdWidths(id2,iw)){d[0]=0xfffffffbu;return -4;}
 gIas=iw.ias>40?40:iw.ias;gOas=iw.oas>40?40:iw.oas;
 if(gIas<32||gOas<32){d[0]=0xfffffffau;return -5;}
 gStartLevel=gIas>39?0:1;
 const unsigned stride=(id1>>31)?0x10000u:0x1000u;
 const unsigned pages=1u<<(((id1>>28)&7)+1);
 const unsigned banks=id1&0xffu;
 if(!banks||banks<2){d[0]=0xfffffff9u;return -6;}
 gSctlr0=rd(0x0);gSmr0=rd(0x800);gS2cr0=rd(0xc00);
 d[2]=gSctlr0;d[3]=gSmr0;d[4]=gS2cr0;
 if(gSctlr0&1u){d[1]=1;d[0]=0xfffffff8u;return -7;}
 unsigned bank=0,ctx=0;
 for(unsigned b=1;b<banks;++b){
  const uint32_t off=(pages+b)*stride;
  if(uint64_t(off)+0x7f8>MmssBytes)break;
  if(!(rd(off)&1u)){bank=b;ctx=off;break;}
 }
 if(!bank||!ctx){d[1]=2;d[0]=0xfffffff7u;return -8;}
 gBank=bank;gCtx=ctx;d[5]=bank;d[6]=ctx;
 gPool=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
  kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,
  (vm_size_t)PoolPages*4096,4096);
 IOByteCount span=0;
 gPoolPA=gPool?gPool->getPhysicalSegment(0,&span,kIOMemoryMapperNone):0;
 gPoolVA=gPool?(volatile uint64_t*)gPool->getBytesNoCopy():nullptr;
 if(!gPool||!gPoolPA||span<(IOByteCount)PoolPages*4096||!gPoolVA||(gPoolPA>>gOas)){
  d[1]=3;d[0]=0xfffffff6u;return -9;
 }
 memset((void*)gPoolVA,0,PoolPages*4096);
 flush_dcache((vm_offset_t)gPoolVA,PoolPages*4096,FALSE);
 __asm__ volatile("dsb sy":::"memory");
 gTables=0;gRoot=newTable();
 if(!gRoot){d[1]=4;d[0]=0xfffffff5u;return -10;}
 gGopPa=gopPa;gGopStride=gopStride;gGopW=width;gGopH=height;
 const uint64_t base=uint64_t(gopPa)&~0x1fffffull;
 if(base+0x4000000ull>0x100000000ull){d[1]=9;d[0]=0xfffffff0u;return -15;}
 gWin[0]=base+0x2000000ull;gWin[1]=base+0x3000000ull;
 const uint32_t gopBytes=gopStride*height;
 for(uint64_t off=0;off<gopBytes;off+=4096)
  if(!mapPage(uint64_t(gopPa)+off,uint64_t(gopPa)+off)){d[1]=5;d[0]=0xfffffff4u;restoreRegisters();return -11;}
 wr(0x0,(rd(0x0)&~(1u|(3u<<14)|(1u<<13)))|0x1c36u);
 wr(0x800,0);wr(gCtx,0);wr(0x1800,1);wr(0x1000,0x1f300);
 wr(gCtx+0x10,(7u<<15)|(1u<<4)|(gOas==40?2u:gOas==36?1u:0u));
 wr(gCtx+0x30,(1u<<23)|(2u<<12)|(64-gIas));
 wr(gCtx+0x20,uint32_t(gTab[0].pa));wr(gCtx+0x24,uint32_t(gTab[0].pa>>32));
 wr(gCtx+0x28,0);wr(gCtx+0x2c,0);
 wr(gCtx+0x34,0);wr(gCtx+0x38,0x44);wr(gCtx+0x3c,0);
 if(!tlbSync(true)){d[1]=6;d[0]=0xfffffff3u;restoreRegisters();return -12;}
 wr(gCtx+0x04,0x70000000u);
 wr(gCtx,0x1027u);
 wr(0xc00,uint32_t(bank));
 wr(0x800,0x80000000u);
 if(!tlbSync(false)){d[1]=7;d[0]=0xfffffff2u;restoreRegisters();return -13;}
 if(rd(0xc00)!=uint32_t(bank)||rd(0x800)!=0x80000000u||!(rd(gCtx)&1u)){
  d[1]=10;d[0]=0xffffffefu;restoreRegisters();return -15;}
 d[13]=rd(0x0);d[14]=rd(0x800);d[15]=rd(0xc00);
 uint32_t latched=0,latchUs=0;
 const int lrc=native_legacy_mdp_flush_latch(&latched,&latchUs);
 d[7]=latched;d[8]=latchUs;
 if(lrc||latched!=gopPa){d[1]=8;d[0]=0xfffffff1u;restoreRegisters();return -14;}
 gActive=true;gCurrent=-1;d[0]=0;
 return 0;
}

extern "C" int cheeseburger_dpu_present(IOMemoryDescriptor *memory,uint64_t stride,
    uint32_t width,uint32_t height,uint32_t format){
 auto &d=native_adreno_dpu;
 if(!gActive||!memory)return -1;
 if(width!=gGopW||height!=gGopH||(stride&63)||!stride||stride>0xffffu||
    uint64_t(width)*4>stride||(stride*height)>WindowSlot||format!=0x42475241u)return -2;
 // The route and bank must still be ours before the pipe is pointed at an
 // IOVA; otherwise the address would be taken as physical.
 if(rd(0xc00)!=uint32_t(gBank)||rd(0x800)!=0x80000000u||!(rd(gCtx)&1u))return -7;
 const int target=gCurrent==0?1:0;
 const uint64_t iova=gWin[target];
 const uint64_t bytes=stride*height;
 uint64_t off=0;
 while(off<bytes){
  IOByteCount seg=0;
  const IOPhysicalAddress pa=memory->getPhysicalSegment(off,&seg,kIOMemoryMapperNone);
  if(!pa||!seg||(pa&4095))return -3;
  if(seg>bytes-off)seg=bytes-off;
  const unsigned pages=unsigned((seg+4095)/4096);
  for(unsigned i=0;i<pages;++i)
   if(!mapPage(iova+off+uint64_t(i)*4096,pa+uint64_t(i)*4096))return -4;
  off+=uint64_t(pages)*4096;
 }
 if(!tlbSync(true))return -5;
 uint32_t latchUs=0;
 if(native_legacy_mdp_commit(uint32_t(iova),uint32_t(stride),&latchUs))return -6;
 gCurrent=target;
 d[9]=d[9]+1;d[10]=latchUs;d[7]=uint32_t(iova);
 return 0;
}

extern "C" int cheeseburger_dpu_fallback(void){
 auto &d=native_adreno_dpu;
 if(!gActive)return -1;
 if(gCurrent<0)return 0;
 uint32_t latchUs=0;
 if(native_legacy_mdp_commit(gGopPa,gGopStride,&latchUs)){d[11]=d[11]+1;return -2;}
 gCurrent=-1;d[10]=latchUs;d[7]=gGopPa;return 0;
}
