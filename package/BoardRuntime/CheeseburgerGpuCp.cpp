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
volatile uint64_t cb_cp[32];
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
 b.gva=gva;int r=cheeseburger_gpu_smmu_map(gva,b.pa,b.bytes/4096);
 IOLog("CB GPU CP map: iova=%llx pa=%llx pages=%u r=%d seq=%u\n",gva,b.pa,b.bytes/4096,r,seq);
 if(r==0)cb_cp[8]|=uint64_t(1)<<seq;
 else {cb_cp[0]=10+seq;cb_cp[30]=uint64_t(int32_t(r));}
 ++seq;return r==0;
}
static void reset(){for(unsigned i=0;i<32;++i)cb_cp[i]=0xdeadbeef;cb_cp[31]=0x0c9;}
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
 // Persist each map result before the next step: the panel may black here and
 // the run is often returned manually, so a quick all-at-once map could be lost.
 if(!mapBuf(ring,gva,seq)){IOSleep(3000);return;}IOSleep(3000);gva+=ring.bytes;
 if(!mapBuf(pm4,gva,seq)){IOSleep(3000);return;}IOSleep(3000);gva+=pm4.bytes;
 if(!mapBuf(pfp,gva,seq)){IOSleep(3000);return;}IOSleep(3000);gva+=pfp.bytes;
 if(!mapBuf(gpmu,gva,seq)){IOSleep(3000);return;}IOSleep(3000);gva+=gpmu.bytes;
 if(!mapBuf(fence,gva,seq)){IOSleep(3000);return;}
 cb_cp[25]=ring.gva;cb_cp[26]=pm4.gva;cb_cp[27]=pfp.gva;cb_cp[28]=gpmu.gva;cb_cp[29]=fence.gva;
 cb_cp[30]=seq;
 W gp;if(!mapw(gp,0x5000000,0x40000)){cb_cp[0]=10;return;}
 cb_cp[9]=rd(gp,0x20c8);cb_cp[10]=rd(gp,0xc200);cb_cp[11]=rd(gp,0xc204);
 cb_cp[12]=rd(gp,0x2000);cb_cp[13]=rd(gp,0x2008);cb_cp[14]=rd(gp,0x2018);cb_cp[15]=rd(gp,0x201c);
 cb_cp[16]=rd(gp,0x13d4)|(uint64_t(rd(gp,0x14c0))<<32);
 cb_cp[17]=rd(gp,0x1384)|(uint64_t(rd(gp,0x2c60))<<32);
 cb_cp[18]=rd(gp,0xf400);cb_cp[19]=rd(gp,0xf800*4);
 cb_cp[30]=seq;cb_cp[0]=0;
 IOLog("CB GPU CP: fw=%u pm4=%u pfp=%u gpmu=%u ring=%u map=%u me=%x vbif=%x/%x trust=%x\n",
  gpu_firmware_size,fw.cpBytes(0),fw.cpBytes(1),fw.gpmuWords(),ring.bytes,seq,
  uint32_t(cb_cp[9]),uint32_t(cb_cp[10]),uint32_t(cb_cp[11]),uint32_t(cb_cp[18]));
}
