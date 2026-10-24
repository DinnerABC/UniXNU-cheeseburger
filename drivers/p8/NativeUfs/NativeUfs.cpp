// SPDX-License-Identifier: BSD-2-Clause-Patent
// MSM8998 warm-link UFS adapter. Generic descriptor layout/completion policy
// derived from pinned UniXNU NativeUfs; no SM8550 MMIO, SMMU or disk write path.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <libkern/c++/OSData.h>
#include <libkern/libkern.h>
#include <kern/task.h>
#include <kern/clock.h>
#include "ReadPolicy.h"
#include "TransferPoll.h"
#include "RootTransportLayout.h"

class NativeUfs;
class NativeUfsLun : public IOBlockStorageDevice {
 OSDeclareDefaultStructors(NativeUfsLun)
 NativeUfs *host=nullptr; UInt8 lun=0; UInt64 last=0; UInt32 sector=0;
public:
 bool configure(NativeUfs*,UInt8,UInt64,UInt32);
 IOReturn doEjectMedia() override {return kIOReturnUnsupported;}
 IOReturn doFormatMedia(UInt64) override {return kIOReturnNotWritable;}
 UInt32 doGetFormatCapacities(UInt64*,UInt32) const override {return 0;}
 char *getVendorString() override {return const_cast<char*>("Qualcomm");}
 char *getProductString() override {return const_cast<char*>("MSM8998 UFS read-only");}
 char *getRevisionString() override {return const_cast<char*>("P8");}
 char *getAdditionalDeviceInfoString() override {return const_cast<char*>("NativeUfs warm-link LUN");}
 IOReturn reportBlockSize(UInt64 *v) override {*v=sector;return kIOReturnSuccess;}
 IOReturn reportMaxValidBlock(UInt64 *v) override {*v=last;return kIOReturnSuccess;}
 IOReturn reportMediaState(bool *p,bool *c) override {*p=true;if(c)*c=false;return kIOReturnSuccess;}
 IOReturn reportRemovability(bool *v) override {*v=false;return kIOReturnSuccess;}
 IOReturn reportEjectability(bool *v) override {*v=false;return kIOReturnSuccess;}
 IOReturn reportWriteProtection(bool *v) override {*v=true;return kIOReturnSuccess;}
 IOReturn doSynchronizeCache() override {return kIOReturnNotWritable;}
 IOReturn doSynchronize(UInt64,UInt64,IOOptionBits) override {return kIOReturnNotWritable;}
 IOReturn getWriteCacheState(bool *v) override {*v=false;return kIOReturnSuccess;}
 IOReturn setWriteCacheState(bool) override {return kIOReturnNotWritable;}
 IOReturn doAsyncReadWrite(IOMemoryDescriptor*,UInt64,UInt64,IOStorageAttributes*,IOStorageCompletion*) override;
};
class NativeUfs : public IOService {
 OSDeclareDefaultStructors(NativeUfs)
 IOMemoryMap *hcMap=nullptr,*gccMap=nullptr;
 IOBufferMemoryDescriptor *dma=nullptr; IOLock *lock=nullptr;
 UInt8 *mem=nullptr; UInt64 phys=0; uintptr_t hc=0;
 bool poisoned=false,installed=false;
 UInt32 oldLo=0,oldHi=0,oldRun=0,oldIE=0;
 UInt32 rd(unsigned o) {UInt32 v=*reinterpret_cast<volatile UInt32*>(hc+o);asm volatile("dmb oshld":::"memory");return v;}
 void wr(unsigned o,UInt32 v) {asm volatile("dsb sy":::"memory");*reinterpret_cast<volatile UInt32*>(hc+o)=v;asm volatile("dsb sy":::"memory");}
 static void clean(void *p,unsigned n,bool invalidate=false) {
  asm volatile("dsb sy":::"memory");
  for(uintptr_t a=(uintptr_t)p;a<(uintptr_t)p+n;a+=64)
   if(invalidate)asm volatile("dc ivac,%0"::"r"(a):"memory");else asm volatile("dc civac,%0"::"r"(a):"memory");
  asm volatile("dsb sy":::"memory");
 }
 bool wait(unsigned o,UInt32 mask,UInt32 expected,unsigned us) {
  for(unsigned i=0;i<us/10;i++){if((rd(o)&mask)==expected)return true;IODelay(10);}return false;
 }
 bool command(UInt8 lun,const UInt8 cdb[16],unsigned bytes,void *dst);
 bool nop(); bool submit(unsigned direction,unsigned bytes);
public:
 bool start(IOService*) override;
 void stop(IOService*) override;
 void free() override;
 IOReturn read(UInt8 lun,UInt64 block,UInt64 count,UInt32 size,IOMemoryDescriptor*,UInt64 *done);
};
OSDefineMetaClassAndStructors(NativeUfs,IOService)
OSDefineMetaClassAndStructors(NativeUfsLun,IOBlockStorageDevice)
static UInt32 be32(const UInt8 *p){return UInt32(p[0])<<24|UInt32(p[1])<<16|UInt32(p[2])<<8|p[3];}
static void put32(UInt8 *p,UInt32 x){for(int i=3;i>=0;i--){p[i]=x&255;x>>=8;}}
static void put64(UInt8 *p,UInt64 x){put32(p,x>>32);put32(p+4,x);}
struct P8Port {
 uintptr_t hc; UInt8 *mem; unsigned ticks=0;
 UInt32 doorbell(){asm volatile("dmb oshld":::"memory");return *(volatile UInt32*)(hc+0x58);}
 UInt32 interrupt(){return *(volatile UInt32*)(hc+0x20);}
 UInt32 completedOcs(){asm volatile("dc ivac,%0\n dsb sy"::"r"(mem):"memory");return *(volatile UInt32*)(mem+8)&255;}
 bool expired(){return ticks>=100000;}
 void delay(){IODelay(2);ticks++;} // fixed 200 ms upper bound; no scheduler dependence
};
bool NativeUfs::submit(unsigned direction,unsigned bytes) {
 if(poisoned||rd(0x58)||rd(0x20)&kUfsFatalStatus)return false;
 UInt8 *q=mem+kUcdOffset;
 if(q[0]==1) {if(!p8ReadCommand(q[16],direction,bytes))return false;}
 else if(q[0]!=0||bytes||direction)return false; // only NOP OUT otherwise
 UInt32 *d=(UInt32*)mem;bzero(d,32);d[0]=0x10000000U|(direction<<25);d[2]=15;
 d[4]=(UInt32)(phys+kUcdOffset);d[5]=(phys+kUcdOffset)>>32;
 d[6]=(512U>>2)|((kRspOffset>>2)<<16);d[7]=(bytes?1:0)|((kPrdtOffset>>2)<<16);
 if(bytes){UInt32 *p=(UInt32*)(q+kPrdtOffset);p[0]=(UInt32)(phys+kDataOffset);p[1]=(phys+kDataOffset)>>32;p[2]=0;p[3]=bytes-1;}
 clean(mem,kDmaBytes);wr(0x20,1);wr(0x58,1);
 P8Port port{hc,mem};UfsTransferPoll state;
 auto end=pollUfsTransfer(port,state);
 if(end!=UfsTransferEnd::Completed){poisoned=true;IOLog("P8 UFS quarantined: completion=%u IS=%x DB=%x\n",unsigned(end),state.interruptSeen,state.doorbellSeen);return false;}
 clean(mem,kDmaBytes,true);
 if(state.ocs)return false;
 return true;
}
bool NativeUfs::nop(){
 if(poisoned||rd(0x58))return false;bzero(mem+kUcdOffset,kDataOffset-kUcdOffset);
 mem[kUcdOffset+3]=1;if(!submit(0,0))return false;
 UInt8 *r=mem+kUcdOffset+kRspOffset;return r[0]==0x20&&r[3]==1;
}
bool NativeUfs::command(UInt8 lun,const UInt8 cdb[16],unsigned bytes,void *dst){
 if(!p8ReadCommand(cdb[0],bytes?2:0,bytes)||poisoned||rd(0x58))return false;
 if(cdb[0]==0x9e&&cdb[1]!=0x10)return false;
 bzero(mem+kUcdOffset,kDmaBytes-kUcdOffset);UInt8 *q=mem+kUcdOffset;
 q[0]=1;q[1]=bytes?0x40:0;q[2]=lun;q[3]=1;put32(q+12,bytes);memcpy(q+16,cdb,16);
 if(!submit(bytes?2:0,bytes))return false;
 UInt8 *r=q+kRspOffset;
 // Require full transfer, matching transaction, LUN, task tag, no sense/EHS.
 if(r[0]!=0x21||r[2]!=lun||r[3]!=1||r[4]||r[6]||r[7]||r[10]||r[11]||be32(r+12))return false;
 if(bytes&&dst)memcpy(dst,mem+kDataOffset,bytes);return true;
}
bool NativeUfs::start(IOService *provider){
 if(!IOService::start(provider))return false;
 auto *compatible=OSDynamicCast(OSData,provider->getProperty("compatible"));
 auto *profile=OSDynamicCast(OSData,provider->getProperty("p8,msm8998-direct-dma"));
 if(!compatible||compatible->getLength()!=11||memcmp(compatible->getBytesNoCopy(),"qcom,ufshc",11)||!profile||profile->getLength()!=4||*(const UInt32*)profile->getBytesNoCopy()!=1){IOLog("P8 UFS refused: missing verified MSM8998 profile\n");return false;}
 // All bases/offsets sourced from captured cheeseburger FDT and OnePlus e34b770.
 auto *gccDesc=IOMemoryDescriptor::withPhysicalAddress(0x100000,0xb0000,kIODirectionInOut);
 if(!gccDesc)return false;gccMap=gccDesc->map();gccDesc->release();
 if(!gccMap)return false;uintptr_t gcc=gccMap->getVirtualAddress();
 const unsigned clocks[]={0x7500c,0x75008,0x76008};
 for(unsigned c:clocks){UInt32 value=*(volatile UInt32*)(gcc+c);if(value&(1U<<31)){IOLog("P8 UFS cold clock %x=%x; cold init not implemented\n",c,value);return false;}}
 auto *desc=IOMemoryDescriptor::withPhysicalAddress(0x1da4000,0x2500,kIODirectionInOut);if(!desc)return false;
 hcMap=desc->map();desc->release();if(!hcMap)return false;hc=hcMap->getVirtualAddress();
 IOLog("P8 UFS warm state CAP=%x VER=%x HCE=%x HCS=%x\n",rd(0),rd(8),rd(0x34),rd(0x30));
 if(!(rd(0x34)&1)||(rd(0x30)&3)!=3||(rd(0x20)&kUfsFatalStatus)||rd(0x58)||rd(0x78)){IOLog("P8 UFS requires idle, live firmware link; no reset attempted\n");return false;}
 dma=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,kIODirectionInOut|kIOMemoryPhysicallyContiguous|kIOMemoryMapperNone,kDmaBytes,4096);
 lock=IOLockAlloc();if(!dma||!lock)return false;mem=(UInt8*)dma->getBytesNoCopy();IOByteCount contiguous=0;phys=dma->getPhysicalSegment(0,&contiguous,kIOMemoryMapperNone);
 if(!phys||(phys&4095)||contiguous<kDmaBytes||(!(rd(0)&(1<<24))&&phys+kDmaBytes>0x100000000ULL))return false;
 bzero(mem,kDmaBytes);clean(mem,kDmaBytes);
 oldRun=rd(0x60);oldLo=rd(0x50);oldHi=rd(0x54);oldIE=rd(0x24);
 wr(0x60,0);if(!wait(0x60,1,0,10000)||rd(0x58))return false;
 // Own queue allocation, never CPU-write firmware's descriptors or SMMU tables.
 wr(0x24,0);wr(0x50,(UInt32)phys);wr(0x54,phys>>32);installed=true;wr(0x60,1);
 if(!wait(0x60,1,1,10000)||!nop()){IOLog("P8 UFS NOP failed; no retry\n");return false;}
 UInt8 report[256],cdb[16]={};cdb[0]=0xa0;put32(cdb+6,sizeof(report));
 if(!command(0,cdb,sizeof(report),report)){IOLog("P8 UFS REPORT LUNS failed\n");return false;}
 unsigned list=be32(report);if(list>248||(list&7))return false;
 unsigned found=0;for(unsigned i=8;i<8+list;i+=8){
  if(report[i]||report[i+1]>=8)continue;UInt8 lun=report[i+1],capacity[32];
  bzero(cdb,16);cdb[0]=0x9e;cdb[1]=0x10;put32(cdb+10,32);
  if(!command(lun,cdb,32,capacity))continue;
  UInt64 last=(UInt64(be32(capacity))<<32)|be32(capacity+4);UInt32 size=be32(capacity+8);
  if(!p8BlockRange(0,1,last,size)||last==~UInt64(0)||(last+1)>~UInt64(0)/size)continue;
  auto *child=new NativeUfsLun;
  if(child&&child->init(nullptr)&&child->configure(this,lun,last,size)&&child->attach(this)&&child->start(this)){
   child->registerService();found++;IOLog("P8 UFS published LUN=%u block=%u last=%llu read-only\n",lun,size,last);
  }
  if(child)child->release();
 }
 setProperty("P8ReadOnly",true);setProperty("P8Luns",found,32);return found>0;
}
void NativeUfs::stop(IOService *provider){
 if(installed&&!poisoned){wr(0x60,0);if(wait(0x60,1,0,10000)&&!rd(0x58)){wr(0x50,oldLo);wr(0x54,oldHi);wr(0x60,oldRun);wr(0x24,oldIE);installed=false;}else poisoned=true;}
 IOService::stop(provider);
}
void NativeUfs::free(){
 // A failed start may leave controller-owned memory: retain DMA and maps.
 // There is deliberately no blind cancel, reset, resubmit or disk write.
 if(!installed&&!poisoned){if(dma)dma->release();if(hcMap)hcMap->release();if(gccMap)gccMap->release();}
 if(lock)IOLockFree(lock);IOService::free();
}
IOReturn NativeUfs::read(UInt8 lun,UInt64 block,UInt64 count,UInt32 size,IOMemoryDescriptor *dst,UInt64 *done){
 *done=0;if(!dst||dst->getDirection()!=kIODirectionIn)return kIOReturnNotWritable;
 IOLockLock(lock);IOReturn status=kIOReturnSuccess;
 while(count){unsigned n=count>kDataMax/size?kDataMax/size:(unsigned)count;UInt8 cdb[16]={};cdb[0]=0x88;put64(cdb+2,block);put32(cdb+10,n);
  if(!command(lun,cdb,n*size,nullptr)){status=kIOReturnIOError;break;}
  UInt64 bytes=UInt64(n)*size;if(dst->writeBytes(*done,mem+kDataOffset,bytes)!=bytes){status=kIOReturnIOError;break;}
  *done+=bytes;block+=n;count-=n;
 }
 IOLockUnlock(lock);return status;
}
bool NativeUfsLun::configure(NativeUfs *h,UInt8 l,UInt64 end,UInt32 bs){host=h;lun=l;last=end;sector=bs;setProperty("P8Lun",l,32);return true;}
IOReturn NativeUfsLun::doAsyncReadWrite(IOMemoryDescriptor *buf,UInt64 block,UInt64 count,IOStorageAttributes*,IOStorageCompletion *completion){
 if(!buf||buf->getDirection()!=kIODirectionIn)return kIOReturnNotWritable;
 if(!p8BlockRange(block,count,last,sector)||count>~UInt64(0)/sector||buf->getLength()!=count*sector)return kIOReturnBadArgument;
 UInt64 done=0;IOReturn status=host->read(lun,block,count,sector,buf,&done);
 if(completion&&completion->action)completion->action(completion->target,completion->parameter,status,done);
 return kIOReturnSuccess; // completion owns result, including short/error status
}
