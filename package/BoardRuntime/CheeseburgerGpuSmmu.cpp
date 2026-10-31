// SPDX-License-Identifier: GPL-2.0-only
// A540 GPU SMMU bank 0 / SID 0 bring-up, ported from the pinned sagit
// QcomSMMU sequence (gpu_smmu.h + ContextTLB.h). The first step installs an
// empty AArch64 stage-1 root table and enables the non-secure context; it does
// not map any buffer yet, so no GPU DMA can run until firmware/rings are added.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include "CheeseburgerGpuWalk.h"
#include "CheeseburgerSmmuMicro.h"
extern "C" {
volatile uint64_t cb_smmu[24];
volatile uint64_t cb_map[4];
volatile uint64_t cb_mapdiag[4];
volatile uint64_t cb_mapret[4];
void flush_dcache(vm_offset_t,unsigned,boolean_t);
void cheeseburger_bootlog_step(const char *) __attribute__((weak_import));
}
namespace {
struct W {IOMemoryMap *map=nullptr;volatile uint32_t *v=nullptr;};
static bool mapw(W &w,uint64_t pa,unsigned size){
 IODeviceMemory *m=IODeviceMemory::withRange(pa,size);
 w.map=m?m->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;
 OSSafeReleaseNULL(m);
 w.v=w.map?(volatile uint32_t*)w.map->getVirtualAddress():nullptr;
 return w.v;
}
static uint32_t rd(W &w,unsigned off){uint32_t x=w.v[off/4];__asm__ volatile("dmb sy":::"memory");return x;}
static void wr(W &w,unsigned off,uint32_t x){__asm__ volatile("dsb sy":::"memory");w.v[off/4]=x;__asm__ volatile("dsb sy":::"memory");}
static bool tlbSync(W &s,unsigned ctx,bool allASIDs){
 wr(s,ctx+(allASIDs?0x618:0x610),0);
 wr(s,ctx+0x7f0,0xffffffffu);
 for(unsigned i=0;i<100000;++i){
  uint32_t st=rd(s,ctx+0x7f4);
  if(st==0xffffffffu)return false;
  if(!(st&1))return true;
  IODelay(1);
 }
 return false;
}
// One aperture mapping for the whole boot. The earlier per-map allocation
// created a new IOMemoryMap on every call and the 4th map never returned.
static W gSmmuW;static bool gSmmuMapped=false;
static W *smmuMap(){
 if(!gSmmuMapped)gSmmuMapped=mapw(gSmmuW,0x5040000,0x10000);
 return gSmmuMapped?&gSmmuW:nullptr;
}
struct MicroIo {
 W &s;
 uint32_t read(unsigned off){return rd(s,off);}
 void write(unsigned off,uint32_t v){wr(s,off,v);}
 void delay(unsigned us){IODelay(us);}
 void mark(const char *){if(&cheeseburger_bootlog_step)cheeseburger_bootlog_step("smresume");}
};
static void reset(){for(unsigned i=0;i<24;++i)cb_smmu[i]=0xdeadbeef;cb_smmu[15]=0x5e7;}
static IOBufferMemoryDescriptor *gRootBuf=nullptr;
static uint64_t gRootPA=0;
static volatile uint64_t *gRootVA=nullptr;
static bool allocTable(uint64_t &pa,volatile uint64_t *&va){
 IOBufferMemoryDescriptor *b=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
   kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,4096,4096);
 if(!b)return false;
 IOByteCount span=0;uint64_t p=b->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
 void *v=b->getBytesNoCopy();
 if(!p||(p&4095)||span<4096||!v||(p>>36))return false;
 memset(v,0,4096);flush_dcache((vm_offset_t)v,4096,FALSE);
 pa=p;va=(volatile uint64_t*)v;return true;
}
static void cleanTable(volatile uint64_t *v){flush_dcache((vm_offset_t)v,4096,FALSE);}
struct Tbl {uint64_t pa;volatile uint64_t *va;};
static Tbl gTables[32];static unsigned gTableCount=0;
static volatile uint64_t *findTable(uint64_t pa){
 for(unsigned i=0;i<gTableCount;++i)if(gTables[i].pa==pa)return gTables[i].va;
 return nullptr;
}
static bool ensureTable(uint64_t &pa,volatile uint64_t *&va){
 if(!allocTable(pa,va))return false;
 if(gTableCount>=32)return false;
 gTables[gTableCount++]={pa,va};return true;
}
static bool mapPage(uint64_t iova,uint64_t pa){
 cb_mapdiag[2]=iova;cb_mapdiag[3]=pa;
 if(!gRootVA||(iova&4095)||(pa&4095)){cb_mapdiag[0]=1;return false;}
 volatile uint64_t *t=gRootVA;
 for(unsigned level=0;level<3;++level){
  unsigned shift=39-9*level;
  unsigned idx=(iova>>shift)&511;
  uint64_t e=t[idx];
  if(!e){
   uint64_t npa=0;volatile uint64_t *nva=nullptr;
   if(!ensureTable(npa,nva)){cb_mapdiag[0]=2;cb_mapdiag[1]=level;return false;}
   t[idx]=npa|3;cleanTable(t);
   t=nva;
  }else{
   if((e&0xfffULL)!=3){cb_mapdiag[0]=3;cb_mapdiag[1]=(level<<32)|(e&0xffffffff);return false;}
   t=findTable(e&~0xfffULL);
   if(!t){cb_mapdiag[0]=4;cb_mapdiag[1]=(level<<32)|(e&0xffffffff);return false;}
  }
 }
 const uint64_t attrs=3ULL|(1ULL<<6)|(2ULL<<8)|(1ULL<<10)|(1ULL<<11)|(3ULL<<53);
 t[(iova>>12)&511]=pa|attrs;cleanTable(t);return true;
}
static void mapTest(W &s,unsigned ctx){
 cb_map[0]=0xdeadbeef;cb_map[1]=cb_map[2]=cb_map[3]=0;
 IOBufferMemoryDescriptor *buf=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
   kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,4096,4096);
 if(!buf){cb_map[0]=1;return;}
 IOByteCount span=0;uint64_t pa=buf->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
 void *va=buf->getBytesNoCopy();
 if(!pa||(pa&4095)||span<4096||!va||(pa>>36)){cb_map[0]=2;return;}
 memset(va,0x5a,4096);flush_dcache((vm_offset_t)va,4096,FALSE);
 uint64_t p1=0,p2=0,p3=0;volatile uint64_t *t1=nullptr,*t2=nullptr,*t3=nullptr;
 if(!gRootVA||!ensureTable(p1,t1)||!ensureTable(p2,t2)||!ensureTable(p3,t3)){cb_map[0]=3;return;}
 const uint64_t iova=0x1000000ULL; // 16 MiB, the sagit MinimumVA
 gRootVA[(iova>>39)&511]=p1|3;cleanTable(gRootVA);
 t1[(iova>>30)&511]=p2|3;cleanTable(t1);
 t2[(iova>>21)&511]=p3|3;cleanTable(t2);
 const uint64_t attrs=3ULL|(1ULL<<6)|(2ULL<<8)|(1ULL<<10)|(1ULL<<11)|(3ULL<<53);
 t3[(iova>>12)&511]=pa|attrs;cleanTable(t3);
 if(!tlbSync(s,ctx,true)){cb_map[0]=4;cb_map[1]=iova;cb_map[2]=pa;return;}
 cb_map[0]=0;cb_map[1]=iova;cb_map[2]=pa;cb_map[3]=t3[(iova>>12)&511];
 IOLog("CB GPU SMMU map: va=%llx pa=%llx pte=%llx root0=%llx\n",iova,pa,cb_map[3],gRootVA[0]);
}
}
// Map `pages` contiguous 4 KiB pages at `iova`. The setup must have installed
// the context and root; each table update is cleaned and one CB TLB sync is
// issued after the whole range.
extern "C" int cheeseburger_gpu_smmu_map(uint64_t iova,uint64_t pa,unsigned pages){
 if(!gRootVA||!pages||(iova&4095)||(pa&4095)||iova<0x1000000ULL||
    iova+uint64_t(pages)*4096>((1ULL<<40))||pa+uint64_t(pages)*4096>(1ULL<<36)){
  IOLog("CB GPU SMMU map reject: iova=%llx pa=%llx pages=%u\n",iova,pa,pages);return -1;
 }
 cb_mapret[2]=iova;cb_mapret[3]=pages;
 for(unsigned i=0;i<pages;++i)if(!mapPage(iova+uint64_t(i)*4096,pa+uint64_t(i)*4096)){
  cb_mapret[0]=uint64_t(-2);cb_mapret[1]=i;
  IOLog("CB GPU SMMU map fail: page=%u stage=%llu info=%llx iova=%llx pa=%llx\n",
   i,cb_mapdiag[0],cb_mapdiag[1],cb_mapdiag[2],cb_mapdiag[3]);return -2;
 }
 IOLog("CB GPU SMMU map ptes: iova=%llx pages=%u\n",iova,pages);
 W *sp=smmuMap();if(!sp){cb_mapret[0]=uint64_t(-3);IOLog("CB GPU SMMU map no mmio\n");return -3;}
 IOLog("CB GPU SMMU map pre-tlb: iova=%llx\n",iova);
 bool ok=tlbSync(*sp,0x8000,true);uint32_t tlb=ok?0:rd(*sp,0x8000+0x7f4);
 IOLog("CB GPU SMMU map ok=%d iova=%llx pages=%u tlb=%x\n",ok?1:0,iova,pages,tlb);
 cb_mapret[0]=ok?0:uint64_t(-4);cb_mapret[1]=tlb;
 return ok?0:-4;
}
// Read-only software walk of the live tables for diagnostics; no MMIO.
struct KernelTables {
 volatile uint64_t *root(){return gRootVA;}
 uint64_t entry(volatile uint64_t *t,unsigned index){return t[index];}
 volatile uint64_t *next(uint64_t pa){return findTable(pa);}
};
extern "C" uint64_t cheeseburger_gpu_smmu_root(void){return gRootPA;}
extern "C" int cheeseburger_gpu_smmu_lookup(uint64_t iova,uint64_t *pte){
 if(!pte||(iova&4095))return -1;
 KernelTables tables;
 return CheeseburgerGpu::lookupLeaf(tables,iova,*pte);
}
extern "C" void cheeseburger_gpu_smmu_setup(void){
 reset();
 W *smp=smmuMap();if(!smp){cb_smmu[0]=1;return;}
 W &s=*smp;
 uint32_t id0=rd(s,0x20),id1=rd(s,0x24),id2=rd(s,0x28);
 cb_smmu[10]=id0;cb_smmu[11]=id1;cb_smmu[12]=id2;
 if(id0==0xffffffffu||id1==0xffffffffu||id2==0xffffffffu){cb_smmu[0]=2;return;}
 // Same fixed-width checks as sagit QcomSMMU::SMMUStartup::probe.
 const unsigned widths[]={32,36,40,42,44,48,52};
 unsigned pages=1u<<(((id1>>28)&7)+1);
 unsigned groups=id0&255,sidBits=(id0>>9)&15;
 unsigned inBits=widths[(id2>>8)&15],outBits=widths[id2&15];
 unsigned out2=widths[(id2>>4)&15];
 if(inBits>40)inBits=40;
 if(out2<outBits)outBits=out2;
 if(outBits>40)outBits=40;
 if((id0&((1u<<30)|(1u<<27)))!=((1u<<30)|(1u<<27))||!(id2&(1u<<12))||
    (id1&(1u<<31))||pages!=8||!groups||groups>128||!sidBits||
    (id1&255)>8||(id1&0xff0000)){cb_smmu[0]=3;return;}
 unsigned ctx=pages*4096;
 if(ctx!=0x8000||rd(s,ctx)&1||rd(s,ctx+0x58)||rd(s,0x48)||(rd(s,0)&8)){cb_smmu[0]=4;return;}
 // SMR0/S2CR0 are free on this cold GPU.
 if(rd(s,0x800)||rd(s,0xc00)!=0x000200ffu){cb_smmu[0]=5;return;}
 // One physically contiguous, cache-cleaned, zero root table. Keep it for the
 // boot; the mapper that adds leaf entries will reuse it.
 gRootBuf=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
   kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,4096,4096);
 if(!gRootBuf){cb_smmu[0]=6;return;}
 IOByteCount span=0;gRootPA=gRootBuf->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
 gRootVA=(volatile uint64_t*)gRootBuf->getBytesNoCopy();
 if(!gRootPA||(gRootPA&4095)||span<4096||!gRootVA||(gRootPA>>36)){cb_smmu[0]=7;return;}
 memset((void*)gRootVA,0,4096);flush_dcache((vm_offset_t)gRootVA,4096,FALSE);
 cb_smmu[1]=gRootPA;cb_smmu[13]=(outBits<<8)|inBits;
 // Global setup, then bank 0, exactly as the sagit non-secure sequence.
 wr(s,0,(rd(s,0)&~(1u|(3u<<14)|(1u<<13)))|0x1c36);
 wr(s,0x800,0);wr(s,ctx,0);wr(s,0x1800,1);wr(s,0x1000,0x1f300);
 wr(s,ctx+0x10,(7u<<15)|(1u<<4)|(outBits==40?2u:outBits==36?1u:0u));
 wr(s,ctx+0x30,(1u<<23)|(2u<<12)|(64-inBits));
 wr(s,ctx+0x20,(uint32_t)gRootPA);wr(s,ctx+0x24,(uint32_t)(gRootPA>>32));
 wr(s,ctx+0x28,0);wr(s,ctx+0x2c,0);wr(s,ctx+0x34,0);wr(s,ctx+0x38,0x44);wr(s,ctx+0x3c,0);
 if(!tlbSync(s,ctx,true)){cb_smmu[0]=8;cb_smmu[5]=rd(s,ctx);return;}
 // sagit's proven GPU control value. Per arm-smmu.h CB SCTLR bits:
 // 0x1027 = M(0)|TRE(1)|AFE(2)|CFRE(5)|S1_ASIDPNE(12); 0x1167 adds
 // CFIE(6)|HUPCF(8) (delta 0x140). Do not assert per-bit fault semantics
 // until they are confirmed against this SoC's SMMU implementation.
 wr(s,ctx,0x1027);wr(s,0xc00,0);wr(s,0x800,0x80000000u);
 if(!tlbSync(s,ctx,false)){cb_smmu[0]=9;cb_smmu[5]=rd(s,ctx);return;}
 // Vendor arm-smmu IMPL_DEF1 micro-MMU control (DTB qcom,micro-mmu-control
 // 0x6000). LOCAL_HALT_REQ is bit 2; IDLE is bit 3. This DTB does not set
 // qcom,enable-static-cb, so a direct write is the vendor path. A request bit
 // that survives the write means the resume did not happen: stop here instead
 // of starting the CP on a halted translation pipeline.
 {
  MicroIo io{s};
  const auto micro=CheeseburgerGpu::resumeMicroMmu(io,3000);
  cb_smmu[16]=micro.before;cb_smmu[17]=micro.after;
  cb_smmu[19]=uint64_t(int64_t(micro.result));
  cb_smmu[18]=rd(s,0x2000);
  if(micro.result==CheeseburgerGpu::MicroResult::Timeout||micro.result==CheeseburgerGpu::MicroResult::Invalid){
   cb_smmu[0]=10;
   IOLog("CB GPU SMMU micro-MMU resume failed: result=%d before=%x after=%x\n",
    int(micro.result),micro.before,micro.after);
   return;
  }
 }
 cb_smmu[0]=0;
 cb_smmu[2]=rd(s,0);cb_smmu[3]=rd(s,0x800);cb_smmu[4]=rd(s,0xc00);
 cb_smmu[5]=rd(s,ctx);cb_smmu[6]=rd(s,ctx+0x20);cb_smmu[7]=rd(s,ctx+0x24);
 cb_smmu[8]=rd(s,ctx+0x30);cb_smmu[9]=rd(s,ctx+0x58);
 IOLog("CB GPU SMMU: root=%llx sctlr=%x smr=%x s2cr=%x cb=%x tcr=%x fsr=%x ids=%x/%x/%x\n",
  gRootPA,cb_smmu[2],cb_smmu[3],cb_smmu[4],cb_smmu[5],cb_smmu[8],cb_smmu[9],id0,id1,id2);
 IOLog("CB GPU SMMU: micro=%x/%x impl0=%x\n",uint32_t(cb_smmu[16]),uint32_t(cb_smmu[17]),uint32_t(cb_smmu[18]));
 unsigned mt=0;if(PE_parse_boot_argn("cheeseburger-gpu-map-test",&mt,sizeof mt)&&mt)mapTest(s,ctx);
}
