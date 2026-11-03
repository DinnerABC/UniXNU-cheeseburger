// SPDX-License-Identifier: GPL-2.0-only
// A540 GPU SMMU bank 0 / SID 0 bring-up, ported from the pinned sagit
// QcomSMMU sequence (gpu_smmu.h + ContextTLB.h). The first step installs an
// empty AArch64 stage-1 root table and enables the non-secure context; it does
// not map any buffer yet, so no GPU DMA can run until firmware/rings are added.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include "CheeseburgerGpuWalk.h"
#include "CheeseburgerSmmuMicro.h"
#include "CheeseburgerSmmuId.h"
extern "C" {
int cheeseburger_gpu_smmu4k_setup(void);
int cheeseburger_gpu_smmu4k_active(void);
int cheeseburger_gpu_smmu4k_map(uint64_t,uint64_t,unsigned);
int cheeseburger_gpu_smmu4k_unmap(uint64_t,unsigned);
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
// 64-bit MMIO registers (TTBR0, ATS1PR) must be written as one store, as the
// vendor smmu_writeq does on 64-bit; two 32-bit halves can land non-atomically.
static void wr64(W &w,unsigned off,uint64_t value){
 __asm__ volatile("dsb sy":::"memory");
 *reinterpret_cast<volatile uint64_t *>(reinterpret_cast<uintptr_t>(w.v)+off)=value;
 __asm__ volatile("dsb sy":::"memory");
}
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
static void reset(){for(unsigned i=0;i<24;++i)cb_smmu[i]=0xdeadbeef;cb_smmu[15]=0x5e7;cb_smmu[22]=0;cb_smmu[23]=0;}
static unsigned gLowTables=0,gHighTables=0;
static IOBufferMemoryDescriptor *gRootBuf=nullptr;
static uint64_t gRootPA=0;
static volatile uint64_t *gRootVA=nullptr;
static unsigned gIovaBits=0,gPaBits=0,gStartLevel=0;
// DarwinBoot reserves a low (<4 GiB) pool before ExitBootServices and passes it
// through the command line. Use it for every table when present; the SMMU can
// otherwise fail to read the high pages XNU normally hands out.
static IOMemoryMap *gPoolMap=nullptr;
static volatile uint8_t *gPoolVA=nullptr;
static uint64_t gPoolPA=0,gPoolBytes=0,gPoolUsed=0;
static bool gPoolTried=false;
static bool allocPoolPage(uint64_t &pa,void *&va){
 if(!gPoolTried){
  gPoolTried=true;unsigned ppa=0;const unsigned pbytes=65536; // DarwinBoot allocates 16 pages
  if(PE_parse_boot_argn("cheeseburger-smmu-pool",&ppa,sizeof ppa)&&ppa&&
     uint64_t(ppa)+pbytes<=0x100000000ull){
   IOMemoryDescriptor *d=IOMemoryDescriptor::withPhysicalAddress(ppa,pbytes,kIODirectionInOut);
   gPoolMap=d?d->map(kIOMapAnywhere|kIOMapCopybackCache):nullptr;OSSafeReleaseNULL(d);
   if(gPoolMap){gPoolVA=(volatile uint8_t*)gPoolMap->getVirtualAddress();gPoolPA=ppa;gPoolBytes=pbytes;
    cb_smmu[20]=gPoolPA;cb_smmu[21]=gPoolBytes;
    IOLog("CB GPU SMMU pool: pa=%llx bytes=%llu\n",gPoolPA,gPoolBytes);}
   else IOLog("CB GPU SMMU pool map failed pa=%x bytes=%u\n",ppa,pbytes);
  }
 }
 if(!gPoolVA||gPoolUsed+4096>gPoolBytes)return false;
 pa=gPoolPA+gPoolUsed;va=(void*)(gPoolVA+gPoolUsed);gPoolUsed+=4096;++gLowTables;cb_smmu[14]=gPoolUsed;cb_smmu[22]=gLowTables;
 return true;
}
// Try the restricted low-physical pool first and verify the returned PA is
// really below 4 GiB; the mask is a placement request, not a guarantee. Fall
// back to the normal pool and record the split so a low-table result is never
// mistaken for a high-table one.
static IOBufferMemoryDescriptor *allocPage(uint64_t &pa,void *&va,bool &low){
 low=false;
 IOBufferMemoryDescriptor *b=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
   kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,4096,0x00000000fffff000ull);
 if(b){
  IOByteCount span=0;uint64_t p=b->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
  void *v=b->getBytesNoCopy();
  if(p&&!(p&4095)&&span>=4096&&v&&p<0x100000000ull){pa=p;va=v;low=true;return b;}
  b->release();
 }
 b=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
   kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,4096,4096);
 if(!b)return nullptr;
 IOByteCount span=0;uint64_t p=b->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
 void *v=b->getBytesNoCopy();
 if(!p||(p&4095)||span<4096||!v||(p>>36)){b->release();return nullptr;}
 pa=p;va=v;return b;
}
static bool allocTable(uint64_t &pa,volatile uint64_t *&va){
 void *v=nullptr;bool low=false;
 if(allocPoolPage(pa,v)){memset(v,0,4096);flush_dcache((vm_offset_t)v,4096,FALSE);va=(volatile uint64_t*)v;return true;}
 if(!allocPage(pa,v,low))return false;
 memset(v,0,4096);flush_dcache((vm_offset_t)v,4096,FALSE);
 va=(volatile uint64_t*)v;
 if(low)++gLowTables;else ++gHighTables;
 cb_smmu[22]=gLowTables;cb_smmu[23]=gHighTables;
 return true;
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
 for(unsigned level=gStartLevel;level<3;++level){
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
 if(!pa||(pa&4095)||span<4096||!va||(pa>>gPaBits)){cb_map[0]=2;return;}
 memset(va,0x5a,4096);flush_dcache((vm_offset_t)va,4096,FALSE);
 const uint64_t iova=0x1000000ULL;
 if(!gRootVA||!mapPage(iova,pa)){cb_map[0]=3;return;}
 if(!tlbSync(s,ctx,true)){cb_map[0]=4;cb_map[1]=iova;cb_map[2]=pa;return;}
 volatile uint64_t *t=gRootVA;
 for(unsigned level=gStartLevel;level<3;++level)
  t=findTable(t[(iova>>(39-9*level))&511]&~0xfffULL);
 uint64_t pte=t?t[(iova>>12)&511]:0;
 cb_map[0]=0;cb_map[1]=iova;cb_map[2]=pa;cb_map[3]=pte;
 IOLog("CB GPU SMMU map: va=%llx pa=%llx pte=%llx root0=%llx\n",iova,pa,pte,gRootVA[0]);
}
}
// Map `pages` contiguous 4 KiB pages at `iova`. The setup must have installed
// the context and root; each table update is cleaned and one CB TLB sync is
// issued after the whole range.
extern "C" int cheeseburger_gpu_smmu_map(uint64_t iova,uint64_t pa,unsigned pages){
 if(cheeseburger_gpu_smmu4k_active())return cheeseburger_gpu_smmu4k_map(iova,pa,pages);
 if(!gRootVA||!pages||(iova&4095)||(pa&4095)||iova<0x1000000ULL||
    iova+uint64_t(pages)*4096>(1ULL<<gIovaBits)||pa+uint64_t(pages)*4096>(1ULL<<gPaBits)){
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
static bool unmapPage(uint64_t iova){
 if(!gRootVA||(iova&4095))return false;
 volatile uint64_t *t=gRootVA;
 for(unsigned level=gStartLevel;level<3;++level){
  unsigned shift=39-9*level;uint64_t e=t[(iova>>shift)&511];
  if(!e)return true; // already clear
  if((e&0xfffULL)!=3)return false;
  t=findTable(e&~0xfffULL);if(!t)return false;
 }
 unsigned index=(iova>>12)&511;
 if(!t[index])return true;
 t[index]=0;cleanTable(t);return true;
}
// Tear down a mapped BO range. The caller must have stopped using the range
// on the GPU (every submit waits for its fence), so clearing and one TLB sync
// cannot race an in-flight fetch.
extern "C" int cheeseburger_gpu_smmu_unmap(uint64_t iova,unsigned pages){
 if(cheeseburger_gpu_smmu4k_active())return cheeseburger_gpu_smmu4k_unmap(iova,pages);
 if(!gRootVA||!pages||(iova&4095)||iova<0x1000000ULL||
    iova+uint64_t(pages)*4096>(1ULL<<gIovaBits))return -1;
 for(unsigned i=0;i<pages;++i)if(!unmapPage(iova+uint64_t(i)*4096))return -2;
 W *sp=smmuMap();if(!sp)return -3;
 bool ok=tlbSync(*sp,0x8000,true);
 return ok?0:-4;
}
// Read-only software walk of the live tables for diagnostics; no MMIO.
struct KernelTables {
 volatile uint64_t *root(){return gRootVA;}
 uint64_t entry(volatile uint64_t *t,unsigned index){return t[index];}
 volatile uint64_t *next(uint64_t pa){return findTable(pa);}
};
// Read-only hardware translation probe (ARM SMMU CB_ATS1PR/ATSR/PAR). It
// exercises the real stage-1 walk of the installed tables; a fault or a
// mismatched PA is a concrete SMMU failure, unlike a software PTE lookup.
// No memory is written and the micro-MMU stays running (vendor
// iova_to_phys_hard_no_halt path). Returns 0 with the raw PAR in *par.
// Hardware translation probe (ATOS) in the vendor hard order: halt the
// micro-MMU, terminate any stalled context fault (CB_RESUME=1), clear FSR,
// disable CFCFG for the probe, write ATS1PR/read PAR, then restore SCTLR and
// resume. The previous no-halt-first attempt left ATSR ACTIVE and the halt
// could never drain, so do not issue ATOS before the halt.
// diag[0..7] = ctl before, ctl after halt, ATSR, FSR, FSYNR0, FAR_LO,
// ctl after resume, SCTLR. *par = raw PAR (bit0 fault).
// Returns 0 translated, -2 ATOS timeout, -3 halt timeout, -4 resume failure.
extern "C" int cheeseburger_gpu_smmu_atos(uint64_t iova,uint64_t *par,volatile uint64_t *diag){
 W *sp=smmuMap();if(!sp||!par)return -1;
 W &s=*sp;const unsigned cb=0x8000;const uint64_t va=iova&~0xfffull;
 uint32_t ctl0=rd(s,0x6000);if(diag)diag[0]=ctl0;
 // Always request the halt (vendor arm_smmu_halt_nowait), even when IDLE is
 // already set: idle is not the same as the halted state ATOS expects.
 wr(s,0x6000,ctl0|(1u<<2));
 uint32_t idle=0;unsigned i=0;
 for(;i<30000;++i){idle=rd(s,0x6000);if(idle&(1u<<3))break;IODelay(1);}
 ctl0=idle;
 if(diag)diag[1]=ctl0;
 // Require both the request and the idle acknowledgement. Idle alone may be
 // the running quiescent state, not the halted state ATOS expects.
 if((ctl0&((1u<<2)|(1u<<3)))!=((1u<<2)|(1u<<3))){
  wr(s,0x6000,rd(s,0x6000)&~(1u<<2));
  return -3;
 }
 if(diag){diag[8]=rd(s,cb+0x58);diag[9]=rd(s,cb+0x68);diag[10]=rd(s,cb+0x60);
  diag[11]=rd(s,cb+0x20);diag[12]=rd(s,cb+0x24);diag[13]=rd(s,cb+0x30);diag[14]=gRootPA;}
 wr(s,cb+0x08,1);                    // CB_RESUME = TERMINATE
 wr(s,cb+0x58,0xffffffffu);          // clear FSR before ATOS
 uint32_t sctlr=rd(s,cb);            // ours already has CFCFG=0
 wr(s,cb,sctlr&~(1u<<7));            // disable stall mode for the probe
 if(diag)diag[7]=sctlr;
 wr64(s,cb+0x800,va);
 uint32_t atsr=0;unsigned j=0;
 for(;j<10000;++j){atsr=rd(s,cb+0x8f0);if(!(atsr&1))break;IODelay(1);}
 if(diag)diag[2]=atsr;
 uint64_t p=0;
 if(!(atsr&1))p=uint64_t(rd(s,cb+0x50))|(uint64_t(rd(s,cb+0x54))<<32);
 if(diag){diag[3]=rd(s,cb+0x58);diag[4]=rd(s,cb+0x68);diag[5]=rd(s,cb+0x60);}
 wr(s,cb,sctlr);                     // restore SCTLR
 uint32_t now=rd(s,0x6000);
 wr(s,0x6000,now&~(1u<<2));
 uint32_t after=rd(s,0x6000);if(diag)diag[6]=after;
 *par=(atsr&1)?0xffffffffffffffffull:p;
 if(after&(1u<<2))return -4;
 return (atsr&1)?-2:0;
}
extern "C" uint64_t cheeseburger_gpu_smmu_root(void){return gRootPA;}
// sheng invalidates the context-bank TLB before every submission (int-070: the
// CP fetched a zero IB whose PTE and DRAM were both correct, after the address
// had been mapped to other pages). TLBIALL+TLBSYNC is safe while the CP runs;
// the caller must fail closed and not kick when this does not complete.
extern "C" int cheeseburger_gpu_smmu_invalidate(void){
 W *sp=smmuMap();if(!sp)return -1;
 return tlbSync(*sp,0x8000,true)?0:-2;
}
// Read-only micro-MMU control (SMMU+0x6000): bit2 LOCAL_HALT_REQ, bit3 IDLE.
// A runtime TLBIALL that left the translation pipeline halted would stop every
// CP fetch while all RBBM reads still succeed; the submit path samples this
// before its doorbell.
extern "C" int cheeseburger_gpu_smmu_micro(void){
 W *sp=smmuMap();if(!sp)return -1;
 return int(rd(*sp,0x6000)&0xffffu);
}
// Re-run the vendor micro-MMU resume before a runtime doorbell. A translation
// pipeline left with LOCAL_HALT_REQ stalls every CP fetch without a context
// fault; intact (request clear) makes this a no-op.
extern "C" int cheeseburger_gpu_smmu_resume_micro(void){
 W *sp=smmuMap();if(!sp)return -1;
 struct Io {
  W &s;
  uint32_t read(unsigned off){return rd(s,off);}
  void write(unsigned off,uint32_t v){wr(s,off,v);}
  void delay(unsigned us){IODelay(us);}
  void mark(const char *){}
 } io{*sp};
 const auto micro=CheeseburgerGpu::resumeMicroMmu(io,3000);
 cb_smmu[16]=micro.before;cb_smmu[17]=micro.after;cb_smmu[19]=uint64_t(int64_t(micro.result));
 return int(micro.result);
}
extern "C" int cheeseburger_gpu_smmu_lookup(uint64_t iova,uint64_t *pte){
 if(!pte||(iova&4095))return -1;
 KernelTables tables;
 return CheeseburgerGpu::lookupLeaf(tables,iova,*pte,gStartLevel);
}
extern "C" void cheeseburger_gpu_smmu_setup(void){
 // Migration bridge to the official sheng ArmSmmu4K path. Once a device round
 // confirms the probe fence, map and user submission on it, the pinned
 // sequence below is removed and the kit becomes the only path.
 { unsigned k=0;
   if(PE_parse_boot_argn("cheeseburger-gpu-smmu4k",&k,sizeof k)&&k){
    int kr=cheeseburger_gpu_smmu4k_setup();
    if(kr==0){IOLog("CB GPU SMMU: sheng ArmSmmu4K path active\n");return;}
    IOLog("CB GPU SMMU: sheng ArmSmmu4K setup failed %d; using the pinned sequence\n",kr);
   } }
 reset();
 W *smp=smmuMap();if(!smp){cb_smmu[0]=1;return;}
 W &s=*smp;
 uint32_t id0=rd(s,0x20),id1=rd(s,0x24),id2=rd(s,0x28);
 cb_smmu[10]=id0;cb_smmu[11]=id1;cb_smmu[12]=id2;
 if(id0==0xffffffffu||id1==0xffffffffu||id2==0xffffffffu){cb_smmu[0]=2;return;}
 // Same fixed-width checks as sagit QcomSMMU::SMMUStartup::probe.
 unsigned pages=1u<<(((id1>>28)&7)+1);
 unsigned groups=id0&255,sidBits=(id0>>9)&15;
 // Vendor ID2 layout: IAS bits 0..3, OAS bits 4..7, UBS bits 8..11. Values
 // 7..15 are reserved; validate before indexing the supported-width table.
 CheeseburgerGpu::IdWidths iw;
 if(!CheeseburgerGpu::decodeIdWidths(id2,iw)){
  cb_smmu[0]=11;
  IOLog("CB GPU SMMU: reserved ID2 width code id2=%x\n",id2);
  return;
 }
 unsigned ias=iw.ias,oas=iw.oas;
 if(ias>40)ias=40;
 if(oas>40)oas=40;
 gIovaBits=ias;gPaBits=oas;gStartLevel=ias>39?0:1;
 if((id0&((1u<<30)|(1u<<27)))!=((1u<<30)|(1u<<27))||!(id2&(1u<<12))||
    (id1&(1u<<31))||pages!=8||!groups||groups>128||!sidBits||
    (id1&255)>8||(id1&0xff0000)){cb_smmu[0]=3;return;}
 unsigned ctx=pages*4096;
 if(ctx!=0x8000||rd(s,ctx)&1||rd(s,ctx+0x58)||rd(s,0x48)||(rd(s,0)&8)){cb_smmu[0]=4;return;}
 // SMR0/S2CR0 are free on this cold GPU.
 if(rd(s,0x800)||rd(s,0xc00)!=0x000200ffu){cb_smmu[0]=5;return;}
 // One physically contiguous, cache-cleaned, zero root table. Keep it for the
 // boot; the mapper that adds leaf entries will reuse it.
 {void *rootVA=nullptr;bool rootLow=false;
  if(allocPoolPage(gRootPA,rootVA)){gRootBuf=nullptr;}
  else{gRootBuf=allocPage(gRootPA,rootVA,rootLow);
   if(rootLow)++gLowTables;else if(gRootBuf)++gHighTables;
   cb_smmu[22]=gLowTables;cb_smmu[23]=gHighTables;}
  gRootVA=(volatile uint64_t*)rootVA;}
 if(!gRootVA||!gRootPA||(gRootPA&4095)||(gRootPA>>36)){cb_smmu[0]=7;return;}
 memset((void*)gRootVA,0,4096);flush_dcache((vm_offset_t)gRootVA,4096,FALSE);
 cb_smmu[1]=gRootPA;cb_smmu[13]=(oas<<8)|ias;
 // Global setup, then bank 0, exactly as the sagit non-secure sequence.
 wr(s,0,(rd(s,0)&~(1u|(3u<<14)|(1u<<13)))|0x1c36);
 wr(s,0x800,0);wr(s,ctx,0);wr(s,0x1800,1);wr(s,0x1000,0x1f300);
 wr(s,ctx+0x10,(7u<<15)|(1u<<4)|(oas==40?2u:oas==36?1u:0u));
 wr(s,ctx+0x30,(1u<<23)|(2u<<12)|(64-ias));
 wr64(s,ctx+0x20,gRootPA);
 wr64(s,ctx+0x28,0);wr(s,ctx+0x34,0);wr(s,ctx+0x38,0x44);wr(s,ctx+0x3c,0);
 if(!tlbSync(s,ctx,true)){cb_smmu[0]=8;cb_smmu[5]=rd(s,ctx);return;}
 // sagit's proven GPU control value. Per arm-smmu.h CB SCTLR bits:
 // 0x1027 = M(0)|TRE(1)|AFE(2)|CFRE(5)|S1_ASIDPNE(12); 0x1167 adds
 // CFIE(6)|HUPCF(8) (delta 0x140). Do not assert per-bit fault semantics
 // until they are confirmed against this SoC's SMMU implementation.
 // Qualcomm SMMU-v2 ACTLR: inner/outer/non-shareable hazard flags
 // (vendor ACTLR_QCOM_OSH/ISH/NSH, bits 28..30). They are part of the
 // vendor context init and affect TLB/page-walk coherency on this SoC.
 wr(s,ctx+0x04,0x70000000u);
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

/* Read-only snapshot of the stream routes and both context banks, as the
 * vendor driver's attach-impl-defs and our own stream ownership would see
 * them. No writes, no TLB operations. */
volatile uint32_t native_adreno_smmu_dump[24];
extern "C" int cheeseburger_gpu_smmu_dump(void){
 W *sp=smmuMap();if(!sp)return -1;
 W &s=*sp;
 native_adreno_smmu_dump[0]=rd(s,0x0);            // SCR
 for(unsigned i=0;i<6;++i){
  native_adreno_smmu_dump[1+i*2]=rd(s,0x800+4*i);
  native_adreno_smmu_dump[2+i*2]=rd(s,0xc00+4*i);
 }
 native_adreno_smmu_dump[13]=rd(s,0x8000);        // CB0 SCTLR
 native_adreno_smmu_dump[14]=rd(s,0x8020);        // CB0 TTBR0 low
 native_adreno_smmu_dump[15]=rd(s,0x8024);        // CB0 TTBR0 high
 native_adreno_smmu_dump[16]=rd(s,0x8030);        // CB0 TCR
 native_adreno_smmu_dump[17]=rd(s,0x8058);        // CB0 FSR
 native_adreno_smmu_dump[18]=rd(s,0x8068);        // CB0 FSYNR0
 native_adreno_smmu_dump[19]=rd(s,0xc000);        // CB1 SCTLR
 native_adreno_smmu_dump[20]=rd(s,0xc020);        // CB1 TTBR0 low
 native_adreno_smmu_dump[21]=rd(s,0xc024);        // CB1 TTBR0 high
 native_adreno_smmu_dump[22]=rd(s,0x48);          // global FSR
 native_adreno_smmu_dump[23]=0x5a5a0001u;
 IOLog("CB GPU SMMU dump: scr=%x s0=%x/%x s1=%x/%x s2=%x/%x cb0=%x/%llx/%x cb1=%x/%llx gfsr=%x\n",
  native_adreno_smmu_dump[0],native_adreno_smmu_dump[1],native_adreno_smmu_dump[2],
  native_adreno_smmu_dump[3],native_adreno_smmu_dump[4],native_adreno_smmu_dump[5],
  native_adreno_smmu_dump[6],native_adreno_smmu_dump[13],
  (unsigned long long)(uint64_t(native_adreno_smmu_dump[14])|(uint64_t(native_adreno_smmu_dump[15])<<32)),
  native_adreno_smmu_dump[16],native_adreno_smmu_dump[19],
  (unsigned long long)(uint64_t(native_adreno_smmu_dump[20])|(uint64_t(native_adreno_smmu_dump[21])<<32)),
  native_adreno_smmu_dump[22]);
 return 0;
}
