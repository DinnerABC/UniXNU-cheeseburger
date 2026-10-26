// SPDX-License-Identifier: MIT
// Device-scoped Apple interface compatibility; not a Qualcomm PMIC backend.
// ABI/lifetime reference: UniXNU-sheng/package/ShengPlatformPower.cpp (MIT).
#include "OriginalPMGRABI.h"
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/libkern.h>
#include <mach/kmod.h>
extern "C" {
extern kmod_info_t g_kernel_kmod_info;
kern_return_t OSKextLoadKextWithIdentifier(const char *);
void cheeseburger_pmgr_static_init(void) __asm__("__GLOBAL__sub_I_CheeseburgerPlatformPower.cpp");
IOService *sheng_original_pmgr_nub_create(IORegistryEntry *, ApplePMGR *);
uint32_t sheng_original_pmgr_nub_read(IOService *);
void sheng_original_pmgr_nub_write(IOService *, uint64_t);
volatile uint64_t p11_pmgr_reads=0, p11_pmgr_writes=0;
}
static bool ready=false, classReady=false;
__attribute__((noreturn)) static void unavailable(unsigned slot) {
 panic("CheeseburgerPlatformPower: Qualcomm hardware operation %u unsupported",slot);
}
class CheeseburgerPlatformPower : public ApplePMGR {
 OSDeclareDefaultStructors(CheeseburgerPlatformPower)
 IOService *nub=nullptr;
public:
 bool start(IOService *provider) override;
 void stop(IOService *provider) override;
 void free() override;
 void enableCPUCore(unsigned,uint64_t) override { unavailable(168); }
 void enableCPUCore(unsigned) override { unavailable(169); }
 void disableCPUCore(unsigned) override { unavailable(170); }
 void enableCPUCluster(unsigned) override { unavailable(171); }
 void disableCPUCluster(unsigned) override { unavailable(172); }
 void initCPUIdle(ml_processor_info_t*) override { unavailable(173); }
 void enterCPUIdle(UInt64*) override { unavailable(174); }
 void exitCPUIdle(UInt64*) override { unavailable(175); }
 void updateCPUIdle(UInt64*) override { unavailable(176); }
 uint32_t readAon() override {
  uint32_t value=ApplePMGR::readAon();
  if(value!=0xffffffffU)panic("P11: original unsupported AON contract changed");
  __sync_fetch_and_add(&p11_pmgr_reads,1);
  IOLog("P11 PMGR compat READ unsupported=%x caller=%p (not hardware)\n",value,__builtin_return_address(0));
  return value;
 }
 void writeAon(uint64_t value) override {
  // The unchanged target's unsupported sentinel OR 4 remains the sentinel.
  // No successful security latch, persistent state or hardware write is asserted.
  if(value!=0xffffffffULL)panic("P11 PMGR: unsupported AON request %llx",value);
  ApplePMGR::writeAon(value);
  __sync_fetch_and_add(&p11_pmgr_writes,1);
  IOLog("P11 PMGR compat WRITE request=%llx caller=%p (original no-hardware semantics)\n",value,__builtin_return_address(0));
 }
#include "GuardedPMGRSlots.inc"
};
OSDefineMetaClassWithInit(CheeseburgerPlatformPower,ApplePMGR,)
OSMetaClassConstructorInit(CheeseburgerPlatformPower,ApplePMGR,)
OSDefineBasicStructors(CheeseburgerPlatformPower,ApplePMGR)
void *CheeseburgerPlatformPower::operator new(size_t size) { return OSObject::operator new(size); }
void CheeseburgerPlatformPower::operator delete(void *p,size_t n) { OSObject::operator delete(p,n); }
static const OSMetaClass *lookup(const char *s) {
 const OSSymbol *key=OSSymbol::withCString(s);
 const OSMetaClass *result=key?OSMetaClass::getMetaClassWithName(key):nullptr;
 OSSafeReleaseNULL(key);return result;
}
bool CheeseburgerPlatformPower::start(IOService *provider) {
 if(ready||!provider||!provider->metaCast("IOPlatformExpertDevice"))return false;
 // The OS profile owns model (iPad7,1); board matching must survive that identity.
 // A present marker is authoritative. Only legacy ADTs may fall back to model.
 OSObject *boardProperty=provider->getProperty("unixnu,board-id");
 if(boardProperty){
  OSData *board=OSDynamicCast(OSData,boardProperty);
  static const char boardId[]="oneplus,cheeseburger";
  if(!board||board->getLength()!=sizeof(boardId)||
     memcmp(board->getBytesNoCopy(),boardId,sizeof(boardId)))return false;
 }else{
  OSData *model=OSDynamicCast(OSData,provider->getProperty("model"));
  if(!model||model->getLength()!=9||memcmp(model->getBytesNoCopy(),"OnePlus5",9))return false;
 }
 if(!IOService::start(provider))return false;
 setProperty("IOClass","CheeseburgerPlatformPower");
 setProperty("IOMatchCategory","CheeseburgerPlatformPower");
 setProperty("AppleInterfaceCompatibilityOnly",true);
 setProperty("QualcommHardwarePowerSupported",false);
 setProperty("AONBackend","original-unsupported-contract; no-hardware-state");
 // The original init(old,gIODTPlane) TRANSFERS old's DT relationships.
 // Never pass the real platform/root/arm-io entry as its description.
 unsigned dtChildren=provider->getChildCount(gIODTPlane);
 if(!dtChildren)panic("P11 PMGR: platform DT absent before nub creation");
 {
  IORegistryEntry *description=new IORegistryEntry;
  if(!description||!description->init()){
   OSSafeReleaseNULL(description);IOService::stop(provider);return false;
  }
  description->setName("cheeseburger-platform-power");
  description->setProperty("AppleInterfaceCompatibilityOnly",true);
  nub=sheng_original_pmgr_nub_create(description,this);
  description->release();
 }
 if(provider->getChildCount(gIODTPlane)!=dtChildren)panic("P11 PMGR: platform DT relationships changed");
 IOLog("P11 PMGR: preserved platform DT children=%u\n",dtChildren);
 const OSMetaClass *nm=lookup("ApplePMGRNub");uintptr_t bound=0;
 if(!nub||!nm||nub->getMetaClass()!=nm||nm->getClassSize()!=0x740)goto fail;
 memcpy(&bound,reinterpret_cast<const char*>(nub)+0x88,sizeof(bound));
 if(bound!=reinterpret_cast<uintptr_t>(this)||!nub->attach(this))goto fail;
 if(!nub->start(this)){nub->detach(this);goto fail;}
 nub->setProperty("QualcommHardwarePowerSupported",false);
 nub->setProperty("AppleInterfaceCompatibilityOnly",true);
 registerService(kIOServiceSynchronous);
 nub->registerService(kIOServiceSynchronous);
 {
  OSDictionary *match=IOService::serviceMatching("ApplePMGRNub");
  IOService *found=match?IOService::waitForMatchingService(match,1000000):nullptr;
  OSSafeReleaseNULL(match);
  bool valid=found==nub && nub->getProvider()==this &&
   (nub->getState()&kIOServiceRegisteredState) && (getState()&kIOServiceRegisteredState);
  OSSafeReleaseNULL(found);
  if(!valid)panic("P11 PMGR: original nub publication mismatch");
 }
 ready=true;
 IOLog("P11 PMGR: original ApplePMGRNub registered; backend=unsupported-contract; no Qualcomm power claim\n");
 return true;
fail:
 OSSafeReleaseNULL(nub);IOService::stop(provider);return false;
}
void CheeseburgerPlatformPower::stop(IOService *p) {
 if(nub){nub->terminate(kIOServiceSynchronous);nub->stop(this);nub->detach(this);nub->release();nub=nullptr;}
 ready=false;IOService::stop(p);
}
void CheeseburgerPlatformPower::free() { if(nub)panic("P11 PMGR: free with live nub");IOService::free(); }
extern "C" void cheeseburger_pmgr_startup(void) {
 if(ready)return;
 if(OSKextLoadKextWithIdentifier("com.apple.driver.ApplePMGR"))panic("P11 PMGR: original dependency not loaded");
 auto parent=lookup("ApplePMGR");
 if(!parent||parent!=&ApplePMGR::gMetaClass||parent->getClassSize()!=0x4b8a8)panic("P11 PMGR: original base ABI mismatch");
 if(!classReady){
  void *handle=OSMetaClass::preModLoad(g_kernel_kmod_info.name);
  if(!handle)panic("P11 PMGR: preModLoad failed");
  cheeseburger_pmgr_static_init();
  if(OSMetaClass::postModLoad(handle))panic("P11 PMGR: metaclass registration failed");
  classReady=true;
 }
 // Explicit early platform attachment before AMFI's asynchronous callback.
 // Same exact provider/name/category as KernelConfigTables; later duplicate starts refuse.
 IOService *root=IOService::getServiceRoot();
 auto power=new CheeseburgerPlatformPower;
 if(!root||!power||!power->init())panic("P11 PMGR: allocation failed");
 power->setProperty("IOMatchCategory","CheeseburgerPlatformPower");
 power->setProperty("IOClass","CheeseburgerPlatformPower");
 if(!power->attach(root))panic("P11 PMGR: root attachment failed");
 if(!power->start(root)){power->detach(root);power->release();panic("P11 PMGR: device provider refused");}
 power->release(); // provider/registry and original nub retain the live hierarchy
}
