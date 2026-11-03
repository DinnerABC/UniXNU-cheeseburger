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
int vnode_lookup(const char *,int,void **,void *);
void vnode_put(void *);
void *vfs_context_current(void);
int cheeseburger_gpu_smmu_lookup(uint64_t,uint64_t *);
uint64_t cheeseburger_gpu_smmu_root(void);
int cheeseburger_gpu_smmu_atos(uint64_t,uint64_t *,volatile uint64_t *);
int cheeseburger_gpu_smmu_invalidate(void);
int cheeseburger_gpu_smmu_micro(void);
int cheeseburger_gpu_smmu_resume_micro(void);
int cheeseburger_gpu_zap_load(volatile uint32_t *);
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
static volatile uint32_t *gRing=nullptr,*gFence=nullptr;
// The probe GPMU command page is pre-mapped and was already fetched by the CP;
// expose it as a scratch IB/target page for the user-path bring-up test.
static volatile uint32_t *gScratchVA=nullptr;
static uint64_t gScratchGva=0;
static unsigned gScratchWords=0;
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
 if(!allocBuf(ring,8192*4)||!allocBuf(pm4,fw.cpBytes(0))||!allocBuf(pfp,fw.cpBytes(1))||
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
    cb_cp[93]=*(volatile uint32_t*)fence.va;
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
   IOLog("CB GPU start: fence OK seq=%x\n",*(volatile uint32_t*)fence.va);
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
    const uint32_t fraw=*fp;
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
   unsigned svc=0;
   if(PE_parse_boot_argn("cheeseburger-gpu-service",&svc,sizeof svc)&&svc==1){
    int prc=cheeseburger_gpu_service_publish();
    cb_cp[96]=uint64_t(int32_t(prc));
    IOLog("CB GPU service: publish rc=%d\n",prc);
   }
   {uint64_t after[6];powerCore(gp,after);cb_cp[87]=after[4];cb_cp[88]=after[5];
    IOLog("CB GPU power: after-zap rbccu=%x/%x sp=%x/%x\n",
     uint32_t(after[0]),uint32_t(after[1]),uint32_t(after[2]),uint32_t(after[3]));}
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
   cb_cp[93]=*(volatile uint32_t*)fence.va;
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
static bool gEngineSubmitFailed=false;
static unsigned gEngineSubmitCount=0;
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
extern "C" int cheeseburger_gpu_engine_failed(void){return gEngineSubmitFailed?1:0;}
extern "C" int cheeseburger_gpu_engine_scratch(uint64_t *gva,volatile uint32_t **va,unsigned *words){
 if(!gScratchVA||!gScratchGva||!gScratchWords)return -1;
 if(gva)*gva=gScratchGva;if(va)*va=gScratchVA;if(words)*words=gScratchWords;return 0;
}
extern "C" int cheeseburger_gpu_engine_submit(const uint64_t *vas,const uint32_t *dwords,
 unsigned count,uint32_t *seq){
 if(!gEngine||!gRing||!gFence||!gFenceGva||!gSubmitLock||!seq||count>16||(count&&(!vas||!dwords)))return -1;
 // A submission that failed may have left the CP fetching a bad IB. Fail closed:
 // no further submission until reboot (the failure record names rc/stage).
 CheeseburgerGpu::Startup<Mmio>::Ib ibs[16];
 for(unsigned i=0;i<count;++i)ibs[i]={vas[i],dwords[i]};
 IOLockLock(gSubmitLock);
 if(gEngineSubmitFailed||gEngineSubmitCount>=1){IOLockUnlock(gSubmitLock);return -2;}
 gEngineSubmitCount++;
 // Re-assert the core power domains and sample the engine before the doorbell.
 // A collapsed domain or a halted CP explains a ring the CP never reads; the
 // reads happen while the bus is still healthy (no failed kick yet).
 int pwr=cheeseburger_gpu_pre_kick();
 IOLog("CB GPU engine: pre pwr=%d rb=%x/%x sp=%x/%x>%x/%x me/vbif=%x st=%x micro=%x/%d\n",pwr,
  native_adreno_pwr[0],native_adreno_pwr[1],native_adreno_pwr[2],native_adreno_pwr[3],
  native_adreno_pwr[4],native_adreno_pwr[5],native_adreno_pwr[6],native_adreno_pwr[7],
  native_adreno_pwr[8],(int)native_adreno_pwr[9]);
 if(pwr){
  gEngineSubmitFailed=true;native_adreno_submit_rc=pwr;
  native_adreno_submit_stage=0x80;
  IOLockUnlock(gSubmitLock);return pwr;
 }
 uint32_t s=gFenceSeq+1;if(!s)s=1;
 // Hardware translation gate for every IB. sheng invalidates the CB TLB per
 // submission; a leaf the SMMU walk cannot resolve stalls the CP fetch with no
 // CPU-visible record, and this board must not read GPU MMIO after a failed
 // kick. Compare the raw PAR with the software leaf and fail closed before the
 // doorbell instead of ringing an unfetchable IB.
 int atosRc=0;uint64_t atosPar=0,atosExpect=0,atosPte=0;
 for(unsigned i=0;i<count&&!atosRc;++i){
  uint64_t pte=0;
  if(cheeseburger_gpu_smmu_lookup(vas[i],&pte)){atosRc=-1;break;}
  atosPte=pte;atosExpect=pte&0x0000fffffffff000ull;
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
 CheeseburgerGpu::Record rec;
 // The probe's own streams fetch from 0x2000000 without any CP_SMMU_TABLE_UPDATE,
 // so the SMMU CB0 TTBR0 installed by the kernel is the translation the CP uses.
 // The sagit pageTable sequence (0x53/0x43 under a protected-mode toggle) is a
 // per-job context switch; here it stalled the CP before the pre-IB fence ever
 // executed (boot 0x6330fe6c, pf=0/1). sheng instead invalidates the CB TLB in
 // the SMMU (top of submit) and sends no page-table packets per submission.
 int rc=gEngine->submit(ibs,count,gFenceGva,s,gFence,rec,gRingWords,0,true);
 if(rc==0){gFenceSeq=s;*seq=s;}
 else {
  gEngineSubmitFailed=true;
  native_adreno_submit_rc=rc;native_adreno_submit_stage=rec.stage;
  native_adreno_submit_irq=rec.irq;native_adreno_submit_cpirq=rec.cpIrq;
  native_adreno_submit_seq=s;native_adreno_submit_post=rec.postRead?1:0;
  native_adreno_submit_preirq=rec.preIrq;native_adreno_submit_precpirq=rec.preCpIrq;
  native_adreno_submit_fenced=rec.fenced?1:0;
  native_adreno_submit_prefence=rec.prefence;
  native_adreno_submit_prefence_valid=rec.prefenceValid?1:0;
  native_adreno_submit_fence_raw=rec.fenceRaw;
  // Bounded: a failing submission must say why. Unknown registers are
  // 0xffffffff and postRead=0 means they were never sampled after failure.
  // pre= are the latches sampled before the doorbell (stale bits), fenced=1
  // means the CPU-visible fence page held the sequence despite the rc.
  static unsigned logged;
  if(logged++<4)IOLog("CB GPU engine: submit rc=%d stage=%u seq=%u rptr=%x wptr=%x irq=%x cpirq=%x pre=%x/%x fenced=%u post=%u pf=%x/%u fr=%x stg=%u\n",
    rc,rec.stage,s,rec.read,rec.write,rec.irq,rec.cpIrq,rec.preIrq,rec.preCpIrq,
    (unsigned)rec.fenced,(unsigned)rec.postRead,rec.prefence,(unsigned)(rec.prefenceValid?1:0),rec.fenceRaw,
    (unsigned)native_adreno_submit_staged);
 }
 IOLockUnlock(gSubmitLock);
 return rc;
}
extern "C" int cheeseburger_gpu_engine_wait(uint32_t seq,uint64_t timeout_ns){
 if(!gFence||!seq)return -1;
 auto read=[](){
  __asm__ volatile("dsb sy":::"memory");
  flush_dcache((vm_offset_t)gFence,4,FALSE);
  __asm__ volatile("dsb sy":::"memory");
  return *gFence;
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
 if(gEngineSubmitFailed)return -1;
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
// Address window the user client may map BOs into. 0x2020000 (beside the
// probe's fetched pages) panicked the kernel on the first user submission, so
// it is not used. Keep the original window until the CP-visible aperture is
// understood from the panic. One 16 MiB window keeps the 4 KiB leaf tables
// inside the preallocated cap.
extern "C" int cheeseburger_gpu_engine_window(uint64_t *start,uint64_t *bytes){
 if(!start||!bytes||!gEngine||!gRing)return -1;
 *start=0x4000000ULL;*bytes=0x1000000ULL;return 0;
}
