// SPDX-License-Identifier: GPL-2.0-only
// Bounded automatic return: after cheeseburger-auto-reset=<seconds> from kernel
// start, record a marker, give the persistent log writer time to commit, then
// issue PSCI SYSTEM_RESET (SMC64 0x84000009), the same call the entry-probe
// reset stage uses on this board. A value of 0 (default) disables it.
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
extern "C" {
volatile uint64_t cb_reset[8];
}
namespace {
static void resetWorker(void*,wait_result_t){
 uint32_t seconds=0;
 if(!PE_parse_boot_argn("cheeseburger-auto-reset",&seconds,sizeof seconds)||!seconds)return;
 if(seconds>3600)seconds=3600;
 uint64_t now=0;absolutetime_to_nanoseconds(mach_absolute_time(),&now);
 uint64_t target=uint64_t(seconds)*1000000000ULL;
 uint64_t elapsed=now/1000000000ULL;
 cb_reset[0]=1;cb_reset[1]=seconds;cb_reset[2]=elapsed;
 IOLog("CB auto reset: requested=%u s, kernel elapsed=%llu s\n",seconds,elapsed);
 while(now<target){
  uint64_t left=target-now;if(left>1000000000ULL)left=1000000000ULL;
  IOSleep((unsigned)((left+999999ULL)/1000000ULL));
  absolutetime_to_nanoseconds(mach_absolute_time(),&now);
 }
 cb_reset[3]=1;
 IOLog("CB auto reset: %u s reached; committing the log, then PSCI SYSTEM_RESET\n",seconds);
 IOSleep(4000);   // the log writer commits every two seconds
 register uint64_t x0 __asm__("x0")=0x84000009ULL;
 register uint64_t x1 __asm__("x1")=0;
 register uint64_t x2 __asm__("x2")=0;
 register uint64_t x3 __asm__("x3")=0;
 __asm__ volatile("smc #0":"+r"(x0):"r"(x1),"r"(x2),"r"(x3):
   "x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","memory");
 IOLog("CB auto reset: PSCI returned %llx; no further reset attempt\n",x0);
 cb_reset[4]=x0;
 thread_terminate(current_thread());
}
}
extern "C" void cheeseburger_auto_reset_start(void){
 uint32_t seconds=0;
 if(!PE_parse_boot_argn("cheeseburger-auto-reset",&seconds,sizeof seconds)||!seconds)return;
 if(cb_reset[0])return;
 thread_t t=nullptr;
 if(kernel_thread_start(resetWorker,nullptr,&t)==KERN_SUCCESS)thread_deallocate(t);
 else cb_reset[5]=0xffffffff;
}
