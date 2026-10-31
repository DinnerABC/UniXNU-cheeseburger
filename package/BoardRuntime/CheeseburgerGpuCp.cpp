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
#if __has_include("CheeseburgerGpuFirmware.h")
#include "CheeseburgerGpuFirmware.h"
#define CB_GPU_FW 1
#else
static const unsigned char gpu_firmware_blob[1]={0};
static const unsigned gpu_firmware_size=0;
#define CB_GPU_FW 0
#endif
extern "C" {
volatile uint64_t cb_cp[56];
void flush_dcache(vm_offset_t,unsigned,boolean_t);
int cheeseburger_gpu_smmu_map(uint64_t,uint64_t,unsigned);
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
 // Log before the call and hold for one BootLog round (5 s plus write time):
 // the previous run lost the 4th map entirely, so the last committed line must
 // name the buffer that was being mapped, not the one before it.
 IOLog("CB GPU CP map begin: iova=%llx pa=%llx pages=%u seq=%u\n",gva,b.pa,b.bytes/4096,seq);
 IOSleep(8000);
 int r=cheeseburger_gpu_smmu_map(gva,b.pa,b.bytes/4096);
 IOLog("CB GPU CP map: iova=%llx pa=%llx pages=%u r=%d seq=%u\n",gva,b.pa,b.bytes/4096,r,seq);
 if(r==0)cb_cp[8]|=uint64_t(1)<<seq;
 else {cb_cp[0]=10+seq;cb_cp[30]=uint64_t(int32_t(r));}
 ++seq;return r==0;
}
static void reset(){for(unsigned i=0;i<32;++i)cb_cp[i]=0xdeadbeef;for(unsigned i=32;i<56;++i)cb_cp[i]=0xdeadbeef;cb_cp[31]=0x0c9;}
// ---- CP reset/configure/start, ported from the pinned sagit AdrenoStartup.h
// (Linux v6.12 a5xx_gpu.c). Registers here are DWORD indices in the sagit
// enum, so each access multiplies by four.
static constexpr uint32_t kRBBase=0x800*4,kRBControl=0x802*4,kRBRead=0x806*4,kRBWrite=0x807*4,
 kMEControl=0x832*4,kPFPBase=0x835*4,kMEBase=0x838*4,kContextSwitch=0x83b*4,
 kCPInterruptStatus=0xb18*4,kInterruptClear=0x37*4,kInterruptMask=0x38*4,
 kInterruptStatus=0x4e1*4,kStatus=0x4f5*4,kStatus3=0x530*4,
 kTrustControl=0xf400*4,kGPMUReady=0xa8e0*4,kGPMUError=0xa8e1*4,
 kVBIFHalt=0x3080*4,kVBIFHalted=0x3081*4,kRBBMSwReset=0x43*4,kGPMUCM3SysReset=0xa8d8*4;
static constexpr uint32_t kHalt=0x303u,kRingControl=12u|(2u<<8)|(1u<<27),
 kErrorIRQs=0x7e|(1u<<9)|(1u<<23)|(1u<<24)|(1u<<25)|(1u<<28),
 kOwnedIRQs=(1u<<20)|0x7e|(1u<<9)|(1u<<23)|(1u<<24)|(1u<<25)|(1u<<28);
static uint32_t packet7(uint32_t op,uint32_t n){return 0x70000000u|n|(parity(n)<<15)|(op<<16)|(parity(op)<<23);}
static uint32_t protect(uint32_t base,unsigned log2){return 0x60000000u|(uint32_t(log2)<<24)|(base<<2);}
static bool idleStatus(uint32_t s){return !(s&~0x800001c1u);}
struct G {
 volatile uint32_t *v=nullptr;
 uint32_t rd(uint32_t o){uint32_t x=v[o/4];__asm__ volatile("dmb sy":::"memory");return x;}
 void wr(uint32_t o,uint32_t x){__asm__ volatile("dsb sy":::"memory");v[o/4]=x;__asm__ volatile("dsb sy":::"memory");}
 void delay(unsigned us){IODelay(us);}
 void publish(){__asm__ volatile("dsb oshst":::"memory");}
};
static void configure(G &g){
 g.wr(0x3049*4,3);g.wr(0x302a*4,9);                 // VBIF round-robin, gate-off WRREQ
 g.wr(0x46f*4,0xffffffff);g.wr(0x93*4,1);g.wr(0x2f*4,0x4000ffff);
 g.wr(0x464*4,1);g.wr(0xbb0*4,0);g.wr(0x46b*4,6);
 g.wr(0xe96*4,2);
 g.wr(0xe89*4,0xffff0000);g.wr(0xe8a*4,0x1ffff);
 g.wr(0xe87*4,0xffff0000);g.wr(0xe88*4,0x1ffff);
 g.wr(0xe8b*4,0x100000);g.wr(0xe8c*4,0);g.wr(0xe8d*4,0x1fffff);g.wr(0xe8e*4,0);
 g.wr(0x825*4,0x40);g.wr(0x826*4,0x400);
 g.wr(0x820*4,0x80000060);g.wr(0x81f*4,0x40201b16);
 g.wr(0xd00*4,(0x400u<<11)|(0x300u<<22));
 g.wr(0xe81*4,1u<<29);g.wr(0x833*4,0x02000000);g.wr(0x94*4,0xa6ffffff);
 // hardwareClockGating(true) deliberately omitted: it only adds per-block
 // gating; the reset default keeps functional clocks running.
 g.wr(0x95*4,0x3f);g.wr(0xf02*4,2u<<7);g.wr(0xcc6*4,2u<<1);g.wr(0xe8f*4,2);
 g.wr(0x8a0*4,7);
 g.wr(0xe60*4,g.rd(0xe60*4)|(1u<<10)|(1u<<23));
 g.wr(0xe04*4,g.rd(0xe04*4)&~(1u<<18));
 static const struct {uint32_t base;unsigned log2;} cpProtect[]={
  {0x4,2},{0x8,3},{0x10,4},{0x20,5},{0x40,6},{0x80,6},{0xf800,4},{0xf400,1},{0x800,6},
  {0x840,3},{0x880,5},{0xaa0,0},{0xcc0,0},{0xcf0,1},{0xe68,3},{0xe70,4},{0xe80,4},{0x10000,15}};
 for(unsigned i=0;i<sizeof cpProtect/sizeof cpProtect[0];++i)
  g.wr((0x880+i)*4,protect(cpProtect[i].base,cpProtect[i].log2));
 g.wr(0xf803*4,0);g.wr(0xf800*4,0);g.wr(0xf801*4,0);g.wr(0xf802*4,0);
 g.wr(0x860*4,1);g.wr(0xbc1*4,1);g.wr(0xc81*4,1);g.wr(0xcc5*4,1);
 g.wr(0xd01*4,1);g.wr(0xe05*4,1);g.wr(0xe41*4,1);g.wr(0xe61*4,1);
 g.wr(0xe80*4,1);g.wr(0xec1*4,1);g.wr(0xf01*4,1);g.wr(0xf810*4,1);
}
static int gpuReset(G &g){
 g.wr(kVBIFHalt,15);
 unsigned waited=0;while((g.rd(kVBIFHalted)&15)!=15){if(waited>=100000)return -1;g.delay(10);waited+=10;}
 cb_cp[40]=g.rd(kVBIFHalted);
 if(!idleStatus(g.rd(kStatus)))return -2;
 if((g.rd(kMEControl)&kHalt)!=kHalt)return -3;
 g.wr(kRBBMSwReset,1);(void)g.rd(kRBBMSwReset);g.wr(kRBBMSwReset,0);
 g.wr(kInterruptMask,0);g.wr(kMEControl,kHalt);g.wr(kVBIFHalt,15);
 waited=0;while(((g.rd(kMEControl)&kHalt)!=kHalt)||((g.rd(kVBIFHalted)&15)!=15)){
  if(waited>=100000)return -4;g.delay(10);waited+=10;}
 if(g.rd(kRBBMSwReset)||g.rd(kRBRead)||g.rd(kRBWrite)||g.rd(kContextSwitch)||g.rd(kTrustControl)>1)return -5;
 g.wr(kGPMUCM3SysReset,1);g.wr(kGPMUReady,0);g.wr(kGPMUError,0);
 if(g.rd(kGPMUCM3SysReset)!=1||g.rd(kGPMUReady)||g.rd(kGPMUError))return -6;
 cb_cp[41]=g.rd(kMEControl);
 // No ZAP/PAS handoff in this kernel, so take Linux a5xx_gpu.c's fallback
 // and leave secure mode by clearing RBBM_SECVID_TRUST_CNTL. sagit's submit
 // path also refuses to run while TrustControl is non-zero.
 g.wr(kTrustControl,0);
 cb_cp[48]=g.rd(kTrustControl);
 cb_cp[49]=g.rd(0x831*4); // CP_CNTL baseline must be zero
 return 0;
}
static void captureFaults(G &g){
 cb_cp[50]=g.rd(0xb1a*4); // CP_HW_FAULT
 cb_cp[51]=g.rd(0xb1c*4); // CP_PROTECT_STATUS
 cb_cp[52]=g.rd(0xb1b*4); // CP_AHB_FAULT
 W sm;if(mapw(sm,0x5040000,0x10000)){cb_cp[53]=rd(sm,0x8058);cb_cp[54]=rd(sm,0x48);}
}
// Loads the CP microcode addresses and submits CP_ME_INIT, so a true result
// means the CP firmware actually advanced RBRead through the ring.
static int gpuStart(G &g,volatile uint32_t *ring,unsigned ringDwords,uint64_t ringVA,uint64_t pm4VA,uint64_t pfpVA){
 if(ringDwords!=8192||(ringVA&4095)||(pm4VA&4095)||(pfpVA&4095))return -1;
 for(unsigned i=0;i<ringDwords;++i)ring[i]=0;
 configure(g);
 g.wr(kMEBase,uint32_t(pm4VA));g.wr(kMEBase+4,uint32_t(pm4VA>>32));
 g.wr(kPFPBase,uint32_t(pfpVA));g.wr(kPFPBase+4,uint32_t(pfpVA>>32));
 g.wr(kRBBase,uint32_t(ringVA));g.wr(kRBBase+4,uint32_t(ringVA>>32));
 g.wr(kContextSwitch,0);g.wr(kRBControl,kRingControl);g.wr(kRBWrite,0);
 g.wr(kInterruptMask,0);g.wr(kInterruptClear,kOwnedIRQs);
 if(g.rd(kMEBase)!=uint32_t(pm4VA)||g.rd(kPFPBase)!=uint32_t(pfpVA)||g.rd(kRBBase)!=uint32_t(ringVA)||
    g.rd(kRBControl)!=kRingControl||g.rd(kRBRead)||g.rd(kRBWrite)||g.rd(kContextSwitch)||
    g.rd(0x860*4)!=1||g.rd(0x8a0*4)!=7)return -2;
 unsigned cursor=0;
 ring[cursor++]=packet7(0x48,8); // CP_ME_INIT, A540 payload
 static const uint32_t init[]={0x2f,3,0x20000000,0,0,0,0,0};
 for(unsigned i=0;i<sizeof init/sizeof init[0];++i)ring[cursor++]=init[i];
 flush_dcache((vm_offset_t)ring,ringDwords*4,FALSE);
 g.publish();g.wr(kVBIFHalt,0);g.wr(kMEControl,0);
 g.publish();g.wr(kRBWrite,cursor);
 for(unsigned waited=0;waited<300000;waited+=10){
  const uint32_t irq=g.rd(kInterruptStatus),cpirq=g.rd(kCPInterruptStatus);
  if((irq&kErrorIRQs)||(cpirq&0x3f)){cb_cp[44]=irq;cb_cp[45]=cpirq;return -3;}
  const uint32_t read=g.rd(kRBRead);
  if(read==cursor&&!(g.rd(kStatus)&~1u)&&!(g.rd(kStatus3)&(1u<<24))){
   cb_cp[42]=read;cb_cp[43]=g.rd(kRBWrite);captureFaults(g);return 0;}
  g.delay(10);
 }
 cb_cp[42]=g.rd(kRBRead);cb_cp[43]=g.rd(kRBWrite);
 cb_cp[44]=g.rd(kInterruptStatus);cb_cp[45]=g.rd(kCPInterruptStatus);
 cb_cp[46]=g.rd(kStatus3);cb_cp[47]=g.rd(kStatus);
 captureFaults(g);
 return -4;
}
}
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
 IOSleep(8000); // let one BootLog round persist the state before any map
 // Persist each map result before the next step: the panel may black here and
 // the run is often returned manually, so a quick all-at-once map could be lost.
 if(!mapBuf(ring,gva,seq)){IOSleep(3000);return;}IOSleep(3000);gva+=ring.bytes;
 if(!mapBuf(pm4,gva,seq)){IOSleep(3000);return;}IOSleep(3000);gva+=pm4.bytes;
 if(!mapBuf(pfp,gva,seq)){IOSleep(3000);return;}IOSleep(3000);gva+=pfp.bytes;
 cb_cp[25]=ring.gva;cb_cp[26]=pm4.gva;cb_cp[27]=pfp.gva;
 cb_cp[30]=seq;
 if(!mapBuf(gpmu,gva,seq)){IOSleep(3000);return;}IOSleep(3000);gva+=gpmu.bytes;
 if(!mapBuf(fence,gva,seq)){IOSleep(3000);return;}
 cb_cp[28]=gpmu.gva;cb_cp[29]=fence.gva;
 cb_cp[30]=seq;cb_cp[0]=0;
 IOLog("CB GPU CP: fw=%u pm4=%u pfp=%u gpmu=%u ring=%u map=%u me=%x vbif=%x/%x trust=%x\n",
  gpu_firmware_size,fw.cpBytes(0),fw.cpBytes(1),fw.gpmuWords(),ring.bytes,seq,
  uint32_t(cb_cp[9]),uint32_t(cb_cp[10]),uint32_t(cb_cp[11]),uint32_t(cb_cp[18]));
 // CP reset/configure/start is a separate opt-in: it writes GPU registers and
 // starts real firmware DMA, unlike the read-only mapping probe above.
 unsigned start=0;
 if(PE_parse_boot_argn("cheeseburger-gpu-start",&start,sizeof start)&&start&&gp.v){
  G g;g.v=gp.v;
  cb_cp[38]=1;cb_cp[39]=0;
  IOLog("CB GPU start: begin ring=%llx pm4=%llx pfp=%llx\n",
   ring.gva,pm4.gva,pfp.gva);
  IOSleep(8000);
  int rc=gpuReset(g);
  cb_cp[39]=uint64_t(int32_t(rc));
  IOLog("CB GPU start: reset rc=%d me=%x vbif=%x trust=%x cpcnl=%x\n",rc,
   uint32_t(cb_cp[41]),uint32_t(cb_cp[40]),uint32_t(cb_cp[48]),uint32_t(cb_cp[49]));
  IOSleep(8000);
  if(rc==0){
   cb_cp[38]=2;
   rc=gpuStart(g,(volatile uint32_t*)ring.va,ring.bytes/4,ring.gva,pm4.gva,pfp.gva);
   cb_cp[38]=3;cb_cp[39]=uint64_t(int32_t(rc));
   cb_cp[46]=g.rd(kStatus3);cb_cp[47]=g.rd(kStatus);
   IOLog("CB GPU start: kick rc=%d r=%x w=%x irq=%x cpirq=%x st=%x st3=%x\n",rc,
    uint32_t(cb_cp[42]),uint32_t(cb_cp[43]),uint32_t(cb_cp[44]),uint32_t(cb_cp[45]),
    uint32_t(cb_cp[47]),uint32_t(cb_cp[46]));
   IOLog("CB GPU start: faults cp=%x/%x/%x smfsr=%x smg=%x\n",
    uint32_t(cb_cp[50]),uint32_t(cb_cp[51]),uint32_t(cb_cp[52]),
    uint32_t(cb_cp[53]),uint32_t(cb_cp[54]));
  }
  IOSleep(8000);
 }
}
