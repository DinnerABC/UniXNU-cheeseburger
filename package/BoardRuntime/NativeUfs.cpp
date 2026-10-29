// SPDX-License-Identifier: BSD-2-Clause-Patent
// MSM8998 warm-link UFS adapter. Generic descriptor layout/completion policy
// derived from pinned UniXNU NativeUfs; no SM8550 MMIO, SMMU or disk write path.
#include <IOKit/IOService.h>
#include <mach/kmod.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <libkern/c++/OSData.h>
#include <libkern/libkern.h>
#include <libkern/c++/OSSymbol.h>
#include <kern/task.h>
#include <kern/clock.h>
#include <libkern/crypto/sha2.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSIterator.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSUnserialize.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>
#include <sys/param.h>
#include <kern/thread.h>
#include "verified_root.h"
#include "VerifiedBlockPolicy.h"
#define NATIVE_VERIFIED_BLOCK 1
extern "C" { extern const struct vr_crypto vr_xnu_crypto; extern const uint8_t verified_root_key[]; }
#include "ReadPolicy.h"
#include "LogWindowConfig.h"
#include <pexpert/pexpert.h>
#include "ResponsePolicy.h"
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
 UInt64 primaryLast=0;bool primary4k=false;
 bool logOpened=false,logFault=false,logCommandActive=false,miscCommandActive=false;
 bool poisoned=false,installed=false; unsigned readProofs=0;
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
 bool command(UInt8 lun,const UInt8 cdb[16],unsigned bytes,void *dst,unsigned *received=nullptr);
 bool nop(); bool submit(unsigned direction,unsigned bytes);
public:
 unsigned logGptCheck();
 bool logOpen(const char **);
 IOReturn logWrite(UInt64,UInt32,const void*);
 IOReturn logSync();
 bool rawWrite4k(UInt64,const void*);
 bool miscBootRecovery();
 bool prepareRootWindows();
 IOReturn readRaw4k(UInt8,UInt64,UInt32,void*);
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
 if(q[0]==1) {
  bool read=p8ReadCommand(q[16],direction,bytes);
  bool logWrite=logCommandActive&&logOpened&&!logFault&&q[1]==0x20&&q[16]==0x2a&&cbLogCommand(q+16,q[2],direction,bytes);
  bool logSync=logCommandActive&&logOpened&&!logFault&&q[1]==0&&q[16]==0x35&&cbLogCommand(q+16,q[2],direction,bytes);
  UInt32 lba=((UInt32)q[18]<<24)|((UInt32)q[19]<<16)|((UInt32)q[20]<<8)|q[21];
  bool miscWrite=miscCommandActive&&logOpened&&!logFault&&q[1]==0x20&&q[2]==0&&q[16]==0x2a&&
    direction==1&&bytes==4096&&lba==73736&&q[23]==0&&q[24]==1;
  if(!read&&!logWrite&&!logSync&&!miscWrite)return false;
 }
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
 if(state.ocs){IOLog("P11 UFS OCS error=%x op=%x IS=%x DB=%x\n",state.ocs,q[16],state.interruptSeen,state.doorbellSeen);return false;}
 return true;
}
bool NativeUfs::nop(){
 if(poisoned||rd(0x58))return false;bzero(mem+kUcdOffset,kDataOffset-kUcdOffset);
 mem[kUcdOffset+3]=1;if(!submit(0,0))return false;
 UInt8 *r=mem+kUcdOffset+kRspOffset;return r[0]==0x20&&r[3]==1;
}
bool NativeUfs::command(UInt8 lun,const UInt8 cdb[16],unsigned bytes,void *dst,unsigned *received){
 if(received)*received=0;
 if(!p8ReadCommand(cdb[0],bytes?2:0,bytes)||poisoned||rd(0x58))return false;
 if(cdb[0]==0x9e&&cdb[1]!=0x10)return false;
 bzero(mem+kUcdOffset,kDmaBytes-kUcdOffset);UInt8 *q=mem+kUcdOffset;
 q[0]=1;q[1]=bytes?0x40:0;q[2]=lun;q[3]=1;put32(q+12,bytes);memcpy(q+16,cdb,16);
 if(!submit(bytes?2:0,bytes))return false;
 UInt8 *r=q+kRspOffset;
 P11UfsResponse response;
 bool valid=p11UfsResponse(r,512,lun,cdb[0],bytes,response);
 // Protocol metadata only: never dump block contents or device identity data.
 if(!valid||cdb[0]==0xa0){
  unsigned sense=(unsigned(r[32])<<8)|r[33],key=0,asc=0,ascq=0;
  unsigned segment=(unsigned(r[10])<<8)|r[11];
  if(sense<=18&&sense+2<=segment){
   const UInt8 *s=r+34;unsigned format=s[0]&0x7f;
   if((format==0x70||format==0x71)&&sense>=14){key=s[2]&15;asc=s[12];ascq=s[13];}
   else if((format==0x72||format==0x73)&&sense>=4){key=s[1]&15;asc=s[2];ascq=s[3];}
  }
  IOLog("P11 UFS rsp op=%x lun=%u type=%x rx_lun=%u tag=%u cs=%x ehs=%x flags=%x response=%x status=%x residual=%u segment=%u sense=%u key=%x ASC=%x/%x accepted=%u\n",
   cdb[0],lun,r[0],r[2],r[3],r[4],r[8],r[1],r[6],r[7],be32(r+12),segment,sense,key,asc,ascq,unsigned(valid));
 }
 if(!valid)return false;
 if(received)*received=response.transferred;
 if(response.transferred&&dst)memcpy(dst,mem+kDataOffset,response.transferred);
 return true;
}
bool NativeUfs::start(IOService *provider){
 // Refuse before any queue/DMA operation if original runtime storage ABI differs.
 const OSSymbol *key=OSSymbol::withCString("IOBlockStorageDevice");
 const OSMetaClass *meta=key?OSMetaClass::getMetaClassWithName(key):nullptr;
 OSSafeReleaseNULL(key);
 if(!meta||meta!=&IOBlockStorageDevice::gMetaClass||meta->getClassSize()!=sizeof(IOBlockStorageDevice)||IOService::gMetaClass.getClassSize()!=sizeof(IOService)){
  IOLog("P11 UFS: original storage runtime ABI mismatch; no hardware access\n");return false;
 }
 IOLog("P11 UFS: original storage ABI matched; entering read-only warm-link driver\n");

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
 IOLog("P11 UFS NOP completed; enumerating read-only LUNs\n");
 UInt8 report[256]={},cdb[16]={};unsigned received=0;cdb[0]=0xa0;put32(cdb+6,sizeof(report));
 if(!command(0,cdb,sizeof(report),report,&received)){IOLog("P8 UFS REPORT LUNS failed\n");return false;}
 if(!p11UfsLunList(report,received)){IOLog("P11 UFS REPORT LUNS malformed/truncated: received=%u list=%u\n",received,be32(report));return false;}
 unsigned list=be32(report);IOLog("P11 UFS REPORT LUNS accepted received=%u entries=%u\n",received,list/8);
 // Two phases: finish every DMA command before publishing any child. Storage
 // probes may run synchronously/asynchronously from registerService(), and must
 // never share the one command/response/data allocation with enumeration.
 struct Capacity {UInt8 lun;UInt64 last;UInt32 size;};Capacity capacities[8];
 unsigned capacityCount=0;
 for(unsigned i=8;i<8+list;i+=8){
  if(report[i]||report[i+1]>=8)continue;UInt8 lun=report[i+1],capacity[32];
  bool duplicate=false;for(unsigned j=0;j<capacityCount;j++)if(capacities[j].lun==lun)duplicate=true;
  if(duplicate)continue;
  bzero(cdb,16);cdb[0]=0x9e;cdb[1]=0x10;put32(cdb+10,32);
  if(!command(lun,cdb,32,capacity))continue;
  UInt64 last=(UInt64(be32(capacity))<<32)|be32(capacity+4);UInt32 size=be32(capacity+8);
  if(!p8BlockRange(0,1,last,size)||last==~UInt64(0)||(last+1)>~UInt64(0)/size)continue;
  capacities[capacityCount++]={lun,last,size};
  if(lun==0&&size==4096){primaryLast=last;primary4k=true;}
 }
 IOLog("P11 UFS enumeration complete: usable=%u; publishing after exclusive command phase\n",capacityCount);
 if(primary4k)prepareRootWindows();
 unsigned found=0;
 for(unsigned i=0;i<capacityCount;i++){
  const auto &c=capacities[i];auto *child=new NativeUfsLun;
  if(child&&child->init(nullptr)&&child->configure(this,c.lun,c.last,c.size)&&child->attach(this)&&child->start(this)){
   child->registerService();found++;IOLog("P11 UFS published LUN=%u block=%u last=%llu read-only\n",c.lun,c.size,c.last);
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
 while(count){unsigned n=count>kDataMax/size?kDataMax/size:(unsigned)count;UInt8 cdb[16]={};
  if(!p11Read10(cdb,block,n)){status=kIOReturnUnsupported;break;}
  if(!command(lun,cdb,n*size,nullptr)){status=kIOReturnIOError;break;}
  UInt64 bytes=UInt64(n)*size;
  if(readProofs<4){readProofs++;IOLog("P11 UFS READ10 completed lun=%u lba=%llu blocks=%u bytes=%llu; exact successful response\n",lun,block,n,bytes);}
  if(dst->writeBytes(*done,mem+kDataOffset,bytes)!=bytes){status=kIOReturnIOError;break;}
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


#include "RootDiagnostics.h"
static UfsRootDiagnostics gRootDiagnostic;
static bool gRootDiagnosticEnabled=false;
static const char *native_ufs_root_failure=nullptr;
static NativeUfs *gRootHost=nullptr;
static bool gLogLayoutSafe=false;
static IOReturn hostRead4k(uint8_t lun,uint64_t lba,uint32_t blocks,void *out){
 return gRootHost?gRootHost->readRaw4k(lun,lba,blocks,out):kIOReturnNotReady;
}
#include "RootLayout.h"
#include "RootWindowRead.h"
#include "RootPublication.h"
IOReturn NativeUfs::readRaw4k(UInt8 lun,UInt64 lba,UInt32 count,void *out){
 if(lun||!primary4k||!out||count>kDataMax/4096||!p8BlockRange(lba,count,primaryLast,4096))return kIOReturnBadArgument;
 UInt8 cdb[16]={};if(!p11Read10(cdb,lba,count))return kIOReturnUnsupported;
 IOLockLock(lock);bool ok=command(0,cdb,count*4096,out);IOLockUnlock(lock);
 return ok?kIOReturnSuccess:kIOReturnIOError;
}
// Private storage adapter for the official BootLog writer. No block-device write exposure.
unsigned NativeUfs::logGptCheck(){
 UInt8 *storage=(UInt8*)IOMalloc(8192);if(!storage)return 6;UInt8 *block=storage,*entries=storage+4096,digest[32];
 auto check=[&]()->unsigned {
 if(!primary4k||poisoned||readRaw4k(0,1,1,block)!=kIOReturnSuccess)return 1;
 if(memcmp(block,"EFI PART",8)||le32(block+12)!=92||le32(block+16)!=CB_LOG_GPT_HEADER_CRC||rootCrc32(block,92,true)!=CB_LOG_GPT_HEADER_CRC||le64(block+72)!=2||le32(block+80)!=32||le32(block+84)!=128||le32(block+88)!=CB_LOG_GPT_ENTRIES_CRC)return 2;
 if(readRaw4k(0,2,1,entries)!=kIOReturnSuccess)return 3;
 SHA256_CTX sha;SHA256_Init(&sha);SHA256_Update(&sha,entries,4096);SHA256_Final(digest,&sha);
 if(memcmp(digest,cb_log_gpt_hash,32)||rootCrc32(entries,4096)!=CB_LOG_GPT_ENTRIES_CRC)return 4;
 const UInt8 *u=entries+13*128;if(le64(u+32)!=CB_LOG_USER_FIRST||le64(u+40)!=CB_LOG_USER_LAST)return 5;
 return 0;
 };unsigned result=check();IOFree(storage,8192);return result;
}
bool NativeUfs::logOpen(const char **why){
 if(logOpened){*why="already open";return true;}
 extern volatile int native_owner_cache_authorized;
 /* The log window is its own authorized range; GPT geometry is verified below. Root layout may still be invalid in early bring-up. */
 unsigned enabled=0;PE_parse_boot_argn("sheng-log",&enabled,sizeof(enabled));
 if(enabled!=1||logFault||logGptCheck()){*why="log geometry/auth refused";return false;}
 UInt8 *buf=(UInt8*)IOMalloc(kDataMax);if(!buf){*why="allocation failed";return false;}
 bool ok=readRaw4k(0,CB_LOG_FIRST,1,buf)==kIOReturnSuccess;
 bool existing=ok&&!memcmp(buf,"SHLOG001",8)&&le32(buf+8)==3&&le32(buf+12)>=140&&le32(buf+12)<=4096&&le64(buf+24)==CB_LOG_FIRST&&le32(buf+32)==CB_LOG_BLOCKS&&!memcmp(buf+88,"cheeseburger-P21",15);
 if(ok&&!existing){
  SHA256_CTX sha;UInt8 hash[32];SHA256_Init(&sha);
  for(UInt64 at=0;ok&&at<CB_LOG_BLOCKS;at+=32){ok=readRaw4k(0,CB_LOG_FIRST+at,32,buf)==kIOReturnSuccess;if(ok)SHA256_Update(&sha,buf,kDataMax);}
  if(ok){SHA256_Final(hash,&sha);ok=!memcmp(hash,cb_log_empty_hash,32);}
 }
 IOFree(buf,kDataMax);if(!ok){*why="window is not backed-up empty or own log";return false;}
 logOpened=true;*why="ready";IOLog("CB persistent log: bounded window lba=%llu blocks=%llu\n",CB_LOG_FIRST,CB_LOG_BLOCKS);return true;
}
IOReturn NativeUfs::logWrite(UInt64 lba,UInt32 count,const void *src){
 if(!logOpened||logFault||!src||!cbLogRange(lba,count))return kIOReturnNotWritable;
 /* The GPT was verified when the window opened. A per-write GPT read doubled UFS traffic
  * while userland was paging in and wedged the system about 95 s after boot (logfix8-10). */
 IOLockLock(lock);bool ok=!poisoned&&!rd(0x58);unsigned bytes=count*4096;
 if(ok){
  bzero(mem+kUcdOffset,kDmaBytes-kUcdOffset);UInt8 *q=mem+kUcdOffset;
  q[0]=1;q[1]=0x20;q[2]=0;q[3]=1;put32(q+12,bytes);q[16]=0x2a;put32(q+18,(UInt32)lba);q[23]=count>>8;q[24]=count;
  memcpy(mem+kDataOffset,src,bytes);logCommandActive=true;ok=submit(1,bytes);logCommandActive=false;
  P11UfsResponse response;if(ok)ok=p11UfsResponse(q+kRspOffset,512,0,0x2a,bytes,response);
  // Readback while the same queue lock is held. Never retry failed writes.
  UInt8 cdb[16]={};if(ok)ok=p11Read10(cdb,lba,count)&&command(0,cdb,bytes,nullptr)&&!memcmp(mem+kDataOffset,src,bytes);
 }
 if(!ok)logFault=true;IOLockUnlock(lock);return ok?kIOReturnSuccess:kIOReturnIOError;
}
IOReturn NativeUfs::logSync(){
 if(!logOpened||logFault)return kIOReturnNotReady;
 IOLockLock(lock);bool ok=!poisoned&&!rd(0x58);
 if(ok){bzero(mem+kUcdOffset,kDmaBytes-kUcdOffset);UInt8 *q=mem+kUcdOffset;q[0]=1;q[3]=1;q[16]=0x35;logCommandActive=true;ok=submit(0,0);logCommandActive=false;P11UfsResponse response;if(ok)ok=p11UfsResponse(q+kRspOffset,512,0,0x35,0,response);}
 if(!ok)logFault=true;IOLockUnlock(lock);return ok?kIOReturnSuccess:kIOReturnIOError;
}
bool NativeUfs::rawWrite4k(UInt64 lba,const void *src){
 if(poisoned||!primary4k||!src||!p8BlockRange(lba,1,primaryLast,4096))return false;
 IOLockLock(lock);bool ok=!poisoned&&!rd(0x58);unsigned bytes=4096;
 if(ok){
  bzero(mem+kUcdOffset,kDmaBytes-kUcdOffset);UInt8 *q=mem+kUcdOffset;
  q[0]=1;q[1]=0x20;q[2]=0;q[3]=1;put32(q+12,bytes);q[16]=0x2a;put32(q+18,(UInt32)lba);q[23]=0;q[24]=1;
  memcpy(mem+kDataOffset,src,bytes);miscCommandActive=(lba==73736);logCommandActive=true;
  ok=submit(1,bytes);logCommandActive=false;miscCommandActive=false;
  P11UfsResponse response;if(ok)ok=p11UfsResponse(q+kRspOffset,512,0,0x2a,bytes,response);
  UInt8 cdb[16]={};if(ok)ok=p11Read10(cdb,lba,1)&&command(0,cdb,bytes,nullptr)&&!memcmp(mem+kDataOffset,src,bytes);
 }
 IOLockUnlock(lock);return ok;
}
// Android BCB in misc: boot-recovery on the next bootloader pass. Only the misc LBA from GPT.
bool NativeUfs::miscBootRecovery(){
 if(!logOpened||logFault||logGptCheck())return false;
 UInt8 entries[4096];
 if(readRaw4k(0,2,1,entries)!=kIOReturnSuccess)return false;
 for(int i=0;i<32;i++){
  const UInt8 *e=entries+i*128;
  bool empty=true;for(int j=0;j<16;j++)if(e[j]){empty=false;break;}
  if(empty)continue;
  static const UInt8 name[10]={'m','\0','i','\0','s','\0','c','\0','\0','\0'};
  if(memcmp(e+56,name,10))continue;
  UInt64 first=le64(e+32),last=le64(e+40);
  // GPT on this UFS device uses 4 KiB LBAs. Match the captured misc extent.
  if(first!=73736||last!=73991||!p8BlockRange(first,1,primaryLast,4096))return false;
  UInt8 *block=(UInt8*)IOMalloc(4096);if(!block)return false;
  bool ok=readRaw4k(0,first,1,block)==kIOReturnSuccess;
  if(ok){memset(block,0,32);memcpy(block,"boot-recovery",13);ok=rawWrite4k(first,block);}
  IOFree(block,4096);
  return ok&&logSync()==kIOReturnSuccess;
 }
 return false;
}
extern "C" int native_ufs_misc_boot_recovery(void){return gRootHost?gRootHost->miscBootRecovery():0;}
extern "C" int native_ufs_log_open(uint64_t *first,uint64_t *blocks,const char **why){if(!gRootHost){*why="no root layout";return 0;}if(!gRootHost->logOpen(why))return 0;*first=CB_LOG_FIRST;*blocks=CB_LOG_BLOCKS;return 1;}
// Last block of the authorized window is outside every normal slot: a panic-time note that does not
// depend on the BootLog writer having opened. Read back by scripts/logging/read-log-note.py.
extern "C" int native_ufs_log_force_note(const char *text){
 if(!gRootHost||!text)return 0;
 UInt8 *block=(UInt8*)IOMalloc(4096);if(!block)return 0;
 memset(block,0,4096);memcpy(block,"CBNOTE01",8);
 UInt64 stamp=0;__asm__ volatile("isb; mrs %0,cntpct_el0":"=r"(stamp));
 int at=snprintf((char*)block+8,64,"t=%llx ",(unsigned long long)stamp);
 size_t n=0;for(;text[n]&&at+n<4000;n++){char c=text[n];block[8+at+n]=(UInt8)((c>=32&&c<127)?c:'?');}
 bool ok=gRootHost->rawWrite4k(CB_LOG_FIRST+CB_LOG_BLOCKS-1,block);
 IOFree(block,4096);return ok?1:0;
}
extern "C" int native_ufs_log_write(uint64_t rel,uint32_t count,const void *src){if(!gRootHost||rel>=CB_LOG_BLOCKS||!count||count>CB_LOG_BLOCKS-rel)return 0;return gRootHost->logWrite(CB_LOG_FIRST+rel,count,src)==kIOReturnSuccess;}
extern "C" int native_ufs_log_sync(void){return gRootHost&&gRootHost->logSync()==kIOReturnSuccess;}
extern "C" unsigned native_ufs_store_gpt_check(void){return gRootHost?gRootHost->logGptCheck():1;}

class NativeUfsBlock : public IOBlockStorageDevice {
  OSDeclareDefaultStructors (NativeUfsBlock)
  uint64_t absolute = 0;      // byte offset of the window on LUN 0
  uint64_t bytes = 0;
  char label[32] = {};
  uint8_t *bounce = nullptr;
  IOLock *windowLock = nullptr;
  bool online = false;
#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
  struct vr_view verified = {};
  vm_offset_t verifiedStorage = 0;
  vm_size_t verifiedStorageBytes = 0;
  bool configureVerification ();
#endif
public:
#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
  bool authenticatesVolume (const char *uuid, uint8_t *manifestDigest = nullptr) const;
  bool signedVolumeUuid (char out[37]) const;
#endif
  const char *windowLabel () const { return label; }
  uint8_t expectedImageHash[32] = {};
  bool configure (const char *name, uint64_t absoluteOffset, uint64_t size, const uint8_t digest[32]);
  bool start (IOService *provider) override;
  void free () override;
  IOReturn doEjectMedia () override { return kIOReturnUnsupported; }
  IOReturn doFormatMedia (UInt64) override { return kIOReturnNotWritable; }
  UInt32 doGetFormatCapacities (UInt64 *, UInt32) const override { return 0; }
  char *getVendorString () override { return const_cast<char *> ("Native"); }
  char *getProductString () override { return label; }
  char *getRevisionString () override { return const_cast<char *> ("1.0"); }
  char *getAdditionalDeviceInfoString () override { return const_cast<char *> ("UFS LUN 0 userdata window, read-only"); }
  IOReturn reportBlockSize (UInt64 *value) override { *value = 512; return kIOReturnSuccess; }
  IOReturn reportEjectability (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn reportMaxValidBlock (UInt64 *value) override { *value = bytes / 512 - 1; return kIOReturnSuccess; }
  IOReturn reportMediaState (bool *present, bool *changed) override {
    *present = online; if (changed) *changed = false; return kIOReturnSuccess;
  }
  IOReturn reportRemovability (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn reportWriteProtection (bool *value) override { *value = true; return kIOReturnSuccess; }
  IOReturn doSynchronizeCache () override { return online ? kIOReturnSuccess : kIOReturnNotReady; }
  IOReturn doSynchronize (UInt64, UInt64, IOOptionBits) override { return doSynchronizeCache (); }
  IOReturn getWriteCacheState (bool *value) override { *value = false; return kIOReturnSuccess; }
  IOReturn setWriteCacheState (bool) override { return kIOReturnUnsupported; }
  IOReturn doAsyncReadWrite (IOMemoryDescriptor *, UInt64, UInt64, IOStorageAttributes *, IOStorageCompletion *) override;
};
OSDefineMetaClassAndStructors (NativeUfsBlock, IOBlockStorageDevice)

extern "C" {
volatile uint32_t native_ufs_blocks_published = 0;
volatile uint64_t native_ufs_block_reads = 0;
volatile uint64_t native_ufs_block_failures = 0;
}

static char gRootDiagnosticText[2500];
static char gRootMediaText[1000];
// Passive diagnostic reader: cached software values only. It must remain safe
// when called from an IRQ while the transport is busy: no locks, allocations,
// MMIO reads, queue resets, or changes to the command/trust state. Fields can
// represent adjacent updates; this is explicitly not an atomic HW snapshot.
extern "C" void native_ufs_progress_text (char *out, size_t size)
{
  UfsRootDiagnostics sample = gRootDiagnostic;
  sample.published = native_ufs_blocks_published;
  sample.reads = native_ufs_block_reads;
  sample.failures = native_ufs_block_failures;
  formatUfsRootDiagnostics (out, size, sample, "cached values; not a new HW fault sample", "UFS CACHED SNAPSHOT");
}

static void rootDiagnosticFailure (const char *reason)
{
  // Kept even when the panic diagnostic is off, so the boot page can say why nothing was published.
  native_ufs_root_failure = reason;
  if (!gRootDiagnosticEnabled) return;
  gRootDiagnostic.published = native_ufs_blocks_published;
  gRootDiagnostic.reads = native_ufs_block_reads;
  gRootDiagnostic.failures = native_ufs_block_failures;
  formatUfsRootDiagnostics (gRootDiagnosticText, sizeof (gRootDiagnosticText), gRootDiagnostic, reason);
  strlcat (gRootDiagnosticText, gRootMediaText, sizeof (gRootDiagnosticText));
  panic ("%s", gRootDiagnosticText);
}

bool NativeUfsBlock::configure (const char *name, uint64_t absoluteOffset, uint64_t size, const uint8_t digest[32])
{
  memcpy(expectedImageHash,digest,32);
  if (!name || !ufsWindowGeometry (absoluteOffset, size)) {
    return false;
  }
  strlcpy (label, name, sizeof (label));
  absolute = absoluteOffset;
  bytes = size;
  windowLock = IOLockAlloc ();
  if (!windowLock) return false;
  bounce = static_cast<uint8_t *> (IOMalloc (kDataMax));
  if (!bounce) {
    return false;
  }
#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
  // Reads are served only after each 4096-byte integrity block verifies against the owner-signed
  // policy; without a matching policy the device is never published.
  return configureVerification ();
#else
  return true;
#endif
}

#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
volatile uint64_t native_ufs_verified_blocks = 0;
// Time spent hashing/verifying and reading from the UFS transport, in generic-timer ticks (diagnostic only).
volatile uint64_t native_ufs_verify_ticks = 0, native_ufs_read_ticks = 0;
static inline uint64_t timerTicks (void) {uint64_t v; __asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (v)); return v;}
volatile uint32_t native_ufs_integrity_failures = 0;

static bool gGroupWindow = false;

bool NativeUfsBlock::configureVerification ()
{
  if (gGroupWindow) {
    /* Grouped-container bring-up: the relocated window carries its own signed
     * policy built separately; read it without the per-block owner table until
     * that policy is linked in. */
    gRootDiagnostic.stage = "group window: policy skipped";
    return true;
  }
  gRootDiagnostic.stage = "policy selection";
  gRootDiagnostic.windowBytes = bytes;
  gRootDiagnostic.policyCount = verified_root_policy_count;
  gRootDiagnostic.verify = 0xffffffff;
  // Select the one policy whose signed image size equals this window; two matches are refused.
  const uint8_t *manifest = nullptr;
  uint64_t manifestBytes = 0;
  if (!verified_root_policy_count || verified_root_policy_count > 16) {
    gRootDiagnostic.stage = "policy count invalid";
    return false;
  }
  for (uint64_t index = 0; index < verified_root_policy_count; ++index) {
    const NativeVerifiedBlockPolicy &policy = verified_root_policies[index];
    if (!policy.manifest || policy.manifestBytes < VR_HEADER_BYTES + VR_SIGNATURE_BYTES) {
      gRootDiagnostic.stage = "policy envelope invalid";
      return false;
    }
    uint64_t imageBytes = 0;
    for (unsigned i = 0; i < 8; ++i) {
      imageBytes |= static_cast<uint64_t> (policy.manifest[24 + i]) << (8 * i);
    }
    if (imageBytes != bytes) {
      continue;
    }
    if (manifest) {
      gRootDiagnostic.stage = "duplicate capacity-matching policy";
      return false;
    }
    manifest = policy.manifest;
    manifestBytes = policy.manifestBytes;
  }
  if (!manifest) {
    gRootDiagnostic.stage = "no capacity-matching policy";
    return false;
  }
  gRootDiagnostic.stage = "policy storage";
  vm_size_t tableBytes = round_page (static_cast<vm_size_t> (manifestBytes));
  if (tableBytes < manifestBytes || tableBytes > UINT64_MAX - 4096) {
    gRootDiagnostic.stage = "policy size overflow";
    return false;
  }
  verifiedStorageBytes = tableBytes + 4096;
  gRootDiagnostic.stage = "policy allocation";
  if (kmem_alloc (kernel_map, &verifiedStorage, verifiedStorageBytes,
                  static_cast<kma_flags_t> (KMA_KOBJECT | KMA_DATA), VM_KERN_MEMORY_SECURITY) != KERN_SUCCESS) {
    return false;
  }
  memcpy (reinterpret_cast<void *> (verifiedStorage), manifest, manifestBytes);
  uint8_t *key = reinterpret_cast<uint8_t *> (verifiedStorage + tableBytes);
  memcpy (key, verified_root_key, VR_KEY_BYTES);
  gRootDiagnostic.stage = "policy read-only protection";
  if (vm_map_protect (kernel_map, verifiedStorage, verifiedStorage + verifiedStorageBytes,
                      VM_PROT_READ, TRUE) != KERN_SUCCESS) {
    return false;
  }
  gRootDiagnostic.stage = "owner signature and hash table";
  int status = vr_open (reinterpret_cast<const uint8_t *> (verifiedStorage), manifestBytes, key, 1,
                        &vr_xnu_crypto, &verified);
  gRootDiagnostic.verify = static_cast<uint32_t> (status);
  // The relocated grouped window has its own signed manifest; the on-disk SHA record still names the original window.
  return status == VR_OK && verified.image_bytes == bytes && (gGroupWindow || memcmp(verified.image_sha256,expectedImageHash,32)==0);
}

bool NativeUfsBlock::authenticatesVolume (const char *uuid, uint8_t *manifestDigest) const
{
  if (!uuid || !online || !verifiedStorage || !verified.hashes) {
    return false;
  }
  static const char hex[] = "0123456789ABCDEF";
  char expected[37];
  unsigned at = 0;
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      expected[at++] = '-';
    }
    expected[at++] = hex[verified.volume_uuid[i] >> 4];
    expected[at++] = hex[verified.volume_uuid[i] & 15];
  }
  expected[at] = 0;
  if (strcmp (uuid, expected)) {
    return false;
  }
  if (manifestDigest) {
    // Hash only the authenticated envelope, excluding allocation padding and the key.
    const uint64_t envelope = VR_HEADER_BYTES + VR_SIGNATURE_BYTES + verified.block_count * 32;
    SHA384_CTX context;
    SHA384_Init (&context);
    SHA384_Update (&context, reinterpret_cast<const void *> (verifiedStorage), envelope);
    SHA384_Final (manifestDigest, &context);
  }
  return true;
}

bool NativeUfsBlock::signedVolumeUuid (char out[37]) const
{
  if (!verifiedStorage || !verified.hashes) {
    return false;
  }
  static const char hex[] = "0123456789ABCDEF";
  unsigned at = 0;
  for (unsigned i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out[at++] = '-';
    }
    out[at++] = hex[verified.volume_uuid[i] >> 4];
    out[at++] = hex[verified.volume_uuid[i] & 15];
  }
  out[at] = 0;
  return true;
}

// Called from the root authorization query in NativeVirtioBlock: -1 when `provider` is not a
// UFS window, otherwise 1 if its signed volume UUID equals `uuid`, else 0.
extern "C" int native_ufs_block_authenticates (IOService *provider, const char *uuid, uint8_t *digest)
{
  NativeUfsBlock *block = OSDynamicCast (NativeUfsBlock, provider);
  if (!block) {
    return -1;
  }
  return block->authenticatesVolume (uuid, digest) ? 1 : 0;
}
#endif

bool NativeUfsBlock::start (IOService *provider)
{
  if (!bounce || !windowLock || !IOBlockStorageDevice::start (provider)) {
    return false;
  }
  online = true;
  registerService ();
  return true;
}

IOReturn NativeUfsBlock::doAsyncReadWrite (IOMemoryDescriptor *buffer, UInt64 block, UInt64 count,
                                           IOStorageAttributes *, IOStorageCompletion *completion)
{
  if (!buffer || !completion || !completion->action || !count || block >= bytes / 512 ||
      count > bytes / 512 - block || buffer->getLength () < count * 512) {
    return kIOReturnBadArgument;
  }
  if (buffer->getDirection () != kIODirectionIn) {
    return kIOReturnNotWritable;
  }
  if (!online) {
    return kIOReturnNotReady;
  }
  struct Operations {
    IOLock *mutex;
    IOMemoryDescriptor *buffer;
#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
    const vr_view *view;
#endif
    void lock () { IOLockLock (mutex); }
    void unlock () { IOLockUnlock (mutex); }
    IOReturn read (uint64_t lba, uint32_t blocks, void *out) {
      uint64_t t0 = timerTicks ();
      IOReturn r = hostRead4k (0, lba, blocks, out);
      __atomic_fetch_add (&native_ufs_read_ticks, timerTicks () - t0, __ATOMIC_RELAXED);
      return r;
    }
    IOReturn verify (uint64_t index, const void *data, size_t length) {
#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
      uint64_t t0 = timerTicks ();
      int status = vr_verify_block (view, index, data, length, &vr_xnu_crypto);
      __atomic_fetch_add (&native_ufs_verify_ticks, timerTicks () - t0, __ATOMIC_RELAXED);
      if (status != VR_OK) {
        gRootDiagnostic.stage = "read block integrity";
        gRootDiagnostic.verify = static_cast<uint32_t> (status);
        __atomic_fetch_add (&native_ufs_integrity_failures, 1, __ATOMIC_RELAXED);
        return kIOReturnNotPrivileged;
      }
      __atomic_fetch_add (&native_ufs_verified_blocks, 1, __ATOMIC_RELAXED);
      // No optional progress callback in this device adapter.
#else
      (void) index; (void) data; (void) length;
#endif
      return kIOReturnSuccess;
    }
    uint64_t copy (uint64_t offset, const void *data, uint64_t length) {
      uint64_t copied = buffer->writeBytes (offset, data, length);
      if (copied == length) __atomic_fetch_add (&native_ufs_block_reads, 1, __ATOMIC_RELAXED);
      return copied;
    }
  } operations = { windowLock, buffer
#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
    , &verified
#endif
  };
  UfsWindowResult outcome = ufsReadWindow (operations, absolute, bytes, block, count, bounce);
  IOReturn result = outcome.status;
  uint64_t done = outcome.done;
  if (result != kIOReturnSuccess) {
    __atomic_fetch_add (&native_ufs_block_failures, 1, __ATOMIC_RELAXED);
    gRootDiagnostic.result = static_cast<uint32_t> (result);
    rootDiagnosticFailure ("root window read failed");
  }
  completion->action (completion->target, completion->parameter, result, done);
  return kIOReturnSuccess;
}

void NativeUfsBlock::free ()
{
#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
  if (verifiedStorage) {
    kmem_free (kernel_map, verifiedStorage, verifiedStorageBytes);
    verifiedStorage = 0;
  }
#endif
  if (bounce) {
    IOFree (bounce, kDataMax);
    bounce = nullptr;
  }
  if (windowLock) {
    IOLockFree (windowLock);
    windowLock = nullptr;
  }
  IOBlockStorageDevice::free ();
}

// Ordered publication of the root windows. The original owner-trust code resolves the SystemOS
// volume by UUID immediately after the root is mounted, so SystemOS must already have published its
// volume media by then; the OS window (whose volume is the root being waited for) goes last.
static bool p12RootMatchingUuid(char out[37])
{
  IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
  if(!chosen)return false;
  OSData *data=OSDynamicCast(OSData,chosen->getProperty("root-matching"));
  OSObject *object=nullptr;bool valid=false;
  if(data&&data->getLength()>0&&data->getLength()<=4096&&
      static_cast<const char*>(data->getBytesNoCopy())[data->getLength()-1]==0){
    object=OSUnserializeXML(static_cast<const char*>(data->getBytesNoCopy()));
    OSDictionary *dict=OSDynamicCast(OSDictionary,object);
    OSDictionary *properties=dict?OSDynamicCast(OSDictionary,dict->getObject("IOPropertyMatch")):nullptr;
    OSString *wanted=properties?OSDynamicCast(OSString,properties->getObject("UUID")):nullptr;
    if(wanted&&wanted->getLength()==36){strlcpy(out,wanted->getCStringNoCopy(),37);valid=true;}
  }
  OSSafeReleaseNULL(object);chosen->release();return valid;
}
static bool p12RootMatchingEquals(const char *uuid)
{
 IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
 if(!chosen)return false;
 OSData *data=OSDynamicCast(OSData,chosen->getProperty("root-matching"));
 OSObject *object=nullptr;bool valid=false;
 if(data&&data->getLength()>0&&data->getLength()<=4096&&
    static_cast<const char*>(data->getBytesNoCopy())[data->getLength()-1]==0){
  object=OSUnserializeXML(static_cast<const char*>(data->getBytesNoCopy()));
  OSDictionary *dict=OSDynamicCast(OSDictionary,object);
  OSString *provider=dict?OSDynamicCast(OSString,dict->getObject("IOProviderClass")):nullptr;
  OSDictionary *properties=dict?OSDynamicCast(OSDictionary,dict->getObject("IOPropertyMatch")):nullptr;
  OSString *wanted=properties?OSDynamicCast(OSString,properties->getObject("UUID")):nullptr;
  valid=provider&&provider->isEqualTo("IOMedia")&&wanted&&wanted->isEqualTo(uuid);
 }
 OSSafeReleaseNULL(object);chosen->release();return valid;
}
static NativeUfsBlock *gPendingDevice[4];
static unsigned gPending;
static bool gPendingAttached[4];
static char gPendingUuid[4][37];
static UfsRootPlan gPublicationPlan;
static NativeUfs *gPendingProvider;

static bool mediaWithUuidExists (const char *uuid, NativeUfsBlock *window)
{
  OSDictionary *matching = IOService::serviceMatching ("IOMedia");
  OSDictionary *properties = OSDictionary::withCapacity (1);
  OSString *wanted = OSString::withCString (uuid);
  bool found = false;
  if (matching && properties && wanted && properties->setObject ("UUID", wanted) &&
      matching->setObject ("IOPropertyMatch", properties)) {
    OSIterator *iterator = IOService::getMatchingServices (matching);
    if (iterator) {
      for (unsigned matches = 0; matches < 64 && !found; ++matches) {
        IORegistryEntry *media = OSDynamicCast (IORegistryEntry, iterator->getNextObject ());
        if (!media) break;
        IORegistryIterator *parents = IORegistryIterator::iterateOver (
          media, gIOServicePlane, kIORegistryIterateRecursively | kIORegistryIterateParents);
        if (parents) {
          found = ufsMediaHasAncestor (*parents, window);
          parents->release ();
        }
      }
      iterator->release ();
    }
  }
  OSSafeReleaseNULL (wanted);
  OSSafeReleaseNULL (properties);
  OSSafeReleaseNULL (matching);
  return found;
}

static void captureMissingMedia (const char *uuid)
{
  int used = snprintf (gRootMediaText, sizeof (gRootMediaText), "Waiting for signed UUID %s\n", uuid);
  OSDictionary *matching = IOService::serviceMatching ("IOMedia");
  OSIterator *iterator = matching ? IOService::getMatchingServices (matching) : nullptr;
  unsigned count = 0;
  while (iterator && used < int (sizeof (gRootMediaText)) - 180 && count++ < 8) {
    IOService *media = OSDynamicCast (IOService, iterator->getNextObject ());
    if (!media) break;
    OSString *id = OSDynamicCast (OSString, media->getProperty ("UUID"));
    OSString *bsd = OSDynamicCast (OSString, media->getProperty (kIOBSDNameKey));
    OSNumber *size = OSDynamicCast (OSNumber, media->getProperty ("Size"));
    used += snprintf (gRootMediaText + used, sizeof (gRootMediaText) - used,
                      "media %.24s bytes=%llx uuid=%.36s\n",
                      bsd ? bsd->getCStringNoCopy () : "-", size ? size->unsigned64BitValue () : 0ULL,
                      id ? id->getCStringNoCopy () : "-");
  }
  OSSafeReleaseNULL (iterator);
  OSSafeReleaseNULL (matching);
}

// Used only for objects that were never started/published. Started objects
// retain their owner references even on a later failure; clients can have I/O.
static void discardPendingWindow (unsigned index)
{
  NativeUfsBlock *device = gPendingDevice[index];
  if (!device) return;
  if (gPendingAttached[index]) device->detach (gPendingProvider);
  gPendingAttached[index] = false;
  gPendingDevice[index] = nullptr;
  device->release ();
}

static void publishThread (void *, wait_result_t)
{
  struct Operations {
    bool start (unsigned index) {
      NativeUfsBlock *device = gPendingDevice[index];
      gRootDiagnostic.stage = "root window service start";
      gRootDiagnostic.publicationPhase = static_cast<unsigned> (UfsRootPhase::Start);
      gRootDiagnostic.publicationWindow = index;
      if (!device->start (gPendingProvider)) return false;
      native_ufs_blocks_published = native_ufs_blocks_published + 1;
      IOLog ("NativeUfs: published %s\n", device->windowLabel ());
      return true;
    }
    bool wait (unsigned index) {
      NativeUfsBlock *device = gPendingDevice[index];
      const char *uuid = gPendingUuid[index];
      gRootDiagnostic.stage = "media publication";
      gRootDiagnostic.publicationPhase = static_cast<unsigned> (UfsRootPhase::Media);
      gRootDiagnostic.publicationWindow = index;
      bool ready = false;
      // Diagnostic mode affects reporting only, never the dependency gate.
      for (unsigned tick = 0; tick <= 300; ++tick) {
        ready = mediaWithUuidExists (uuid, device);
        if (ready || tick == 300) break;
        IOSleep (100);
      }
      IOLog ("NativeUfs: %s volume %s %s\n", device->windowLabel (), uuid, ready ? "ready" : "not ready");
      if (!ready) captureMissingMedia (uuid);
      return ready;
    }
    void discard (unsigned index) { discardPendingWindow (index); }
  } operations;
  UfsRootPublicationResult result = ufsPublishRootWindows (operations, gPublicationPlan);
  if (result.phase != UfsRootPhase::Complete) {
    gRootDiagnostic.result = result.phase == UfsRootPhase::Start ? kIOReturnIOError : kIOReturnTimeout;
    const char *reason = result.phase == UfsRootPhase::Start ? "root window service start failed" :
      (!strcmp (gPendingDevice[result.index]->windowLabel (), "SystemOS") ?
       "SystemOS signed volume not found" : "signed root window volume not found");
    IOLog ("NativeUfs: root publisher stopped at window %u; published=%u\n", result.index, result.published);
    // Start failures have already released their unpublished object; do not
    // dereference it here. The media-failure object is deliberately retained.
    rootDiagnosticFailure (reason);
  }
  gPendingProvider->release ();  // Balanced with successful thread dispatch.
}

bool NativeUfs::prepareRootWindows ()
{
  if(gRootHost && gRootHost!=this)return false;
  gRootHost=this;const char *why="";
  RootLayout *layout = static_cast<RootLayout *> (IOMalloc (sizeof (RootLayout)));
  if (!layout || !findRootLayout (layout, &why)) {
    rootDiagnosticFailure (layout ? why : "root layout allocation failed");
    IOLog ("NativeUfs: root layout failed: %s\n", why);
    if (layout) IOFree (layout, sizeof (RootLayout));
    return false;
  }

  if (layout->manifestStatus != 0) {
    rootDiagnosticFailure ("root manifest rejected");
    IOLog ("NativeUfs: no valid root manifest in userdata (status %u, magic %08x %08x)\n",
           layout->manifestStatus, layout->magic0, layout->magic1);
    IOFree (layout, sizeof (RootLayout));
    return false;
  }
  int group = 0;
  if (PE_parse_boot_argn ("apfs-group", &group, sizeof group) && group == 1) {
    /* The grouped container keeps the OS volume UUID but lives after the log
     * window; relocate the OS window without touching the on-disk manifest. */
    for (unsigned i = 0; i < layout->windows; ++i) {
      if (!strcmp (layout->window[i].name, "OS")) {
        layout->window[i].offset = 0x400000000ULL; /* 16 GiB */
        layout->window[i].size = 0x300000000ULL;   /* 12 GiB */
        layout->imageBytes = 0x700000000ULL;       /* 28 GiB */
        gGroupWindow = true;
        IOLog ("NativeUfs: apfs-group window override active\n");
        for (unsigned i = 0; i < layout->windows; ++i) {
          IOLog ("NativeUfs: window %s offset=%llx size=%llx\n", layout->window[i].name,
                 layout->window[i].offset, layout->window[i].size);
        }
        break;
      }
    }
  }
  const uint64_t logOffset=(CB_LOG_FIRST-CB_LOG_USER_FIRST)*4096ULL;
  bool logSafe=layout->userdataFirst==CB_LOG_USER_FIRST&&layout->userdataLast==CB_LOG_USER_LAST&&layout->imageBytes;
  for(unsigned i=0;i<layout->windows;++i){uint64_t a=layout->window[i].offset,n=layout->window[i].size;if(a+n<a||!(a+n<=logOffset||a>=logOffset+CB_LOG_BLOCKS*4096ULL))logSafe=false;}
  gLogLayoutSafe=logSafe; // Signed layout must remain outside the reserved log window.
  const char *names[4] = {};
  for (unsigned i = 0; i < layout->windows; ++i) names[i] = layout->window[i].name;
  if (!ufsBuildRootPlan (names, layout->windows, gPublicationPlan)) {
    rootDiagnosticFailure ("root windows require distinct OS and SystemOS");
    IOFree (layout, sizeof (RootLayout));
    return false;
  }
  // Prepare all windows or publish none. Diagnostics never decide policy.
  gPending = 0;
  gPendingProvider = this;
  struct Operations {
    NativeUfs *owner;
    RootLayout *layout;
    bool prepare (unsigned index) {
      gRootDiagnostic.stage = "root window allocation/configuration";
      gRootDiagnostic.publicationPhase = static_cast<unsigned> (UfsRootPhase::Prepare);
      gRootDiagnostic.publicationWindow = index;
      gRootDiagnostic.windowBytes = layout->window[index].size;
      NativeUfsBlock *device = OSTypeAlloc (NativeUfsBlock);
      gPendingDevice[index] = device;
      gPendingAttached[index] = false;
      gPendingUuid[index][0] = 0;
      uint64_t absolute = layout->userdataFirst * 4096ULL + layout->window[index].offset;
      if (!device || !device->init (static_cast<OSDictionary *> (nullptr)) ||
          !device->configure (layout->window[index].name, absolute, layout->window[index].size,layout->window[index].sha256)) return false;
      gRootDiagnostic.stage = "root window attachment";
      if (!device->attach (owner)) return false;
      gPendingAttached[index] = true;
      ++gPending;
      gRootDiagnostic.prepared = gPending;
      return true;
    }
    bool validate () {
      gRootDiagnostic.stage = "signed window identities";
      gRootDiagnostic.publicationPhase = static_cast<unsigned> (UfsRootPhase::Identity);
#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK
      for (unsigned i = 0; i < gPending; ++i) {
        gRootDiagnostic.publicationWindow = i;
        if (gGroupWindow && !strcmp (gPendingDevice[i]->windowLabel (), "OS")) {
          /* The relocated window has no per-block policy yet; its root identity
           * is the volume UUID the device tree already selects for root. */
          if (!p12RootMatchingUuid (gPendingUuid[i])) {
            IOLog ("NativeUfs: identity OS uuid unavailable from /chosen\n");
            return false;
          }
        } else if (!gPendingDevice[i]->signedVolumeUuid (gPendingUuid[i])) {
          IOLog ("NativeUfs: identity %s volume uuid unavailable\n", gPendingDevice[i]->windowLabel ());
          return false;
        }
        if (!strcmp(gPendingDevice[i]->windowLabel(),"OS") && !p12RootMatchingEquals (gPendingUuid[i])) {
          IOLog ("NativeUfs: identity OS uuid %s is not the root-matching volume\n", gPendingUuid[i]);
          return false;
        }
        IOLog ("NativeUfs: identity %s uuid %s\n", gPendingDevice[i]->windowLabel (), gPendingUuid[i]);
        for (unsigned j = 0; j < i; ++j) if (!strcmp (gPendingUuid[i], gPendingUuid[j])) return false;
      }
      return true;
#else
      return false; // Root publication requires owner verification in every build.
#endif
    }
    bool schedule () {
      gRootDiagnostic.stage = "root publisher thread";
      gRootDiagnostic.publicationPhase = static_cast<unsigned> (UfsRootPhase::Schedule);
      gRootDiagnostic.publicationWindow = 0xffffffff;
      thread_t thread = nullptr;
      owner->retain ();
      if (kernel_thread_start (publishThread, nullptr, &thread) != KERN_SUCCESS) {
        owner->release ();
        return false;
      }
      thread_deallocate (thread);
      return true;
    }
    void discard (unsigned index) { discardPendingWindow (index); }
  } operations = { this, layout };
  UfsRootPublicationResult result = ufsPrepareRootWindows (operations, layout->windows);
  IOFree (layout, sizeof (RootLayout));
  if (result.phase != UfsRootPhase::Complete) {
    gPending = 0;
    gRootDiagnostic.result = result.phase == UfsRootPhase::Identity ? kIOReturnNotPrivileged :
      result.phase == UfsRootPhase::Schedule ? kIOReturnNoResources : kIOReturnError;
    IOLog ("NativeUfs: root preparation failed phase=%u window=%u; nothing published\n",
           static_cast<unsigned> (result.phase), result.index);

    rootDiagnosticFailure (result.phase == UfsRootPhase::Schedule ? "root publisher thread creation failed" :
                          result.phase == UfsRootPhase::Identity ? "signed window identities rejected" :
                          "root window preparation failed");
    return false;
  }
  return true;
}
extern "C" {
extern kmod_info_t g_kernel_kmod_info;
kern_return_t OSKextLoadKextWithIdentifier(const char*);
void p11_ufs_static_init(void) __asm__("__GLOBAL__sub_I_NativeUfs.cpp");
void cheeseburger_ufs_register(void) {
 static bool loaded=false;if(loaded)return;
 if(OSKextLoadKextWithIdentifier("com.apple.iokit.IOStorageFamily"))panic("P11 UFS: original storage load failed");
 const OSSymbol *key=OSSymbol::withCString("IOBlockStorageDevice");
 const OSMetaClass *base=key?OSMetaClass::getMetaClassWithName(key):nullptr;OSSafeReleaseNULL(key);
 if(!base||base!=&IOBlockStorageDevice::gMetaClass||base->getClassSize()!=sizeof(IOBlockStorageDevice))panic("P11 UFS: original parent ABI mismatch");
 void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
 if(!handle)panic("P11 UFS: preModLoad failed");
 p11_ufs_static_init();
 if(OSMetaClass::postModLoad(handle))panic("P11 UFS: class registration failed");
 loaded=true;IOLog("P11 UFS: NativeUfs and NativeUfsLun registered against original storage; read-only\n");
}
}
