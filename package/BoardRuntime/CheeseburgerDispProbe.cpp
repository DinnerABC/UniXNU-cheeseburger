// SPDX-License-Identifier: GPL-2.0-only
// Read-only MDSS/MDP5 scanout dump. Block bases are the msm8998 entries of the
// Linux MDP5 hardware catalog (drivers/gpu/drm/msm/disp/mdp5/mdp5_cfg.c,
// msm8998_config) and register offsets come from mdp5.xml.h:
//   MDP base 0xc901000, MDSS top 0xc900000, DSI0 0xc994000
//   CTL   0x1000,0x1200,0x1400,0x1600,0x1800     TOP 0x00 FLUSH 0x18 OP 0x14 START 0x1c
//   VIG   0x4000,0x6000,0x8000,0xa000            SRC_XY 0x08 OUT_XY 0x10 SRC0_ADDR 0x14
//   RGB   0x14000,0x16000,0x18000,0x1a000       STRIDE_A 0x24 FORMAT 0x30 OP_MODE 0x200
//   DMA   0x24000,0x26000
//   LM    0x44000..0x49000 (LM0..LM5)            OUT_SIZE 0x04
//   PP    0x70000,0x70800,0x71000,0x71800        TEAR 0x00 SYNC 0x04 HEIGHT 0x08
//   INTF  0x6a000(eDP),0x6a800(DSI0),0x6b000(DSI1)
// The panel is lit by the firmware GOP at this point, so these blocks are
// clocked and the dump only reads. cb_disp layout is documented next to the
// fill order below.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
extern "C" {
volatile uint64_t cb_disp[128];
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
static const unsigned ctlBase[5]={0x1000,0x1200,0x1400,0x1600,0x1800};
static const unsigned vigBase[4]={0x4000,0x6000,0x8000,0xa000};
static const unsigned rgbBase[4]={0x14000,0x16000,0x18000,0x1a000};
static const unsigned dmaBase[2]={0x24000,0x26000};
static const unsigned lmBase[6]={0x44000,0x45000,0x46000,0x47000,0x48000,0x49000};
static const unsigned ppBase[4]={0x70000,0x70800,0x71000,0x71800};
static void worker(void*,wait_result_t){
 cb_disp[124]=1;
 unsigned delay=60;PE_parse_boot_argn("cheeseburger-disp-probe",&delay,sizeof delay);
 if(delay>600)delay=600;if(delay<1)delay=1;
 for(unsigned i=0;i<delay;++i)IOSleep(1000);
 cb_disp[124]=2;
 W md;if(!mapw(md,0xc900000,0x100000)){cb_disp[123]=0xffffffff;cb_disp[124]=3;return;}
 cb_disp[124]=4;
 unsigned n=0;
 cb_disp[n++]=rd(md,0x0);                       // MDSS top HW version
 cb_disp[n++]=rd(md,0x1000);                    // MDP HW version at MDP base+0
 cb_disp[124]=5;
 for(unsigned i=0;i<5;++i)cb_disp[n++]=rd(md,0x1000+ctlBase[i]+0x00);   // CTL TOP
 for(unsigned i=0;i<5;++i)cb_disp[n++]=rd(md,0x1000+ctlBase[i]+0x18);   // CTL FLUSH
 for(unsigned i=0;i<5;++i)cb_disp[n++]=rd(md,0x1000+ctlBase[i]+0x14);   // CTL OP
 for(unsigned i=0;i<5;++i)cb_disp[n++]=rd(md,0x1000+ctlBase[i]+0x1c);   // CTL START
 cb_disp[124]=6;
 for(unsigned i=0;i<6;++i)cb_disp[n++]=rd(md,0x1000+lmBase[i]+0x04);    // LM OUT_SIZE
 for(unsigned i=0;i<6;++i)cb_disp[n++]=rd(md,0x1000+lmBase[i]+0x00);    // LM blend0
 for(unsigned i=0;i<4;++i){cb_disp[n++]=rd(md,0x1000+ppBase[i]+0x00);cb_disp[n++]=rd(md,0x1000+ppBase[i]+0x04);cb_disp[n++]=rd(md,0x1000+ppBase[i]+0x08);}
 cb_disp[124]=7;
 cb_disp[n++]=rd(md,0x1000+0x6a000);cb_disp[n++]=rd(md,0x1000+0x6a004);   // INTF eDP
 cb_disp[n++]=rd(md,0x1000+0x6a800);cb_disp[n++]=rd(md,0x1000+0x6a804);   // INTF DSI0
 cb_disp[n++]=rd(md,0x1000+0x6b000);cb_disp[n++]=rd(md,0x1000+0x6b004);   // INTF DSI1
 // Pipes: address/stride/format/xy/out_xy/op_mode for VIG0-3, RGB0-1, DMA0-1.
 cb_disp[124]=8;
 const unsigned pipes[8]={vigBase[0],vigBase[1],vigBase[2],vigBase[3],rgbBase[0],rgbBase[1],dmaBase[0],dmaBase[1]};
 for(unsigned p=0;p<8;++p){unsigned b=0x1000+pipes[p];
  cb_disp[n++]=rd(md,b+0x14);cb_disp[n++]=rd(md,b+0x24);cb_disp[n++]=rd(md,b+0x30);}
 for(unsigned p=0;p<8;++p){unsigned b=0x1000+pipes[p];
  cb_disp[n++]=rd(md,b+0x08);cb_disp[n++]=rd(md,b+0x10);cb_disp[n++]=rd(md,b+0x200);}
 cb_disp[124]=9;
 // DSI0 controller state (video vs command mode, clock control).
 cb_disp[n++]=rd(md,0xc994000);cb_disp[n++]=rd(md,0xc994004);cb_disp[n++]=rd(md,0xc994008);
 PE_Video video={};if(PE_current_console(&video))video.v_baseAddr=0;
 cb_disp[120]=video.v_baseAddr;cb_disp[121]=(uint64_t)video.v_rowBytes<<32|video.v_width;
 cb_disp[122]=video.v_height;
 cb_disp[123]=1;cb_disp[124]=10;
}
}
extern "C" void cheeseburger_disp_probe(void){
 unsigned delay=60;
 if(!PE_parse_boot_argn("cheeseburger-disp-probe",&delay,sizeof delay)||!delay)return;
 cb_disp[125]=1;
 thread_t t=nullptr;
 if(kernel_thread_start(worker,nullptr,&t)==KERN_SUCCESS){thread_deallocate(t);cb_disp[125]=2;}
 else cb_disp[123]=0xfffffffe;
}
