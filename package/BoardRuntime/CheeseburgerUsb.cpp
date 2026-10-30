// SPDX-License-Identifier: GPL-2.0-only
// USB3 (Synopsys DWC3) state dump for the msm8998 port. Offsets: GCC from
// gcc-msm8998.c, controller from the UEFI reference
// Silicon/Samsung/SamsungPkg/Include/Device/Dwc3.h, block addresses from the
// captured msm8998 DTS (wrapper usb@a8f8800, controller usb@a800000,
// QUSB2 phy@c012000, QMP usb3 phy@c010000).
//
// cheeseburger-usb-probe=<seconds>  delay before the dump (default 60, cap 600)
// cheeseburger-usb-mode=1           read-only (default)
// cheeseburger-usb-mode=2           if the domain is off, power the GDSC and the
//                                   AHB clocks first and dump again; the state
//                                   before any write stays in [12..22]
// An unclocked third-party block access can raise a NoC error, so the
// controller, wrapper and PHYs are only read when the USB30 GDSC status and the
// USB30 master clock read back as on; skipped ranges hold 0xdeadbeef.
//
// cb_usb layout (128 words):
//   [0..10] GCC now, [11] flags, [12..22] GCC before any write,
//   [32..63] DWC3, [64..79] QSCRATCH, [80..95] QUSB2 PHY, [96..127] QMP PHY,
//   [112] done marker.
// flags: bit0 power domain on, bit1 master clock running, bit2 mode 2,
//        bit3 clocks enabled here, bit4 pre-enable power, bit5 pre-enable clock.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
extern "C" {
volatile uint64_t cb_usb[128];
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
static void wr(W &w,unsigned off,uint32_t x){w.v[off/4]=x;__asm__ volatile("dsb sy":::"memory");}
static void fill(unsigned from,unsigned to,uint32_t v){for(unsigned i=from;i<to;++i)cb_usb[i]=v;}

// GCC: USB30 GDSC 0xf004, master 0xf008, sleep 0xf00c, mock utmi 0xf010,
// usb3 phy aux 0x50000, usb3 phy pipe 0x50004, cfg noc 0x5018, aggre1 0x82024,
// usb3 clkref 0x88008, rx1 usb2 clkref 0x88014, usb phy cfg ahb2phy 0x6a004.
static const unsigned gccRegs[]={0xf004,0xf008,0xf00c,0xf010,0x50000,0x50004,0x5018,0x82024,0x88008,0x88014,0x6a004};
static const unsigned gccEnable[]={0xf008,0xf00c,0xf010,0x50000,0x50004,0x5018,0x82024,0x88008,0x88014,0x6a004};
static const unsigned dwc3Regs[]={0xc100,0xc104,0xc110,0xc118,0xc11c,0xc120,0xc12c,
 0xc140,0xc144,0xc148,0xc14c,0xc150,0xc154,0xc158,0xc15c,0xc19c,
 0xc200,0xc204,0xc2c0,0xc300,0xc304,0xc380,
 0xc400,0xc404,0xc408,0xc40c,0xc700,0xc704,0xc708,0xc70c,0xc714,0xc710};
static void gccRead(W &gcc,unsigned base){
 for(unsigned i=0;i<11;++i)cb_usb[base+i]=rd(gcc,gccRegs[i]);
}
static void dumpDwc3(){
 W d;fill(32,64,0xdeadbeef);
 if(mapw(d,0xa800000,0xcd00))for(unsigned i=0;i<32;++i)cb_usb[32+i]=rd(d,dwc3Regs[i]);
}
static void dumpQscratch(){
 W q;fill(64,80,0xdeadbeef);
 if(mapw(q,0xa8f8800,0x400))for(unsigned i=0;i<16;++i)cb_usb[64+i]=rd(q,i*4);
}
static void dumpPhys(){
 W p2;fill(80,96,0xdeadbeef);
 if(mapw(p2,0xc012000,0x2a8))for(unsigned i=0;i<16;++i)cb_usb[80+i]=rd(p2,i*4);
 W p3;fill(96,128,0xdeadbeef);
 if(mapw(p3,0xc010000,0x1000))for(unsigned i=0;i<32;++i)cb_usb[96+i]=rd(p3,i*4);
}
static void worker(void*,wait_result_t){
 unsigned delay=60;PE_parse_boot_argn("cheeseburger-usb-probe",&delay,sizeof delay);
 if(delay>600)delay=600;if(delay<1)delay=1;
 unsigned mode=1;PE_parse_boot_argn("cheeseburger-usb-mode",&mode,sizeof mode);
 for(unsigned i=0;i<delay;++i)IOSleep(1000);
 W gcc;if(!mapw(gcc,0x100000,0x53000)){cb_usb[11]=0x80000000u;return;}
 gccRead(gcc,12);                                   // state before any write
 for(unsigned i=0;i<11;++i)cb_usb[i]=cb_usb[12+i];
 bool pwr=(cb_usb[0]&0x80000000u)!=0;
 bool clk=(cb_usb[1]&1u)&&!((cb_usb[1]>>31)&1u);
 bool enabled=false;
 if(!(pwr&&clk) && mode>=2){
  wr(gcc,0xf004,rd(gcc,0xf004)&~1u);                // GDSC SW_COLLAPSE=0
  for(unsigned i=0;i<1000 && !(rd(gcc,0xf004)&0x80000000u);++i)IODelay(10);
  for(unsigned i=0;i<sizeof gccEnable/sizeof gccEnable[0];++i)wr(gcc,gccEnable[i],rd(gcc,gccEnable[i])|1u);
  for(unsigned i=0;i<1000;++i){bool halted=false;
   for(unsigned k=0;k<sizeof gccEnable/sizeof gccEnable[0];++k)if(rd(gcc,gccEnable[k])&0x80000000u)halted=true;
   if(!halted)break;IODelay(10);}
  gccRead(gcc,0);
  pwr=(cb_usb[0]&0x80000000u)!=0;
  clk=(cb_usb[1]&1u)&&!((cb_usb[1]>>31)&1u);
  enabled=true;
 }
 cb_usb[11]=(pwr?1u:0u)|(clk?2u:0u)|(mode>=2?4u:0u)|(enabled?8u:0u)|
            (((cb_usb[12]&0x80000000u)!=0)?0x10u:0u)|(((cb_usb[13]&1u)!=0)?0x20u:0u);
 if(pwr&&clk){dumpDwc3();dumpQscratch();dumpPhys();}
 cb_usb[112]=1;
}
}
extern "C" void cheeseburger_usb_probe(void){
 unsigned delay=60;
 if(!PE_parse_boot_argn("cheeseburger-usb-probe",&delay,sizeof delay)||!delay)return;
 thread_t t=nullptr;
 if(kernel_thread_start(worker,nullptr,&t)==KERN_SUCCESS)thread_deallocate(t);
 else cb_usb[11]=0x40000000u;
}
