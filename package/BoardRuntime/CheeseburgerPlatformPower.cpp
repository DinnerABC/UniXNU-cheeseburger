// SPDX-License-Identifier: MIT
// Device-scoped Apple interface compatibility; not a Qualcomm PMIC backend.
// ABI/lifetime reference: UniXNU-sheng/package/ShengPlatformPower.cpp (MIT).
#include "OriginalPMGRABI.h"
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
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
#if defined(UNIXNU_BOARD_ABI_20H392) && UNIXNU_BOARD_ABI_20H392
  // 20H392 readAon is an empty body (0x5681de0: ret), so the original returns
  // the caller's x0 (this) rather than the 21H461 -1 sentinel: no value gate.
#else
  if(value!=0xffffffffU)panic("P11: original unsupported AON contract changed");
#endif
  __sync_fetch_and_add(&p11_pmgr_reads,1);
  IOLog("P11 PMGR compat READ unsupported=%x caller=%p (not hardware)\n",value,__builtin_return_address(0));
  return value;
 }
 void writeAon(uint64_t value) override {
  // The unchanged target's unsupported sentinel OR 4 remains the sentinel.
  // No successful security latch, persistent state or hardware write is asserted.
#if defined(UNIXNU_BOARD_ABI_20H392) && UNIXNU_BOARD_ABI_20H392
  // 20H392 writeAon is an empty body (0x5681de4: ret): the original drops the
  // request without latching anything, so no request-value gate is applied.
#else
  if(value!=0xffffffffULL)panic("P11 PMGR: unsupported AON request %llx",value);
#endif
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
extern "C" volatile unsigned cheeseburger_unattended_remaining;
extern "C" int native_ufs_log_force_note(const char *) __attribute__((weak_import));
static void cb_reset_note(const char *text){ if (&native_ufs_log_force_note && text) native_ufs_log_force_note(text); }
extern "C" volatile unsigned boot_log_commits;
extern "C" volatile int cheeseburger_recovery_armed;
extern "C" void p8_console_char(char);
extern int (*PE_halt_restart)(unsigned int);
static void unattended_console_line(const char *text);
static uint64_t cb_psci(uint64_t fid,uint64_t a1) {
 register uint64_t x0 __asm__("x0")=fid,x1 __asm__("x1")=a1,x2 __asm__("x2")=0,x3 __asm__("x3")=0;
 __asm__ volatile("smc #0" : "+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3) :: "memory","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17");
 return x0;
}
static uint64_t cb_boot_ticks=0,cb_freq=0;
static unsigned cb_total_seconds=180;
/* Panic funnel hold (osfmk/kern/debug.c): keep the other CPUs running so the persistent log
 * writer commits the panic, then reset. 0 keeps the stock debugger path. */
extern "C" uint32_t board_panic_hold_seconds=0;
static volatile bool cb_panic_parked=false;
extern "C" void board_panic_reset(void) {
 __atomic_store_n(&cb_panic_parked,true,__ATOMIC_RELEASE);
 /* The ML hold already kept this screen for cheeseburger-panic-hold seconds.
  * A parked CPU is unrecoverable when cpus=1 (the countdown thread would never
  * run again), so every panic ends in PSCI from here. The note is skipped:
  * rawWrite4k can block on a storage lock the panic may have left held. */
 unattended_console_line("CB panic: hold done; SYSTEM_RESET\n");
 for (;;) {
  uint64_t rc=cb_psci(0x84000009UL,0); /* PSCI SYSTEM_RESET */
  char line[64]; int n=snprintf(line,sizeof line,"CB panic reset: SYSTEM_RESET returned %llx\n",rc);
  for(int i=0;i<n;i++) p8_console_char(line[i]);
  for (volatile unsigned spin=0; spin<1000000; ++spin) __asm__ volatile("yield");
 }
}
static int cb_halt_restart(unsigned int type) {
 /* Only restart requests reset the SoC; halts and diagnostics keep their normal path. */
 if (type!=kPERestartCPU && type!=kPEPanicRestartCPU && type!=kPEPanicRestartCPUNoCallouts) return 0;
 /* No UFS note here: rawWrite4k can block behind an unhealthy or held storage
  * lock, which would strand every reset path before PSCI. The console line
  * below is the only evidence this code may leave. */
 if (cb_freq && cb_boot_ticks && cb_total_seconds) {
  uint64_t now=0,elapsed=0;
  do {
   __asm__ volatile("isb; mrs %0,cntpct_el0" : "=r"(now));
   elapsed=(now-cb_boot_ticks)/cb_freq;
  } while (elapsed<cb_total_seconds);
 }
 uint64_t rc=cb_psci(0x84000009UL,0); /* PSCI SYSTEM_RESET */
 { char line[80]; int n=snprintf(line,sizeof line,"CB reset: type=%u SYSTEM_RESET returned %llx\n",type,rc); for(int i=0;i<n;i++) p8_console_char(line[i]); }
 for (;;) __asm__ volatile("wfe");
}
static void unattended_console_line(const char *text) {
 for (; *text; ++text) p8_console_char(*text);
}
static void unattended_reset_thread(void *, wait_result_t) {
 int seconds=0;
 if(!PE_parse_boot_argn("cheeseburger-unattended-reset",&seconds,sizeof seconds)||seconds<=0){
  unattended_console_line("CB watchdog: no cheeseburger-unattended-reset arg\n");
  return;
 }
 { char line[48]; snprintf(line,sizeof line,"CB watchdog: %d s\n",seconds); for(int i=0;line[i];++i) p8_console_char(line[i]); }
 printf("P23 WDT start=%d\n",seconds);
 for (int left=seconds; left>0; --left) {
  cheeseburger_unattended_remaining=(unsigned)left;
  char line[48];
  int n=snprintf(line,sizeof line,"AUTO RESET in %d s\n",left);
  for (int i=0;i<n;++i) p8_console_char(line[i]);
  if (left==seconds || (left%10)==0 || left<=5) { char line[48]; snprintf(line,sizeof line,"CB watchdog: reset in %d s\n",left); for(int i=0;line[i];++i) p8_console_char(line[i]); }
  if ((left%10)==0 || left<=3) printf("P23 ALIVE left=%d\n",left);
  IOSleep(1000);
 }
 cheeseburger_unattended_remaining=0;
 if (__atomic_load_n(&cb_panic_parked,__ATOMIC_ACQUIRE)) {
  /* A panic parks its own CPU for inspection; the countdown remains the only
   * reset path, so it must still fire instead of skipping the whole boot. */
  unattended_console_line("CB watchdog: panic parked; countdown reset\n");
 }
 if (!__atomic_load_n(&boot_log_commits,__ATOMIC_ACQUIRE) || cheeseburger_recovery_armed!=1) {
  /* Reset anyway: a missing log writer or BCB must never turn into a parked
   * device. Without the BCB the next pass may land in the normal system
   * instead of Recovery, which is still recoverable by the operator. */
  unattended_console_line("CB watchdog: no log/BCB; countdown reset\n");
 }
 unattended_console_line("CB watchdog: PSCI SYSTEM_RESET\n");
 cb_halt_restart(kPEPanicRestartCPU);
}
volatile unsigned cheeseburger_unattended_remaining = 0;

extern "C" void cheeseburger_unattended_reset_start(void) {
 static bool started=false;if(started)return;
 /* Parse the panic hold here as well: bsd_init calls this before PMGR, and an
  * early panic must still keep its screen/log before the reset. */
 { int hold=0;
   if(PE_parse_boot_argn("cheeseburger-panic-hold",&hold,sizeof hold)&&hold>0&&hold<=120)board_panic_hold_seconds=(uint32_t)hold; }
 int seconds=0;
 if(!PE_parse_boot_argn("cheeseburger-unattended-reset",&seconds,sizeof seconds)||seconds<=0)return;
 thread_t thread=nullptr;
 if(kernel_thread_start(unattended_reset_thread,nullptr,&thread)==KERN_SUCCESS){started=true;thread_deallocate(thread);}
}
extern "C" void cheeseburger_pmgr_startup(void) {
 if(ready)return;
 {
  int seconds=0;
  if(PE_parse_boot_argn("cheeseburger-unattended-reset",&seconds,sizeof seconds)&&seconds>0)cb_total_seconds=(unsigned)seconds;
  int hold=0;
  if(PE_parse_boot_argn("cheeseburger-panic-hold",&hold,sizeof hold)&&hold>0&&hold<=120)board_panic_hold_seconds=(uint32_t)hold;
  __asm__ volatile("isb; mrs %0,cntpct_el0" : "=r"(cb_boot_ticks));
  __asm__ volatile("isb; mrs %0,cntfrq_el0" : "=r"(cb_freq));
 }
 PE_halt_restart=cb_halt_restart;
 {
  uint64_t v=cb_psci(0x84000000UL,0);
  int64_t feature=(int64_t)cb_psci(0x8400000aUL,0x84000009UL);
  char line[96]; int n=snprintf(line,sizeof line,"CB reset: psci v%llx reset_feature=%lld\n",v,feature);
  for(int i=0;i<n;i++) p8_console_char(line[i]);
 }
 extern void boot_log_tick(unsigned);
 boot_log_tick(0);
 cheeseburger_unattended_reset_start();
 if(OSKextLoadKextWithIdentifier("com.apple.driver.ApplePMGR"))panic("P11 PMGR: original dependency not loaded");
 auto parent=lookup("ApplePMGR");
 if(!parent||parent!=&ApplePMGR::gMetaClass||parent->getClassSize()!=sizeof(ApplePMGR))panic("P11 PMGR: original base ABI mismatch");
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
