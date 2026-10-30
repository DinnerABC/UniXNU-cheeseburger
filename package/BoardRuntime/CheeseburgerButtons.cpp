// MSM8998/PM8998 physical key adapter. Board facts: Linux v6.12 device trees.
// SPMI v2/v3 observer READ transactions only; no PMIC/IRQ/clock/reset configuration.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
#include "CheeseburgerKeys.h"
extern "C" {
extern volatile uint32_t native_hid_input_command;
volatile uint64_t cb_keys[13]; // stage,version,reads,failures,raw,mask,edges,sink-deliveries,APIDs x3,last status
// Read-only touch-rail snapshot: stage, then apid/vset/enable for pm8998 l6 and
// l28, then the per-rail read errors. Nothing here writes the PMIC.
volatile uint64_t cb_rails[24];
// Raw arbiter view so the map format can be decoded offline: [0..7] core
// registers, [8..23] the first 16 map words at core+0x800.
volatile uint64_t cb_pmic_dump[24];
static void (*sink)(void *,uint32_t);
static void *sinkContext;
void native_buttons_set_sink(void (*fn)(void *,uint32_t),void *context) {
 sinkContext=context;__atomic_store_n(&sink,fn,__ATOMIC_RELEASE);
}
}
namespace {
struct Window {IOMemoryMap *map=nullptr;volatile uint32_t *va=nullptr;};
static bool mapWindow(Window &w,uint64_t base,uint64_t bytes) {
 IODeviceMemory *mem=IODeviceMemory::withRange(base,bytes);
 w.map=mem?mem->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;OSSafeReleaseNULL(mem);
 w.va=w.map?reinterpret_cast<volatile uint32_t *>(w.map->getVirtualAddress()):nullptr;return w.va;
}
static void unmap(Window &w){w.va=nullptr;OSSafeReleaseNULL(w.map);}
static void deliver(uint32_t mask) {
 auto fn=__atomic_load_n(&sink,__ATOMIC_ACQUIRE);
 if(fn){fn(sinkContext,mask);cb_keys[7]=cb_keys[7]+1;}
}
static int readStatus(Window &w,uint8_t offset,uint8_t &value) {
 w.va[0]=cb_read_command(offset);__asm__ volatile("dsb sy" ::: "memory");
 for(unsigned i=0;i<100;++i){uint32_t status=w.va[2];
  if(status&1){cb_keys[11]=status;if(status&14)return -2;__asm__ volatile("dmb sy" ::: "memory");value=w.va[6]&255;cb_keys[2]=cb_keys[2]+1;return 0;}
  IODelay(1);
 }cb_keys[11]=0;return -3;
}
static void keysThread(void *,wait_result_t) {
 thread_set_thread_name(current_thread(),"cheeseburger-keys");
 Window core,channels[3];bool ready=false;
 if(mapWindow(core,0x0800f000,0x1000)) {
  uint32_t version=core.va[0];cb_keys[1]=version;
  // Linux selects v2 at 0x20010000 and v3 at 0x30000000; v5 layout is different.
  if(version>=0x20010000 && version<0x50000000) {
   const uint32_t ppids[3]={0x008,0x0c4,0x0c5};int apids[3]={-1,-1,-1};
   for(unsigned i=0;i<512;++i){uint32_t entry=core.va[(0x800+4*i)/4];if(!entry)continue;
    for(unsigned k=0;k<3;++k)if(apids[k]<0 && ((entry>>8)&0xfff)==ppids[k])apids[k]=i;
   }
   // Raw read-only arbiter dump. The touch-rail page scan is kept but its
   // decode is known to be wrong for this version (apids read as 3), so the raw
   // core registers and map words are logged for an offline decode first.
   {
    for(unsigned i=0;i<8;++i)cb_pmic_dump[i]=core.va[i];
    for(unsigned i=0;i<16;++i)cb_pmic_dump[8+i]=core.va[(0x800+4*i)/4];
    cb_rails[0]=1;
   }
   ready=true;
   for(unsigned k=0;k<3;++k){cb_keys[8+k]=(uint64_t)(int64_t)apids[k];
    if(apids[k]<0 || !mapWindow(channels[k],cb_observer_address(apids[k]),0x1000)){ready=false;break;}}
  }
 }
 unmap(core);
 if(!ready){cb_keys[0]=UINT64_MAX;for(auto &c:channels)unmap(c);return;}
 cb_keys[0]=2;
 unsigned homeChord=0;PE_parse_boot_argn("cheeseburger-home-chord",&homeChord,sizeof homeChord);
 unsigned failures=0,stable=0;uint32_t candidate=0,last=0;bool released=false;
 for(;;){uint8_t raw[3]={};int rc=0;for(unsigned k=0;k<3 && !rc;++k)rc=readStatus(channels[k],0x10,raw[k]);
  if(rc){cb_keys[3]=cb_keys[3]+1;if(++failures>=3){deliver(0);cb_keys[0]=UINT64_MAX-1;break;}IOSleep(10);continue;}
  failures=0;cb_keys[4]=raw[0]|(uint64_t(raw[1])<<8)|(uint64_t(raw[2])<<16);
  uint32_t mask=cb_key_mask(raw[0],raw[1],raw[2]);
  if(mask!=candidate){candidate=mask;stable=1;}else if(stable<2)++stable;
  if(stable==2){
   // Boot-held buttons cannot accidentally lock the screen. Wait for release.
   if(!released){if(!mask){released=true;deliver(0);}}
   else if(mask!=last){if(homeChord==1 && (mask&12)==12 && (last&12)!=12){native_hid_input_command=1;cb_keys[12]=cb_keys[12]+1;}last=mask;cb_keys[5]=mask;cb_keys[6]=cb_keys[6]+1;deliver(mask);}
  }
  IOSleep(10);
 }
 for(auto &c:channels)unmap(c);
}
}
extern "C" void cheeseburger_buttons_probe(void) {
 unsigned enabled=0;if(!PE_parse_boot_argn("cheeseburger-buttons",&enabled,sizeof enabled)||enabled!=1||cb_keys[0])return;
 cb_keys[0]=1;thread_t thread=nullptr;
 if(kernel_thread_start(keysThread,nullptr,&thread)==KERN_SUCCESS)thread_deallocate(thread);else cb_keys[0]=UINT64_MAX-2;
}
