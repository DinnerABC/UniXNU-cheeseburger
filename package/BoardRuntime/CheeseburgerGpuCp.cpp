// SPDX-License-Identifier: GPL-2.0-only
// Comprehensive A540 firmware/CP input probe: parse the QGFW container built
// from the device vendor blobs, allocate the ring and CP/GPMU buffers, map all
// of them through the SMMU, and read the CP/RBBM/VBIF/secure/GPMU state. It
// does not write any CP register yet; the CP/GPMU startup follows once these
// inputs and the halt/secure state are known.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include "CheeseburgerAtosGate.h"
#if __has_include("CheeseburgerGpuFirmware.h")
#include "CheeseburgerGpuFirmware.h"
#include "CheeseburgerGpuStart.h"
#include "CheeseburgerGpuEngine540.h"
#include "CheeseburgerGpuQuiesce.h"
#include "CheeseburgerGpuWait.h"
#define CB_GPU_FW 1
#else
static const unsigned char gpu_firmware_blob[1]={0};
static const unsigned gpu_firmware_size=0;
#define CB_GPU_FW 0
#endif
extern "C" {
volatile uint64_t cb_cp[128];
extern volatile uint64_t cb_smmu[24];
extern volatile uint64_t cb_zap[32];
extern volatile uint64_t cb_gpu[64];
void flush_dcache(vm_offset_t,unsigned,boolean_t);
int cheeseburger_gpu_smmu_map(uint64_t,uint64_t,unsigned);
int cheeseburger_gpu_smmu_unmap(uint64_t,unsigned);
int cheeseburger_gpu_smmu4k_active(void);
int cheeseburger_gpu_smmu4k_faulted(void);
int cheeseburger_gpu_service_publish(void);
volatile uint64_t cb_gpu_paths[8];
volatile uint64_t native_adreno_probe_exec;
// Kernel IB self-test in a page the CP already fetched: rc, stage and the three
// sentinel words the IB was told to write, plus the fence page word.
volatile uint32_t native_adreno_selftest[8];
// Runtime-map test: rc, map rc, three target words, the leaf PTE and the IOVA.
volatile uint32_t native_adreno_rtmap[8];
// Same test with the buffer mapped inside the user window (0x4020000).
volatile uint32_t native_adreno_rtmap2[8];
// Physical-page test: map rc, PA, sequence and submit rc for a user-owned PA.
volatile uint32_t native_adreno_pa_test[8];
// CP_MEM_TO_MEM read of a user address: rc, sequence, copied words and source.
volatile uint32_t native_adreno_copy_test[8];
// Staged gate results: A/B/C rc and engine stage, each stage's three
// sentinels, stage C's four CP-read words and the kernel's view of the first
// client IB word.
volatile uint32_t native_adreno_gate[22];
// Late kernel-page write control: rc, PA, target words, map rc and sequence.
volatile uint32_t native_adreno_late_test[8];
// Low pool: count and first page, filled at probe time.
volatile uint32_t native_adreno_low_pool[4];
// Diagnostic: when gpu-inline-ib=1, the client's first IB words are run from
// the ring instead of being fetched through CP_INDIRECT_BUFFER.
volatile uint32_t *cb_inline_ib=nullptr;
unsigned cb_inline_dwords=0;
int vnode_lookup(const char *,int,void **,void *);
void vnode_put(void *);
void *vfs_context_current(void);
int cheeseburger_gpu_smmu_lookup(uint64_t,uint64_t *);
uint64_t cheeseburger_gpu_smmu_root(void);
int cheeseburger_gpu_smmu_atos(uint64_t,uint64_t *,volatile uint64_t *);
int cheeseburger_gpu_smmu_invalidate(void);
int cheeseburger_gpu_smmu4k_ttbr(uint64_t *);
int cheeseburger_gpu_smmu_micro(void);
int cheeseburger_gpu_smmu_resume_micro(void);
int cheeseburger_gpu_zap_load(volatile uint32_t *);
void cheeseburger_bootlog_step(const char *) __attribute__((weak_import));
}
namespace CheeseburgerGpu { struct SubmitRecord; }
static int keepAliveInline(uint32_t,CheeseburgerGpu::SubmitRecord &);
// Set once a submission, drain or table update cannot be proven safe. Defined
// here because the probe's handoff keep-alive can fail before the engine's
// submission entry points are reached.
static bool gEngineSubmitFailed=false;
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
static uint32_t le32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
static uint32_t parity(uint32_t v){v^=v>>16;v^=v>>8;v^=v>>4;v^=v>>2;v^=v>>1;return (~v)&1;}
// QGFW container from scripts/p21-gpu-firmware-header.py: magic "QGFW", three
// 16-byte entries (pm4, pfp, gpmu), 16-byte aligned payloads. The GPMU file
// uses the same .fw2 metadata layout as the sagit Firmware class.
struct Firmware {
 const uint8_t *f[3]={};uint32_t bytes[3]={};uint32_t first=0,instructions=0;
 bool open(const uint8_t *p,uint32_t n){
  if(!p||n<80||n>3*1024*1024+128)return false;
  if(le32(p)!=0x57464751u||le32(p+4)!=1||le32(p+8)!=3||le32(p+12)!=n||le32(p+16)!=16)return false;
  uint32_t end=80;
  for(unsigned i=0;i<3;++i){
   const uint8_t *e=p+32+16*i;uint32_t off=le32(e+4),len=le32(e+8);
   if(le32(e)!=i||le32(e+12)||(off&15)||off<end||len<8||off>n||len>n-off)return false;
   f[i]=p+off;bytes[i]=len;end=off+len;
  }
  if(end!=n||bytes[2]<16)return false;
  const uint8_t *g=f[2];uint32_t remaining=le32(g),metadata=le32(g+8);
  if(le32(g+4)!=2||remaining<3||remaining>=bytes[2]/4||metadata>remaining-3)return false;
  first=metadata+3;instructions=remaining-metadata-2;
  return instructions&&instructions<=0x1000;
 }
 uint32_t cpBytes(unsigned i) const{return i<2&&instructions?bytes[i]-4:0;}
 uint32_t gpmuWords() const{return instructions?instructions+(instructions+126)/127:0;}
 bool copyCP(unsigned i,void *dst,uint32_t cap) const{
  uint32_t n=cpBytes(i);if(!dst||!n||cap<n)return false;
  memcpy(dst,f[i]+4,n);memset((uint8_t*)dst+n,0,cap-n);return true;
 }
 bool gpmuCommands(uint32_t *out,uint32_t cap) const{
  if(!out||!instructions||cap<gpmuWords())return false;
  uint32_t at=0,count=0;
  while(at<instructions){
   uint32_t n=instructions-at>127?127:instructions-at,reg=0x8800+at;
   out[count++]=0x40000000u|n|(parity(n)<<7)|(reg<<8)|(parity(reg)<<27);
   for(uint32_t i=0;i<n;++i)out[count++]=le32(f[2]+4*(first+at+i));
   at+=n;
  }
  return true;
 }
};
struct Buf {IOBufferMemoryDescriptor *d=nullptr;uint64_t pa=0;void *va=nullptr;uint32_t bytes=0;uint64_t gva=0;};
static bool allocBuf(Buf &b,uint32_t bytes){
 uint32_t rounded=(bytes+4095)&~4095u;
 b.d=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
   kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,rounded,4096);
 if(!b.d)return false;
 IOByteCount span=0;b.pa=b.d->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
 b.va=b.d->getBytesNoCopy();b.bytes=rounded;
 if(!b.pa||(b.pa&4095)||span<rounded||!b.va||(b.pa>>36))return false;
 memset(b.va,0,rounded);flush_dcache((vm_offset_t)b.va,rounded,FALSE);return true;
}
static bool mapBuf(Buf &b,uint64_t gva,unsigned &seq){
 b.gva=gva;
 // Log before the call. The five-buffer map is now proven; keep only a short
 // hold so the start phase runs closer to the register-state snapshot.
 IOLog("CB GPU CP map begin: iova=%llx pa=%llx pages=%u seq=%u\n",gva,b.pa,b.bytes/4096,seq);
 IOSleep(2000);
 int r=cheeseburger_gpu_smmu_map(gva,b.pa,b.bytes/4096);
 IOLog("CB GPU CP map: iova=%llx pa=%llx pages=%u r=%d seq=%u\n",gva,b.pa,b.bytes/4096,r,seq);
 if(r==0)cb_cp[8]|=uint64_t(1)<<seq;
 else {cb_cp[0]=10+seq;cb_cp[30]=uint64_t(int32_t(r));}
 ++seq;return r==0;
}
// Which directories launchd and kext loading actually use on this image. The
// sheng addon mount assumes /System/Library/LaunchDaemons; the visible check
// fails here, so record existence of both the plain and the SystemOS cryptex
// paths before changing any mount target. 0 exists, positive errno, 0xdeadbeef
// not probed.
static void lookupPath(unsigned i,const char *p){
 if(i>=8)return;
 void *vp=nullptr;int e=vnode_lookup(p,0,&vp,vfs_context_current());
 cb_gpu_paths[i]=uint64_t(unsigned(e));
 if(!e&&vp)vnode_put(vp);
}
static void probePaths(){
 lookupPath(0,"/System/Library/LaunchDaemons");
 lookupPath(1,"/System/Library/LaunchDaemons/com.apple.backboardd.plist");
 lookupPath(2,"/System/Cryptexes/OS/System/Library/LaunchDaemons");
 lookupPath(3,"/System/Cryptexes/OS/System/Library/LaunchDaemons/com.apple.backboardd.plist");
 lookupPath(4,"/System/Library/Extensions");
 lookupPath(5,"/System/Cryptexes/OS/System/Library/Extensions");
 lookupPath(6,"/private/preboot");
 cb_gpu_paths[7]=0x5a5a;
}
static void reset(){for(unsigned i=0;i<32;++i)cb_cp[i]=0xdeadbeef;for(unsigned i=32;i<128;++i)cb_cp[i]=0xdeadbeef;cb_cp[31]=0x0c9;}
// The reset/configure/start state machine lives in CheeseburgerGpuStart.h so a
// host register model can inject every failure point offline.
struct Mmio {
 volatile uint32_t *v=nullptr;
 uint32_t read(uint32_t o){uint32_t x=v[o/4];__asm__ volatile("dmb sy":::"memory");return x;}
 void write(uint32_t o,uint32_t x){__asm__ volatile("dsb sy":::"memory");v[o/4]=x;__asm__ volatile("dsb sy":::"memory");}
 void writePosted(uint32_t o,uint32_t x){__asm__ volatile("dsb sy":::"memory");v[o/4]=x;}
 void delay(unsigned us){IODelay(us);}
 void publish(){__asm__ volatile("dsb oshst":::"memory");}
 void clean(volatile void *p,unsigned n){flush_dcache((vm_offset_t)p,n,FALSE);}
 void mark(const char *tag){if(&cheeseburger_bootlog_step)cheeseburger_bootlog_step(tag);}
 bool invalidateTlb(){return cheeseburger_gpu_smmu_invalidate()==0;}
};
// Android a5xx_pm_resume turns the RBCCU and SP core power domains on before
// any CP use. They are separate from the GPUCC clock vote: with them off the
// RBBM register block stays readable but the micro engine cannot run, which is
// exactly a halted CP whose ring cursor never moves. The writes are skipped
// when the domain already reports on.
static void powerCore(W &gp, volatile uint64_t *out){
 uint32_t rb=rd(gp,0xa88d*4),sp=rd(gp,0xa88b*4);
 out[0]=rd(gp,0xa887*4);out[1]=rb;out[2]=rd(gp,0xa881*4);out[3]=sp;
 if(!(rb&(1u<<20))){
  if(&cheeseburger_bootlog_step)cheeseburger_bootlog_step("power");
  __asm__ volatile("dsb sy":::"memory");gp.v[0xa887]=0x778000;__asm__ volatile("dsb sy":::"memory");
  IODelay(3);
  for(unsigned i=0;i<100&&!(rd(gp,0xa88d*4)&(1u<<20));++i)IODelay(1);
 }
 if(!(sp&(1u<<20))){
  __asm__ volatile("dsb sy":::"memory");gp.v[0xa881]=0x778000;__asm__ volatile("dsb sy":::"memory");
  for(unsigned i=0;i<100&&!(rd(gp,0xa88b*4)&(1u<<20));++i)IODelay(1);
 }
 out[4]=rd(gp,0xa88d*4);out[5]=rd(gp,0xa88b*4);
}
// The CP engine outlives the probe worker: the user client submits through it
// after the probe has returned. Everything here is created once and kept for
// the boot lifetime, like the mapped ring and fence pages.
static Mmio *gMmio=nullptr;
static CheeseburgerGpu::Startup<Mmio> *gEngine=nullptr;
// User submissions run on the sagit-style engine; the startup above keeps the
// reset/ME_INIT/GPMU/ZAP chain that the probe already proved with its fences.
static CheeseburgerGpu::Engine540<Mmio> *gEngine540=nullptr;
static bool gPreflightDone=false;
static volatile uint32_t *gRing=nullptr,*gFence=nullptr;
// The probe GPMU command page is pre-mapped and was already fetched by the CP;
// expose it as a scratch IB/target page for the user-path bring-up test.
static volatile uint32_t *gScratchVA=nullptr;
static uint64_t gScratchGva=0;
static unsigned gScratchWords=0;
// 4 KiB pages reserved at probe time for GPU buffers. The CP reaches early
// allocations (about 4.1-4.7 GB) but not pages allocated later (about 5+ GB).
static IOBufferMemoryDescriptor *gLowPage[64];
static uint64_t gLowPagePa[64];
static unsigned gLowPageCount=0;
static uint64_t gRingGva=0,gFenceGva=0;
static uint32_t gRingWords=0,gFenceSeq=0;
static IOLock *gSubmitLock=nullptr;
static void captureGpuFaults(Mmio &m){
 cb_cp[50]=m.read(CheeseburgerGpu::CPHwFault);
 cb_cp[51]=m.read(CheeseburgerGpu::CPProtectStatus);
 cb_cp[52]=m.read(CheeseburgerGpu::CPAhbFault);
}
// SMMU registers live behind their own client; keep this callable after a
// failed kick when GPU MMIO may be unsafe.
static void captureSmmuFaults(){
 W sm;if(mapw(sm,0x5040000,0x10000)){
  cb_cp[53]=rd(sm,0x8058);cb_cp[54]=rd(sm,0x48);
  // Secure stream (SID 2) and both context banks' fault registers: while
  // TrustControl=1 the CP may be attributed to the secure stream. Only these
  // registers (and CB0's own page) have been read safely on this board; the
  // global NS aliases (0x448+) and secure syndromes (0x50+) are left alone
  // until their EL1 accessibility is verified — reading them stopped a run
  // before it could return to Recovery.
  cb_cp[89]=rd(sm,0x808);cb_cp[90]=rd(sm,0xc08);
  cb_cp[91]=rd(sm,0x9058);cb_cp[92]=rd(sm,0xa058);
  cb_cp[103]=rd(sm,0x805c);cb_cp[104]=rd(sm,0x8060);
 }
}
}  // namespace
extern "C" void cheeseburger_gpu_cp_probe(void){
 reset();
 cb_cp[0]=0;
 cb_cp[1]=gpu_firmware_size;
 Firmware fw;
 if(!CB_GPU_FW||!fw.open(gpu_firmware_blob,gpu_firmware_size)){cb_cp[0]=1;return;}
 cb_cp[2]=fw.cpBytes(0);cb_cp[3]=fw.cpBytes(1);cb_cp[4]=fw.gpmuWords();
 if(!fw.cpBytes(0)||!fw.cpBytes(1)||!fw.gpmuWords()){cb_cp[0]=2;return;}
 static Buf ring,pm4,pfp,gpmu,fence;
 if(!allocBuf(ring,8192*4)){cb_cp[0]=3;return;}
 // Reserve user-buffer pages in the same early allocation window as the ring:
 // the CP fetches its ring/IB from here, while single 4 KiB allocations land
 // near 0x127e/0x1286 where an IB fetch wedges the bus. A 32 KiB contiguous
 // block is the class the ring itself uses, so take one and hand out its pages.
 { static Buf pool;
   if(allocBuf(pool,8192*4)){
    for(unsigned p=0;p<8;++p){
     gLowPage[gLowPageCount]=pool.d;gLowPagePa[gLowPageCount]=pool.pa+4096ull*p;++gLowPageCount;
    }
    pool.d->retain();
   }
   native_adreno_low_pool[0]=gLowPageCount;
   if(gLowPageCount){native_adreno_low_pool[1]=uint32_t(gLowPagePa[0]);
     native_adreno_low_pool[2]=uint32_t(gLowPagePa[0]>>32);}
   IOLog("CB GPU low pool: count=%u first=%llx\n",gLowPageCount,
     (unsigned long long)(gLowPageCount?gLowPagePa[0]:0));}
 if(!allocBuf(pm4,fw.cpBytes(0))||!allocBuf(pfp,fw.cpBytes(1))||
    !allocBuf(gpmu,fw.gpmuWords()*4)||!allocBuf(fence,4096)){cb_cp[0]=3;return;}
 if(!fw.copyCP(0,pm4.va,pm4.bytes)||!fw.copyCP(1,pfp.va,pfp.bytes)||
    !fw.gpmuCommands((uint32_t*)gpmu.va,gpmu.bytes/4)){cb_cp[0]=4;return;}
 flush_dcache((vm_offset_t)pm4.va,pm4.bytes,FALSE);
 flush_dcache((vm_offset_t)pfp.va,pfp.bytes,FALSE);
 flush_dcache((vm_offset_t)gpmu.va,gpmu.bytes,FALSE);
 cb_cp[5]=ring.bytes;cb_cp[6]=pm4.bytes;cb_cp[7]=pfp.bytes;
 cb_cp[20]=pm4.pa;cb_cp[21]=pfp.pa;cb_cp[22]=gpmu.pa;cb_cp[23]=ring.pa;cb_cp[24]=gpmu.bytes;
 unsigned seq=0;uint64_t gva=0x2000000ULL;
 // Read the CP/RBBM/VBIF/secure state before any buffer mapping. Register
 // reads are proven safe (gpu-probe alone boots to the desktop) and this state
 // is the input for the CP init sequence, so it must survive a later hang.
 W gp;if(!mapw(gp,0x5000000,0x40000)){cb_cp[0]=9;return;}
 // The 4K mapper needs the actual GPU registers and submission lock even
 // for the first cold mappings, before the CP engine is constructed.
 if(cheeseburger_gpu_smmu4k_active()){
  if(!gMmio)gMmio=new Mmio;
  if(!gSubmitLock)gSubmitLock=IOLockAlloc();
  if(!gMmio||!gSubmitLock){cb_cp[0]=9;return;}
  gMmio->v=gp.v;
 }
 cb_cp[9]=rd(gp,0x20c8);cb_cp[10]=rd(gp,0xc200);cb_cp[11]=rd(gp,0xc204);
 cb_cp[12]=rd(gp,0x2000);cb_cp[13]=rd(gp,0x2008);cb_cp[14]=rd(gp,0x2018);cb_cp[15]=rd(gp,0x201c);
 cb_cp[16]=rd(gp,0x13d4)|(uint64_t(rd(gp,0x14c0))<<32);
 cb_cp[17]=rd(gp,0x1384)|(uint64_t(rd(gp,0x2c60))<<32);
 // TrustControl is DWORD 0xf400, i.e. byte 0x3d000; the trusted TSB base is
 // DWORD 0xf800 (byte 0x3e000). CP reset needs TrustControl<=1.
 cb_cp[18]=rd(gp,0xf400*4);cb_cp[19]=rd(gp,0xf800*4);
 cb_cp[32]=rd(gp,0x2a380);cb_cp[33]=rd(gp,0x2a384); // GPMU ready/error
 cb_cp[34]=rd(gp,0x2a360);                        // GPMU_CM3_SYSRESET
 cb_cp[35]=rd(gp,0x2280);cb_cp[36]=rd(gp,0x10c);  // CP_PROTECT_CNTL, RBBM_SW_RESET_CMD
 cb_cp[37]=rd(gp,0x24c);                          // RBBM_AHB_CNTL0
 cb_cp[30]=seq;cb_cp[0]=0x10; // state captured; buffer maps still pending
 IOLog("CB GPU CP state: me=%x vbif=%x/%x rb=%x/%x/%x/%x st=%x irq=%x trust=%x sec=%x\n",
  uint32_t(cb_cp[9]),uint32_t(cb_cp[10]),uint32_t(cb_cp[11]),uint32_t(cb_cp[12]),
  uint32_t(cb_cp[13]),uint32_t(cb_cp[14]),uint32_t(cb_cp[15]),uint32_t(cb_cp[16]),
  uint32_t(cb_cp[17]),uint32_t(cb_cp[18]),uint32_t(cb_cp[19]));
 IOLog("CB GPU CP gpmu: ready=%x err=%x cm3rst=%x prot=%x swrst=%x ahb=%x\n",
  uint32_t(cb_cp[32]),uint32_t(cb_cp[33]),uint32_t(cb_cp[34]),uint32_t(cb_cp[35]),
  uint32_t(cb_cp[36]),uint32_t(cb_cp[37]));
 probePaths();
 IOSleep(8000); // let one BootLog round persist the state before any map
 // Persist each map result before the next step: the panel may black here and
 // the run is often returned manually, so a quick all-at-once map could be lost.
 if(!mapBuf(ring,gva,seq)){IOSleep(1000);return;}IOSleep(1000);gva+=ring.bytes;
 if(!mapBuf(pm4,gva,seq)){IOSleep(1000);return;}IOSleep(1000);gva+=pm4.bytes;
 if(!mapBuf(pfp,gva,seq)){IOSleep(1000);return;}IOSleep(1000);gva+=pfp.bytes;
 cb_cp[25]=ring.gva;cb_cp[26]=pm4.gva;cb_cp[27]=pfp.gva;
 cb_cp[30]=seq;
 if(!mapBuf(gpmu,gva,seq)){IOSleep(1000);return;}IOSleep(1000);gva+=gpmu.bytes;
 if(!mapBuf(fence,gva,seq)){IOSleep(1000);return;}
 cb_cp[28]=gpmu.gva;cb_cp[29]=fence.gva;
 cb_cp[30]=seq;cb_cp[0]=0;
 // Hardware walk of the installed tables before any CP write. A matching PAR
 // proves the SMMU can translate the ring/firmware; a fault localizes the
 // fetch stall to the CPU-programmed context instead of the CP itself.
 {uint64_t par=0;int ar=cheeseburger_gpu_smmu_atos(ring.gva,&par,&cb_cp[112]);
  cb_cp[109]=par;cb_cp[110]=uint64_t(int32_t(ar));cb_cp[111]=ring.pa;
  IOLog("CB GPU SMMU atos: iova=%llx par=%llx rc=%d expect=%llx ctl=%llx haltctl=%llx atsr=%llx fsr=%llx fsynr=%llx far=%llx sctlr=%llx resume=%llx\n",
   ring.gva,par,ar,ring.pa,cb_cp[112],cb_cp[113],cb_cp[114],cb_cp[115],
   cb_cp[116],cb_cp[117],cb_cp[119],cb_cp[118]);}
 IOLog("CB GPU CP: fw=%u pm4=%u pfp=%u gpmu=%u ring=%u map=%u me=%x vbif=%x/%x trust=%x\n",
  gpu_firmware_size,fw.cpBytes(0),fw.cpBytes(1),fw.gpmuWords(),ring.bytes,seq,
  uint32_t(cb_cp[9]),uint32_t(cb_cp[10]),uint32_t(cb_cp[11]),uint32_t(cb_cp[18]));
 // CP reset/configure/start is a separate opt-in: it writes GPU registers and
 // starts real firmware DMA, unlike the read-only mapping probe above.
 // 1 = full start; 2 = log-only long line (no GPU writes); 3 = log-only short
 // line. 2/3 exist solely for the controlled console A/B.
 unsigned start=0;
 if(PE_parse_boot_argn("cheeseburger-gpu-start",&start,sizeof start)&&start&&gp.v){
  if(!gMmio)gMmio=new Mmio;
  if(!gEngine&&gMmio)gEngine=new CheeseburgerGpu::Startup<Mmio>(*gMmio);
  if(!gSubmitLock)gSubmitLock=IOLockAlloc();
  if(!gMmio||!gEngine||!gSubmitLock){cb_cp[38]=13;IOSleep(8000);return;}
  gMmio->v=gp.v;
  Mmio &m=*gMmio;
  CheeseburgerGpu::Startup<Mmio> &engine=*gEngine;
  CheeseburgerGpu::Record rec;
  cb_cp[38]=1;cb_cp[39]=0;
  // Reuse the probe's GPUCC snapshot; sampling time is the GPU probe, not the
  // reset. The snapshot is recorded as-is and must not be read as a live value.
  cb_cp[56]=cb_gpu[11];cb_cp[57]=cb_gpu[19];cb_cp[58]=cb_gpu[20];
  if(start==3)
   IOLog("CB GPU start: begin ring=%llx pm4=%llx pfp=%llx\n",ring.gva,pm4.gva,pfp.gva);
  else
   IOLog("CB GPU start: begin ring=%llx pm4=%llx pfp=%llx cx=%x gx=%x gfx=%x\n",
    ring.gva,pm4.gva,pfp.gva,uint32_t(cb_cp[56]),uint32_t(cb_cp[57]),uint32_t(cb_cp[58]));
  IOSleep(8000);
  if(start>=2){cb_cp[38]=9;IOSleep(8000);return;}  // log-only: no GPU write
  // A failed micro-MMU resume leaves the translation pipeline halted; never
  // reach a CP kick in that state. The same applies when the ATOS fallback
  // could not halt or resume the micro-MMU.
  if((int64_t)cb_smmu[19]<0){cb_cp[38]=7;cb_cp[39]=uint64_t(int64_t(cb_smmu[19]));IOSleep(8000);return;}
  unsigned atosGate=1;PE_parse_boot_argn("cheeseburger-gpu-atos-gate",&atosGate,sizeof atosGate);
  if(!CheeseburgerGpu::atosMatches((int)(int64_t)cb_cp[110],cb_cp[109],ring.pa)){
   cb_cp[38]=6;cb_cp[39]=cb_cp[110] ? cb_cp[110] : uint64_t(int64_t(-6));
   IOLog("CB GPU: CP start %s by ATOS rc=%lld par=%llx expected=%llx gate=%u\n",
    atosGate?"blocked":"allowed for diagnostic",
    (int64_t)cb_cp[110],cb_cp[109],ring.pa,atosGate);
   if(atosGate){IOSleep(8000);return;}
  }
  // Android a5xx_pm_resume cores first; without them the CP cannot execute
  // even though every RBBM read succeeds. Log before the first write.
  powerCore(gp,&cb_cp[83]);
  IOLog("CB GPU power: before rbccu=%x/%x sp=%x/%x after=%x/%x\n",
   uint32_t(cb_cp[83]),uint32_t(cb_cp[84]),uint32_t(cb_cp[85]),uint32_t(cb_cp[86]),
   uint32_t(cb_cp[87]),uint32_t(cb_cp[88]));
  IOSleep(8000); // let BootLog persist the core-domain result before reset
  unsigned clearTrust=0;
  PE_parse_boot_argn("cheeseburger-gpu-trust-clear",&clearTrust,sizeof clearTrust);
  int rc=engine.reset(rec,clearTrust!=0);
  cb_cp[39]=uint64_t(int32_t(rc));
  cb_cp[40]=rec.vbif;cb_cp[41]=rec.me;
  cb_cp[48]=rec.trust;cb_cp[49]=rec.cpControl;
  cb_cp[59]=rec.status;cb_cp[60]=rec.me;cb_cp[61]=rec.vbif;cb_cp[62]=rec.retries;
  IOLog("CB GPU start: reset rc=%d me=%x vbif=%x st=%x ry=%u trust=%x cpcnl=%x\n",rc,
   uint32_t(cb_cp[60]),uint32_t(cb_cp[61]),uint32_t(cb_cp[59]),unsigned(cb_cp[62]),
   uint32_t(cb_cp[48]),uint32_t(cb_cp[49]));
  IOSleep(8000);
  if(rc==0){
   cb_cp[38]=2;
   rc=engine.start((volatile uint32_t*)ring.va,ring.bytes/4,ring.gva,pm4.gva,pfp.gva,fence.gva,rec);
   cb_cp[38]=3;cb_cp[39]=uint64_t(int32_t(rc));
   cb_cp[42]=rec.read;cb_cp[43]=rec.write;cb_cp[44]=rec.irq;cb_cp[45]=rec.cpIrq;
   cb_cp[46]=rec.status3;cb_cp[47]=rec.status;cb_cp[60]=rec.me;cb_cp[61]=rec.vbif;
   if(rc!=0){
    // A failed kick can leave the GPU/SMMU bus unsafe. The Record holds the
    // live values; read only our own scratch page (already clean+invalidated
    // at allocation, so a plain load sees DRAM) and touch no SMMU register —
    // the previous failure-path SMMU reads black-screened the device. The
    // success path still records the full SMMU fault set.
    __asm__ volatile("dsb sy":::"memory");
    cb_cp[93]=((volatile uint32_t*)fence.va)[1];
    __asm__ volatile("dmb sy":::"memory");
    IOLog("CB GPU start: kick FAILED rc=%d r=%x w=%x irq=%x cpirq=%x st=%x st3=%x me=%x vbif=%x scratch=%x\n",
     rc,uint32_t(cb_cp[42]),uint32_t(cb_cp[43]),uint32_t(cb_cp[44]),uint32_t(cb_cp[45]),
     uint32_t(cb_cp[47]),uint32_t(cb_cp[46]),uint32_t(cb_cp[60]),uint32_t(cb_cp[61]),
     uint32_t(cb_cp[93]));
    IOSleep(8000);
    return;
   }
   // sagit order: ME_INIT -> GPMU firmware upload -> ZAP. The GPMU command
   // stream is built into the gpmu buffer and must complete before the CP
   // fetches the first indirect buffer.
   bool gok=engine.gpmu(gpmu.gva,fw.gpmuWords(),800000,257000000,rec);
   cb_cp[94]=gok?1:0;
   if(!gok){
    cb_cp[38]=11;cb_cp[39]=uint64_t(int32_t(rec.rc));
    IOLog("CB GPU start: GPMU failed rc=%d; engine halted\n",rec.rc);
    IOSleep(8000);
    return;
   }
   cb_cp[38]=4;cb_cp[39]=0;
   gScratchVA=(volatile uint32_t*)gpmu.va;gScratchGva=gpmu.gva;gScratchWords=gpmu.bytes/4;
   IOLog("CB GPU start: gpmu OK ready=%x err=%x rptr=%x\n",
    uint32_t(m.read(CheeseburgerGpu::GPMUReady)),uint32_t(m.read(CheeseburgerGpu::GPMUError)),
    uint32_t(m.read(CheeseburgerGpu::RBRead)));
   // The CP engine is initialized (RBRead advanced). Both the device vendor
   // driver and sagit authenticate the ZAP after CP_ME_INIT/engine start and
   // only then submit CP_SET_SECURE_MODE; running the PAS chain before the
   // reset left the engine unable to fetch. Authentication still gates every
   // user submission: if it fails here we halt and abandon the engine.
   int zrc=cheeseburger_gpu_zap_load(gp.v);
   cb_cp[82]=uint64_t(int32_t(zrc));
   if(zrc){
    engine.abandon(rec);
    cb_cp[38]=8;cb_cp[39]=uint64_t(int32_t(zrc));
    IOLog("CB GPU start: ZAP failed after ME_INIT zrc=%d; engine halted\n",zrc);
    IOSleep(8000);
    return;
   }
   // Minimal DMA proof: the CP writes a timestamp into the fence page.
   bool fok=engine.fence(fence.gva,1,(volatile uint32_t*)fence.va,rec);
   cb_cp[95]=fok?1:0;
   flush_dcache((vm_offset_t)fence.va,4096,FALSE);
   if(!fok){
    cb_cp[38]=12;cb_cp[39]=uint64_t(int32_t(rec.rc));
    IOLog("CB GPU start: fence FAILED rc=%d; engine halted\n",rec.rc);
    IOSleep(8000);
    return;
   }
   IOLog("CB GPU start: fence OK seq=%x\n",((volatile uint32_t*)fence.va)[1]);
   // Post-probe user-path control: the exact primitive the user client uses
   // (CPU fence only, no post-doorbell MMIO) with no indirect buffer. If this
   // fence completes, the ring, doorbell and fence page still work after the
   // probe; a later user failure then belongs to the user IB, not the engine.
   {
    CheeseburgerGpu::Record ctl;
    volatile uint32_t *fp=(volatile uint32_t*)fence.va;
    int crc=engine.submit(nullptr,0,fence.gva,2,fp,ctl,ring.bytes/4,0,true);
    __asm__ volatile("dsb sy":::"memory");
    flush_dcache((vm_offset_t)fence.va,4096,FALSE);
    __asm__ volatile("dsb sy":::"memory");
    const uint32_t fraw=fp[1];
    cb_cp[127]=uint64_t(uint16_t(uint32_t(crc)))|(uint64_t(fraw)<<16)|(uint64_t(ctl.stage&0xff)<<48);
    IOLog("CB GPU start: userpath fence rc=%d stage=%u seq=2 fence=%x\n",crc,ctl.stage,fraw);
    if(crc){
     cb_cp[38]=14;cb_cp[39]=uint64_t(int32_t(crc));IOSleep(8000);return;
    }
    gFenceSeq=2;
   }
   // The engine and its pages now belong to the persistent GPU service. The
   // probe worker keeps them for the boot lifetime (statics) and never frees
   // them; submission from userland is serialized by gSubmitLock.
   gRing=(volatile uint32_t*)ring.va;gRingGva=ring.gva;gRingWords=ring.bytes/4;
   gFence=(volatile uint32_t*)fence.va;gFenceGva=fence.gva;
   // The probe's fences proved the ring, the doorbell and the +4 timestamp.
   // Hand the live cursors to the submission engine; it continues the CPU
   // sequence at the probe's last value.
   gEngine540=new CheeseburgerGpu::Engine540<Mmio>(*gMmio);
   if(!gEngine540||!gEngine540->adopt(gRing,gRingWords,gRingGva,gFence,gFenceGva,gFenceSeq)){
    IOLog("CB GPU engine: adopt failed; user submits stay refused\n");
    cb_cp[97]=1;
   }
   // Ring keep-alive at handoff: the gate's first stage (a CP_MEM_WRITE
   // submitted in the ring) re-run where the probe's own fences just finished.
   // Running it again after the service publishes attributes a ring that stops
   // accepting work to the probe, the publish path or the client's map instead
   // of assuming the last one.
   if(gEngine540&&gEngine540->ready()){
    CheeseburgerGpu::SubmitRecord ka;
    const int k0=keepAliveInline(gFenceSeq+1,ka);
    native_adreno_gate[18]=uint32_t(k0);native_adreno_gate[19]=ka.stage;
    IOLog("CB GPU engine: keepalive0 rc=%d stage=%u fr=%x\n",k0,ka.stage,ka.fence);
    if(k0==0)gFenceSeq=gFenceSeq+1;else gEngineSubmitFailed=true;
   }
   unsigned svc=0;
   if(PE_parse_boot_argn("cheeseburger-gpu-service",&svc,sizeof svc)&&svc==1){
    int prc=cheeseburger_gpu_service_publish();
    cb_cp[96]=uint64_t(int32_t(prc));
    IOLog("CB GPU service: publish rc=%d\n",prc);
   }
   {uint64_t after[6];powerCore(gp,after);cb_cp[87]=after[4];cb_cp[88]=after[5];
    IOLog("CB GPU power: after-zap rbccu=%x/%x sp=%x/%x\n",
     uint32_t(after[0]),uint32_t(after[1]),uint32_t(after[2]),uint32_t(after[3]));}
   // Second keep-alive: after the accelerator is published and Apple's client
   // stack has had its chance to touch the device, before any BO exists.
   if(gEngine540&&gEngine540->ready()&&!gEngineSubmitFailed){
    CheeseburgerGpu::SubmitRecord kb;
    const int k1=keepAliveInline(gFenceSeq+1,kb);
    native_adreno_gate[20]=uint32_t(k1);native_adreno_gate[21]=kb.stage;
    IOLog("CB GPU engine: keepalive1 rc=%d stage=%u fr=%x\n",k1,kb.stage,kb.fence);
    if(k1==0)gFenceSeq=gFenceSeq+1;else gEngineSubmitFailed=true;
   }
   cb_cp[48]=m.read(CheeseburgerGpu::TrustControl);
   cb_cp[60]=m.read(CheeseburgerGpu::MEControl);cb_cp[61]=m.read(CheeseburgerGpu::VBIFHalted);
   cb_cp[105]=m.read(CheeseburgerGpu::RBWrite);
   cb_cp[107]=m.read(CheeseburgerGpu::RBRead);
   IOLog("CB GPU start: post-auth trust=%x rptr=%x->%x wptr=%x me=%x vbif=%x\n",
    uint32_t(cb_cp[48]),uint32_t(cb_cp[42]),uint32_t(cb_cp[107]),uint32_t(cb_cp[105]),
    uint32_t(cb_cp[60]),uint32_t(cb_cp[61]));
   cb_cp[64]=m.read(CheeseburgerGpu::MEBase);cb_cp[65]=m.read(CheeseburgerGpu::MEBase+4);
   cb_cp[66]=m.read(CheeseburgerGpu::PFPBase);cb_cp[67]=m.read(CheeseburgerGpu::PFPBase+4);
   cb_cp[68]=m.read(CheeseburgerGpu::RBBase);cb_cp[69]=m.read(CheeseburgerGpu::RBBase+4);
   cb_cp[70]=m.read(CheeseburgerGpu::RBControl);
   cb_cp[71]=m.read(CheeseburgerGpu::CPAddrModeCntl);cb_cp[72]=m.read(CheeseburgerGpu::CPProtectCntl);
   W sm;if(mapw(sm,0x5040000,0x10000)){
    cb_cp[73]=rd(sm,0x8000);cb_cp[74]=rd(sm,0x8030);
    cb_cp[75]=rd(sm,0x8020);cb_cp[76]=rd(sm,0x8024);
    cb_cp[77]=rd(sm,0x800);cb_cp[78]=rd(sm,0xc00);}
   {uint64_t pte=0;if(cheeseburger_gpu_smmu_lookup(ring.gva,&pte)==0)cb_cp[79]=pte;
    if(cheeseburger_gpu_smmu_lookup(fence.gva,&pte)==0)cb_cp[108]=pte;}
   cb_cp[80]=cheeseburger_gpu_smmu_root();cb_cp[81]=ring.pa;
   cb_cp[105]=m.read(CheeseburgerGpu::RBWrite);cb_cp[106]=m.read(CheeseburgerGpu::RBRead);
   // Live post-kick state: the CP's scratch cursor, CP_CNTL and the ME ucode
   // debug word distinguish "engine halted" from "engine running, ring stuck".
   // The scratch page was zeroed by the CPU and its mapping was flushed before
   // the kick, but the GPU's DMA write goes to Normal-NC memory. Flush by VA
   // (clean+invalidate) and barrier before reading so a resident clean line
   // cannot hide the GPU's cursor; keep register RPTR separate from this value.
   __asm__ volatile("dsb sy":::"memory");
   flush_dcache((vm_offset_t)fence.va,4096,FALSE);
   __asm__ volatile("dsb sy":::"memory");
   cb_cp[93]=((volatile uint32_t*)fence.va)[1];
   __asm__ volatile("dmb sy":::"memory");
   cb_cp[94]=m.read(0x831*4);cb_cp[95]=m.read(0x830*4);
   captureGpuFaults(m);captureSmmuFaults();
   IOLog("CB GPU start: kick rc=%d r=%x w=%x irq=%x cpirq=%x st=%x st3=%x\n",rc,
    uint32_t(cb_cp[42]),uint32_t(cb_cp[43]),uint32_t(cb_cp[44]),uint32_t(cb_cp[45]),
    uint32_t(cb_cp[47]),uint32_t(cb_cp[46]));
   IOLog("CB GPU start: faults cp=%x/%x/%x smfsr=%x smg=%x\n",
    uint32_t(cb_cp[50]),uint32_t(cb_cp[51]),uint32_t(cb_cp[52]),
    uint32_t(cb_cp[53]),uint32_t(cb_cp[54]));
   IOLog("CB GPU start: sid2 smr=%x s2cr=%x cb1fsr=%x cb2fsr=%x pow=%x/%x/%x/%x\n",
    uint32_t(cb_cp[89]),uint32_t(cb_cp[90]),uint32_t(cb_cp[91]),uint32_t(cb_cp[92]),
    uint32_t(cb_cp[83]),uint32_t(cb_cp[84]),uint32_t(cb_cp[87]),uint32_t(cb_cp[88]));
   IOLog("CB GPU start: live rptr=%x cpctl=%x ucode=%x scratch=%x me=%x vbif=%x\n",
    uint32_t(cb_cp[42]),uint32_t(cb_cp[94]),uint32_t(cb_cp[95]),uint32_t(cb_cp[93]),
    uint32_t(cb_cp[60]),uint32_t(cb_cp[61]));
   IOLog("CB GPU start: faults2 nsg=%x gsyn=%x/%x/%x ssyn=%x/%x/%x cb0=%x/%x rw=%x/%x fpte=%llx\n",
    uint32_t(cb_cp[96]),uint32_t(cb_cp[97]),uint32_t(cb_cp[98]),uint32_t(cb_cp[99]),
    uint32_t(cb_cp[100]),uint32_t(cb_cp[101]),uint32_t(cb_cp[102]),
    uint32_t(cb_cp[103]),uint32_t(cb_cp[104]),uint32_t(cb_cp[106]),uint32_t(cb_cp[105]),cb_cp[108]);
  }
  IOSleep(8000);
 }
}

// Submission interface for the persistent user client. The probe worker owns
// the engine and the pages; these wrappers serialize submits and are the only
// way a user request reaches the CP. Each submit waits for its own fence, so
// the ring is idle when it wraps and a later BO cannot be freed under an
// in-flight fetch.
// Internal result of the last failed user submission. The persistent log and
// the heartbeat carry these so a wedged msgbuf does not erase them.
volatile int32_t native_adreno_submit_rc=-1;
volatile uint32_t native_adreno_submit_stage=0,native_adreno_submit_irq=0,
 native_adreno_submit_cpirq=0,native_adreno_submit_seq=0,native_adreno_submit_post=0,
 native_adreno_submit_preirq=0,native_adreno_submit_precpirq=0;
volatile uint32_t native_adreno_submit_fenced=0;
// CPU-owned word the ring writes before the user IB. pf=0xdeadbeef means the
// probe was not present; a 0x1000|seq value with rc=-4 means the ring, the
// prefence barrier and the doorbell ran and the stall is after the prefence.
volatile uint32_t native_adreno_submit_prefence=0, native_adreno_submit_prefence_valid=0,
 native_adreno_submit_fence_raw=0;
// SMMU hardware walk of the first user IB before its doorbell: rc plus the raw
// PAR and the software leaf's expected page, so a fetch failure can be placed
// on the translation instead of being inferred from a silent timeout.
volatile uint32_t native_adreno_submit_atos_rc=0;
volatile uint64_t native_adreno_submit_atos_par=0, native_adreno_submit_atos_expect=0;
// Raw leaf PTE of the submitted IB, for an attribute comparison with a page
// that is known good.
volatile uint64_t native_adreno_submit_atos_pte=0;
// 1 when buffer 0 was replaced by its copy in the pre-start mapped window, so
// a failure cannot be blamed on the runtime user IOVA.
volatile uint32_t native_adreno_submit_staged=0;
volatile uint64_t native_adreno_submit_stage_gva=0;
// Pre-kick core power and CP liveness. Android's a5xx_pm_resume re-enables the
// RBCCU/SP domains before every CP use; with them collapsed every RBBM read
// still succeeds but the CP cannot execute. Sample before a user doorbell so a
// stopped engine is distinguishable from a ring that was never consumed.
volatile uint32_t native_adreno_pwr[14];
extern "C" int cheeseburger_gpu_pre_kick(void){
 if(!gMmio||!gMmio->v)return -1;
 W gp;gp.v=gMmio->v;
 uint64_t out[6]={0};powerCore(gp,out);
 native_adreno_pwr[0]=uint32_t(out[0]);native_adreno_pwr[1]=uint32_t(out[1]);
 native_adreno_pwr[2]=uint32_t(out[2]);native_adreno_pwr[3]=uint32_t(out[3]);
 native_adreno_pwr[4]=uint32_t(out[4]);native_adreno_pwr[5]=uint32_t(out[5]);
 native_adreno_pwr[6]=gMmio->read(CheeseburgerGpu::MEControl)|(gMmio->read(CheeseburgerGpu::VBIFHalted)<<16);
 native_adreno_pwr[7]=gMmio->read(CheeseburgerGpu::Status);
 native_adreno_pwr[8]=uint32_t(cheeseburger_gpu_smmu_micro());
 native_adreno_pwr[9]=uint32_t(cheeseburger_gpu_smmu_resume_micro());
 native_adreno_pwr[10]=gMmio->read(CheeseburgerGpu::GPMUReady);
 native_adreno_pwr[11]=gMmio->read(CheeseburgerGpu::GPMUError);
 native_adreno_pwr[12]=gMmio->read(CheeseburgerGpu::CPInterruptStatus);
 native_adreno_pwr[13]=gMmio->read(CheeseburgerGpu::InterruptStatus);
 // Do not ring a CP with a collapsed core or a halted/unreadable micro-MMU.
 if(out[4]==0xffffffffu||out[5]==0xffffffffu||
    !(out[4]&(1u<<20))||!(out[5]&(1u<<20)))return -2;
 if((int32_t)native_adreno_pwr[9]<0)return -3;
 return 0;
}
extern "C" int cheeseburger_gpu_engine_failed(void){return (gEngineSubmitFailed||cheeseburger_gpu_smmu4k_faulted())?1:0;}
extern "C" int cheeseburger_gpu_engine_scratch(uint64_t *gva,volatile uint32_t **va,unsigned *words){
 if(!gScratchVA||!gScratchGva||!gScratchWords)return -1;
 if(gva)*gva=gScratchGva;if(va)*va=gScratchVA;if(words)*words=gScratchWords;return 0;
}
extern "C" int cheeseburger_gpu_engine_copy_test(uint64_t srcIova){
 if(!gEngine||!gScratchVA||!gRing||!gFence||!gSubmitLock)return -1;
 if(gEngineSubmitFailed||cheeseburger_gpu_smmu4k_faulted())return -2;
 // One CP_MEM_TO_MEM (0x73) word: read the address the user IB writes and
 // store it in the proven GPMU page. A successful copy proves the CP can read
 // the user page; the user command stream is untouched and no user page is
 // written.
 volatile uint32_t *ib=gScratchVA+0x80;      // byte 0x200
 volatile uint32_t *dst=gScratchVA+0x100;    // byte 0x400
 const uint64_t dstGva=gScratchGva+0x400;
 dst[0]=0;dst[1]=0;
 ib[0]=CheeseburgerGpu::packet7(0x73,5);ib[1]=1u<<30;
 ib[2]=uint32_t(dstGva);ib[3]=uint32_t(dstGva>>32);
 ib[4]=uint32_t(srcIova);ib[5]=uint32_t(srcIova>>32);
 flush_dcache((vm_offset_t)ib,64,FALSE);
 flush_dcache((vm_offset_t)dst,64,FALSE);
 CheeseburgerGpu::Startup<Mmio>::Ib one{gScratchGva+0x200,6};
 CheeseburgerGpu::Record rec;
 IOLockLock(gSubmitLock);
 uint32_t s=gFenceSeq+1;if(!s)s=1;
 int rc=gEngine->submit(&one,1,gFenceGva,s,gFence,rec,gRingWords,0,true);
 if(rc==0)gFenceSeq=s;else gEngineSubmitFailed=true;
 IOLockUnlock(gSubmitLock);
 __asm__ volatile("dsb sy":::"memory");
 flush_dcache((vm_offset_t)dst,64,FALSE);
 __asm__ volatile("dsb sy":::"memory");
 native_adreno_copy_test[0]=uint32_t(rc);native_adreno_copy_test[1]=uint32_t(s);
 native_adreno_copy_test[2]=dst[0];native_adreno_copy_test[3]=dst[1];
 native_adreno_copy_test[4]=uint32_t(srcIova);
 IOLog("CB GPU engine: copy test rc=%d seq=%u src=%llx dst=%x/%x\n",
  rc,s,srcIova,dst[0],dst[1]);
 return rc;
}
// Late-allocation control: the same allocation call and the same moment as a
// user BO, but with no user mapping. It separates "pages allocated late are
// unreachable" from "the user mapping is what breaks the write".
extern "C" int cheeseburger_gpu_engine_late_test(void){
 if(!gEngine||!gScratchVA||!gRing||!gFence||!gSubmitLock)return -1;
 if(gEngineSubmitFailed||cheeseburger_gpu_smmu4k_faulted())return -2;
 IOBufferMemoryDescriptor *b=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
  kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,4096,4096);
 if(!b)return -3;
 IOByteCount span=0;uint64_t pa=b->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
 void *va=b->getBytesNoCopy();
 if(!pa||!va||span<4096){b->release();return -3;}
 const uint64_t gva=0x2019000ULL;
 int m=cheeseburger_gpu_smmu_map(gva,pa,1);
 volatile uint32_t *t=(volatile uint32_t*)va;
 t[0]=0;t[1]=0;t[2]=0;
 flush_dcache((vm_offset_t)va,64,FALSE);
 volatile uint32_t *ib=gScratchVA+0x80;    // byte 0x200
 ib[0]=CheeseburgerGpu::packet7(0x3d,5);ib[1]=uint32_t(gva);ib[2]=uint32_t(gva>>32);
 ib[3]=0x5e5e0001u;ib[4]=0x5e5e0002u;ib[5]=0x5e5e0003u;
 flush_dcache((vm_offset_t)ib,64,FALSE);
 CheeseburgerGpu::Startup<Mmio>::Ib one{gScratchGva+0x200,6};
 CheeseburgerGpu::Record rec;
 IOLockLock(gSubmitLock);
 uint32_t s=gFenceSeq+1;if(!s)s=1;
 int rc=m?m:gEngine->submit(&one,1,gFenceGva,s,gFence,rec,gRingWords,0,true);
 if(rc==0)gFenceSeq=s;else gEngineSubmitFailed=true;
 IOLockUnlock(gSubmitLock);
 __asm__ volatile("dsb sy":::"memory");
 flush_dcache((vm_offset_t)va,64,FALSE);
 __asm__ volatile("dsb sy":::"memory");
 native_adreno_late_test[0]=uint32_t(rc);native_adreno_late_test[1]=uint32_t(pa);
 native_adreno_late_test[2]=uint32_t(pa>>32);native_adreno_late_test[3]=t[0];
 native_adreno_late_test[4]=t[1];native_adreno_late_test[5]=t[2];
 native_adreno_late_test[6]=uint32_t(m);native_adreno_late_test[7]=s;
 IOLog("CB GPU engine: late test rc=%d pa=%llx map=%d tgt=%x/%x/%x seq=%u\n",
  rc,pa,m,t[0],t[1],t[2],s);
 // Never unmap or free after a failed kick: the CP may still be writing this
 // page. The buffer and its mapping stay owned for the rest of the boot.
 if(rc==0){cheeseburger_gpu_smmu_unmap(gva,1);b->release();}
 return rc;
}
extern "C" int cheeseburger_gpu_low_take(uint64_t *pa,IOBufferMemoryDescriptor **buf){
 if(!pa||!buf||!gLowPageCount)return -1;
 --gLowPageCount;*pa=gLowPagePa[gLowPageCount];*buf=gLowPage[gLowPageCount];
 return 0;
}
// Staged gate before the first client IB, in the order sheng's probe proves
// the path (GpuPowerProbe step 7: the first real command is a CP_MEM_WRITE in
// the ring, then an indirect buffer, then a CP-side read):
//   A  ring-inline CP_MEM_WRITE of three sentinels into the GPMU page
//   B  the same write from inside a kernel-owned indirect buffer
//   C  four CP_MEM_TO_MEM loads of the client's first IB page
// Every stage carries its own fence and is refused unless the CP both wrote
// its sentinels and advanced the timestamp. Stage A failing means the ring or
// doorbell stopped after the mapping; B means indirect-buffer fetch; C means
// the client page. Nothing here writes user memory, and no client IB is rung
// until all three pass. After a failure only the fence page and the CPU-owned
// scratch page are read: no GPU register is sampled (boot 0x47806558 hung
// there).
// The gate's first stage used as a handoff keep-alive: one CP_MEM_WRITE
// submitted in the ring, no indirect buffer, its own fence and its own target
// so a later run cannot be read as an earlier one's result.
static int keepAliveInline(uint32_t sequence,CheeseburgerGpu::SubmitRecord &rec){
 if(!gScratchVA||!gScratchGva||!gEngine540)return -1;
 volatile uint32_t *t=gScratchVA+0x1f0;      // byte 0x7c0
 const uint32_t sent[2]={0xc0de0001u,0xc0de0002u};
 uint32_t words[16];
 CheeseburgerGpu::Encoder540 e(words,16);
 if(!e.memWrite(gScratchGva+0x7c0,sent,2))return -1;
 t[0]=0;t[1]=0;
 flush_dcache((vm_offset_t)t,8,FALSE);
 int rc=gEngine540->submit(nullptr,0,sequence,1000000,rec,0,nullptr,words,e.size());
 __asm__ volatile("dsb sy":::"memory");
 flush_dcache((vm_offset_t)t,8,FALSE);
 __asm__ volatile("dsb sy":::"memory");
 if(rc)return rc;
 return (t[0]==sent[0]&&t[1]==sent[1])?0:-2;
}
static int gateSubmit(uint32_t sequence,bool useIb,uint32_t ibDwords,
 const uint32_t *inlineWords,uint32_t inlineCount,CheeseburgerGpu::SubmitRecord &rec){
 CheeseburgerGpu::Ib540 one={gScratchGva+0x200,ibDwords};
 if(useIb)return gEngine540->submit(&one,1,sequence,1000000,rec);
 return gEngine540->submit(nullptr,0,sequence,1000000,rec,0,nullptr,inlineWords,inlineCount);
}
static int preflightUserPage(uint64_t userIova,uint32_t sequence,CheeseburgerGpu::SubmitRecord &rec){
 if(!gScratchVA||!gScratchGva||!gEngine540||!userIova)return -1;
 volatile uint32_t *ib=gScratchVA+0x80;      // byte 0x200, the gate's IB
 volatile uint32_t *sa=gScratchVA+0x1c0;     // byte 0x700, stage A target
 volatile uint32_t *sb=gScratchVA+0x1d0;     // byte 0x740, stage B target
 volatile uint32_t *sc=gScratchVA+0x1e0;     // byte 0x780, stage C target
 const uint32_t sentA[3]={0xa5a50001u,0xa5a50002u,0xa5a50003u};
 const uint32_t sentB[3]={0xb6b60001u,0xb6b60002u,0xb6b60003u};
 uint32_t inlineWords[16];
 int rca=-1,rcb=-1,rcc=-1;
 for(unsigned i=0;i<3;++i)sa[i]=0;
 for(unsigned i=0;i<3;++i)sb[i]=0;
 for(unsigned i=0;i<4;++i)sc[i]=0;
 flush_dcache((vm_offset_t)sa,64,FALSE);
 flush_dcache((vm_offset_t)sb,64,FALSE);
 flush_dcache((vm_offset_t)sc,64,FALSE);
 // A: the ring itself, no indirect buffer.
 { CheeseburgerGpu::Encoder540 e(inlineWords,16);
   if(!e.memWrite(gScratchGva+0x700,sentA,3))return -1;
   rca=gateSubmit(sequence,false,0,inlineWords,e.size(),rec);
   __asm__ volatile("dsb sy":::"memory");
   flush_dcache((vm_offset_t)sa,16,FALSE);
   __asm__ volatile("dsb sy":::"memory");
   native_adreno_gate[0]=uint32_t(rca);native_adreno_gate[1]=rec.stage;
   for(unsigned i=0;i<3;++i)native_adreno_gate[2+i]=sa[i];
   IOLog("CB GPU engine: gate A rc=%d stage=%u sent=%x/%x/%x fr=%x\n",
    rca,rec.stage,sa[0],sa[1],sa[2],rec.fence);
   if(rca||sa[0]!=sentA[0]||sa[1]!=sentA[1]||sa[2]!=sentA[2])return -2; }
 gFenceSeq=sequence++;
 // B: the same command fetched through CP_INDIRECT_BUFFER.
 { CheeseburgerGpu::Encoder540 e((uint32_t*)ib,64);
   if(!e.memWrite(gScratchGva+0x740,sentB,3))return -1;
   flush_dcache((vm_offset_t)ib,256,FALSE);
   rcb=gateSubmit(sequence,true,e.size(),nullptr,0,rec);
   __asm__ volatile("dsb sy":::"memory");
   flush_dcache((vm_offset_t)sb,16,FALSE);
   __asm__ volatile("dsb sy":::"memory");
   native_adreno_gate[5]=uint32_t(rcb);native_adreno_gate[6]=rec.stage;
   for(unsigned i=0;i<3;++i)native_adreno_gate[7+i]=sb[i];
   IOLog("CB GPU engine: gate B rc=%d stage=%u sent=%x/%x/%x fr=%x\n",
    rcb,rec.stage,sb[0],sb[1],sb[2],rec.fence);
   if(rcb||sb[0]!=sentB[0]||sb[1]!=sentB[1]||sb[2]!=sentB[2])return -3; }
 gFenceSeq=sequence++;
 // C: the CP reads the client's page. A translated page reads back the same
 // words the kernel maps; any other value is the page or its translation.
 { CheeseburgerGpu::Encoder540 e((uint32_t*)ib,64);
   bool ok=true;
   for(unsigned i=0;i<4&&ok;++i)
    ok=e.memToMem(gScratchGva+0x780+4ull*i,userIova+4ull*i);
   if(!ok)return -1;
   flush_dcache((vm_offset_t)ib,256,FALSE);
   rcc=gateSubmit(sequence,true,e.size(),nullptr,0,rec);
   __asm__ volatile("dsb sy":::"memory");
   flush_dcache((vm_offset_t)sc,32,FALSE);
   __asm__ volatile("dsb sy":::"memory");
   native_adreno_gate[10]=uint32_t(rcc);native_adreno_gate[11]=rec.stage;
   for(unsigned i=0;i<4;++i)native_adreno_gate[12+i]=sc[i];
   native_adreno_gate[16]=cb_inline_ib?((volatile uint32_t*)cb_inline_ib)[0]:0xdeadbeefu;
   IOLog("CB GPU engine: gate C rc=%d stage=%u cp=%x/%x kview=%x fr=%x\n",
    rcc,rec.stage,sc[0],sc[1],native_adreno_gate[16],rec.fence);
   if(rcc)return -4; }
 gFenceSeq=sequence;
 return 0;
}
extern "C" int cheeseburger_gpu_engine_submit(const uint64_t *vas,const uint32_t *dwords,
 unsigned count,uint32_t *seq){
 if(!gEngine||!gRing||!gFence||!gFenceGva||!gSubmitLock||!seq||count>16||(count&&(!vas||!dwords)))return -1;
 CheeseburgerGpu::Ib540 ibs[16];
 for(unsigned i=0;i<count;++i)ibs[i]={vas[i],dwords[i]};
 IOLockLock(gSubmitLock);
 // A submission that failed may have left the CP fetching a bad IB. Fail
 // closed until reboot; the engine refuses as well, so neither the ring nor a
 // mapping is touched after an unproven stream.
 if(gEngineSubmitFailed||cheeseburger_gpu_smmu4k_faulted()||!gEngine540||!gEngine540->ready()){
  IOLockUnlock(gSubmitLock);return -2;}
 // Re-assert the core power domains and sample the engine before the doorbell.
 // A collapsed domain or a halted CP explains a ring the CP never reads; the
 // reads happen while the bus is still healthy (no failed kick yet).
 int pwr=cheeseburger_gpu_pre_kick();
 IOLog("CB GPU engine: pre pwr=%d rb=%x/%x sp=%x/%x>%x/%x me/vbif=%x st=%x micro=%x/%d\n",pwr,
  native_adreno_pwr[0],native_adreno_pwr[1],native_adreno_pwr[2],native_adreno_pwr[3],
  native_adreno_pwr[4],native_adreno_pwr[5],native_adreno_pwr[6],native_adreno_pwr[7],
  native_adreno_pwr[8],(int)native_adreno_pwr[9]);
 if(pwr){
  gEngineSubmitFailed=true;gEngine540->quarantine();native_adreno_submit_rc=pwr;
  native_adreno_submit_stage=0x80;
  IOLockUnlock(gSubmitLock);return pwr;
 }
 uint32_t s=gFenceSeq+1;if(!s)s=1;
 // Software translation gate for every submitted IB. The pinned ATOS helper
 // rewrites the live CB0, so when the kit owns the context its LiveUpdate is
 // the only authority; a leaf missing from the kit's ledger is refused here.
 int atosRc=0;uint64_t atosPar=0,atosExpect=0,atosPte=0;
 const bool kitPath=cheeseburger_gpu_smmu4k_active()!=0;
 for(unsigned i=0;i<count&&!atosRc;++i){
  uint64_t pte=0;
  if(cheeseburger_gpu_smmu_lookup(vas[i],&pte)){atosRc=-1;break;}
  atosPte=pte;atosExpect=pte&0x0000fffffffff000ull;
  if(kitPath)continue;
  int arc=cheeseburger_gpu_smmu_atos(vas[i],&atosPar,nullptr);
  if(!CheeseburgerGpu::atosMatches(arc,atosPar,atosExpect))atosRc=arc?arc:-3;
 }
 native_adreno_submit_atos_rc=uint32_t(atosRc);
 native_adreno_submit_atos_par=atosPar;native_adreno_submit_atos_expect=atosExpect;
 native_adreno_submit_atos_pte=atosPte;
 if(count)IOLog("CB GPU engine: atos rc=%d par=%llx expect=%llx pte=%llx\n",atosRc,atosPar,atosExpect,atosPte);
 if(atosRc){
  native_adreno_submit_rc=atosRc;native_adreno_submit_stage=0x81;native_adreno_submit_seq=s;
  IOLockUnlock(gSubmitLock);return atosRc;
 }
 // One trusted stream per boot before the client's own commands: it proves the
 // CP still executes an IB and can reach the client's page. A failure keeps
 // every object and refuses the client submit.
 if(!gPreflightDone){
  CheeseburgerGpu::SubmitRecord pre;
  int prc=count?preflightUserPage(vas[0],s,pre):-1;
  gPreflightDone=true;
  if(prc){
   gEngineSubmitFailed=true;gEngine540->quarantine();
   native_adreno_submit_rc=prc;native_adreno_submit_stage=pre.stage?pre.stage:0x90;
   native_adreno_submit_seq=s;native_adreno_submit_fence_raw=pre.fence;
   native_adreno_submit_prefence=pre.rptr;
   IOLockUnlock(gSubmitLock);return prc;
  }
  s=gFenceSeq+1;
 }
 // sheng invalidates the context bank's TLB before every submission
 // (NativeAdrenoVm::invalidateTlb). The engine port dropped that step; it is
 // restored here, before the gate and the client IBs, and refused if it fails.
 if(cheeseburger_gpu_smmu_invalidate()){
  gEngineSubmitFailed=true;gEngine540->quarantine();
  native_adreno_submit_rc=-8;native_adreno_submit_stage=0x83;native_adreno_submit_seq=s;
  IOLockUnlock(gSubmitLock);return -8;
 }
 CheeseburgerGpu::SubmitRecord rec;
 // The probe's streams fetch from 0x2000000 without any CP table packet, so
 // the table CB0 already holds is the one the CP walks. sagit's per-job
 // 0x53/0x43 transition is available in the encoder but stays off here: it
 // changed nothing on boot 0x4670c72a.
 int rc=gEngine540->submit(ibs,count,s,1000000,rec);
 if(rc==0){gFenceSeq=s;*seq=s;}
 else {
  gEngineSubmitFailed=true;
  native_adreno_submit_rc=rc;native_adreno_submit_stage=rec.stage;
  // No post-doorbell MMIO read happened: the IRQ fields are unknown, not zero.
  native_adreno_submit_irq=0xffffffffu;native_adreno_submit_cpirq=0xffffffffu;
  native_adreno_submit_seq=s;native_adreno_submit_post=0;
  native_adreno_submit_preirq=0;native_adreno_submit_precpirq=0;
  native_adreno_submit_fenced=rec.fenced?1:0;
  native_adreno_submit_prefence=rec.rptr;
  native_adreno_submit_prefence_valid=0;
  native_adreno_submit_fence_raw=rec.fence;
  static unsigned logged;
  if(logged++<4)IOLog("CB GPU engine: submit rc=%d stage=%u seq=%u rptr=%x wptr=%x fenced=%u fr=%x ring=%u\n",
    rc,rec.stage,s,rec.rptr,rec.wptr,(unsigned)rec.fenced,rec.fence,rec.words);
 }
 IOLockUnlock(gSubmitLock);
 return rc;
}
extern "C" int cheeseburger_gpu_engine_wait(uint32_t seq,uint64_t timeout_ns){
 if(!gFence||!seq||cheeseburger_gpu_smmu4k_faulted())return -1;
 auto read=[](){
  __asm__ volatile("dsb sy":::"memory");
  flush_dcache((vm_offset_t)gFence,8,FALSE);
  __asm__ volatile("dsb sy":::"memory");
  // The timestamp fence is at +4; +0 is the CP's RPTR shadow.
  return gFence[1];
 };
 // One IODelay(50) represents 50000 ns; the helper owns the conversion.
 auto delay=[](){ IODelay(50); };
 return CheeseburgerGpu::waitFenceValue(read,delay,seq,timeout_ns,50000ull);
}
// Idle gate for buffer teardown: only a running engine that has consumed the
// whole ring and reports the reset idle status may release a mapped BO.
extern "C" int cheeseburger_gpu_engine_idle(void){
 if(!gEngine||!gMmio||!gSubmitLock)return -1;
 // After a failed submission the CP may be fetching a bad IB and the GPU AHB
 // can be stalled; a register read here can hang the caller. Report unknown so
 // the BO policy keeps every mapping instead of touching that bus.
 if(gEngineSubmitFailed||cheeseburger_gpu_smmu4k_faulted())return -1;
 // 1 only after reset read back both stop controls halted. An abandoned or
 // not-started engine without that readback is unknown, not stopped.
 const bool confirmed=gEngine->haltConfirmed();
 if(gEngine->abandoned())return confirmed?1:-1;
 if(!gEngine->running())return confirmed?1:-1;
 IOLockLock(gSubmitLock);
 uint32_t r=gMmio->read(CheeseburgerGpu::RBRead),w=gMmio->read(CheeseburgerGpu::RBWrite);
 uint32_t st=gMmio->read(CheeseburgerGpu::Status),st3=gMmio->read(CheeseburgerGpu::Status3);
 IOLockUnlock(gSubmitLock);
 if(r==0xffffffffu||w==0xffffffffu||st==0xffffffffu||st3==0xffffffffu)return -1;
 return (r==w && CheeseburgerGpu::idleStatus(st) && !(st3&(1u<<24)))?0:-2;
}
// Hold the submission lock throughout an SMMU update. Before the engine is
// constructed the probe owns the (halted) GPU exclusively; afterwards the
// observed idle state and the update must be protected by the same lock as a
// doorbell, not merely checked before taking it.
static CheeseburgerGpu::DrainState gTableDrain;
extern "C" int cheeseburger_gpu_mapping_begin(void){
 if(!gSubmitLock||!gMmio||!gMmio->v)return -1;
 IOLockLock(gSubmitLock);
 if(gEngineSubmitFailed||cheeseburger_gpu_smmu4k_faulted()||(gEngine&&gEngine->abandoned())){
  IOLockUnlock(gSubmitLock);return -1;
 }
 const bool running=gEngine&&gEngine->running();
 if(gEngine&&!running&&!gEngine->haltConfirmed()){
  IOLockUnlock(gSubmitLock);return -1;
 }
 gTableDrain={};
 // sheng updates the stream tables with the masters quiesced; an empty ring is
 // exactly that state (no fetch in flight, the kit syncs TLBs per update). The
 // CP/VBIF stop is only taken when the ring still carries work.
 if(gEngine){
  const uint32_t r=gMmio->read(CheeseburgerGpu::RBRead);
  const uint32_t w=gMmio->read(CheeseburgerGpu::RBWrite);
  const bool empty=(r==w)&&r!=0xffffffffu;
  if(!empty&&!CheeseburgerGpu::drainForTables(*gMmio,running,gTableDrain)){
   gEngineSubmitFailed=true;IOLockUnlock(gSubmitLock);return -1;
  }
 }
 return 1;
}
extern "C" int cheeseburger_gpu_mapping_end(int token,int success){
 if(token!=1)return -1;
 bool ok=success&&!cheeseburger_gpu_smmu4k_faulted();
 if(ok)ok=CheeseburgerGpu::resumeAfterTables(*gMmio,gTableDrain);
 if(!ok)gEngineSubmitFailed=true;
 IOLockUnlock(gSubmitLock);return ok?0:-1;
}
// Address window the user client may map BOs into. 0x2020000 (beside the
// probe's fetched pages) panicked the kernel on the first user submission, so
// it is not used. Keep the original window until the CP-visible aperture is
// understood from the panic. One 16 MiB window keeps the 4 KiB leaf tables
// inside the preallocated cap.
extern "C" int cheeseburger_gpu_engine_window(uint64_t *start,uint64_t *bytes){
 if(!start||!bytes||!gEngine||!gRing)return -1;
 *start=0x4000000ULL;*bytes=0x1000000ULL;return 0;
}
