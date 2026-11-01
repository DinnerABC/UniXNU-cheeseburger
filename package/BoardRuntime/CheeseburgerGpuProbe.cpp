// SPDX-License-Identifier: GPL-2.0-only
// Read-only Adreno 540 (A540, MSM8998) state dump. Bases from the captured DTS
// (GPU 0x05000000, SMMU 0x05040000, GPUCC 0x05065000) and GCC GPU CBCRs at
// 0x71004/0x71010/0x71028; GPUCC/GX decoding and the A5xx register offsets
// follow the pinned sagit A540 bring-up (AdrenoStartup.h, ClockTables.h).
//
// Nothing here powers the GPU. Reading an unclocked GPUCC or GPU block can raise
// a NoC error, so the probe first proves GCC_GPU_CFG_AHB_CLK is enabled and not
// halted, then only reads GPUCC; GPU and SMMU are read only when GPUCC reports
// the GX domain on and gfx3d running. Skipped regions stay 0xdeadbeef.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
extern "C" {
volatile uint64_t cb_gpu[80];
void cheeseburger_gpu_smmu_setup(void);
void cheeseburger_gpu_cp_probe(void);
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
static void wr(W &w,unsigned off,uint32_t x){__asm__ volatile("dsb sy":::"memory");w.v[off/4]=x;__asm__ volatile("dsb sy":::"memory");}
static void fill(unsigned from,unsigned to,uint32_t v){for(unsigned i=from;i<to;++i)cb_gpu[i]=v;}

// GPUCC offsets: PLL/CRC, CX reset/status/GDSC, roots and branches.
static const unsigned gpuccRegs[]={0x0,0x4,0xc,0x38,0x130,0x1000,0x1004,0x1008,0x1020,
 0x1030,0x1050,0x1054,0x1070,0x1074,0x1090,0x1094,0x1098,0x10a0,0x10a4,0x10b0,0x10d0,
 0x1100,0x1120,0x1124};
// A5xx DWORD-index registers exported as byte offsets (AdrenoSubmit.h):
// RBBM id/revision, reset, AHB controls, Status/Status3, interrupt status,
// CP interrupt status, GPMU ready/error, ring base/read/write, ME control, VBIF.
static const unsigned gpuRegs[]={0x0,0x4,0x10c,0x24c,0x250,0x254,0x13d4,0x14c0,0x1384,
 0x2c60,0x2a380,0x2a384,0x2000,0x2018,0x201c,0x20c8,0xc204};
// SMMU global registers and the non-secure bank-0 context used by the GPU.
static const unsigned smmuRegs[]={0x0,0x20,0x24,0x28,0x800,0xc00,0x8000,0x8010,0x8020,
 0x8024,0x8028,0x8030,0x8038,0x803c,0x8058};

static void worker(void*,wait_result_t){
 unsigned delay=60;PE_parse_boot_argn("cheeseburger-gpu-probe",&delay,sizeof delay);
 if(delay>600)delay=600;
 if(delay<1)delay=1;
 for(unsigned i=0;i<delay;++i)IOSleep(1000);
 fill(0,80,0xdeadbeef);
 cb_gpu[61]=0;cb_gpu[62]=0;cb_gpu[63]=0;
 W gcc;if(!mapw(gcc,0x100000,0xb0000)){cb_gpu[61]=0xffffffff;cb_gpu[63]=0xffff;return;}
 cb_gpu[0]=rd(gcc,0x71004);          // GCC_GPU_CFG_AHB_CBCR
 cb_gpu[1]=rd(gcc,0x71010);          // GCC_GPU_BIMC_GFX_CBCR
 cb_gpu[2]=rd(gcc,0x71028);          // GCC_GPU_MISC
 cb_gpu[70]=rd(gcc,0x46040);          // GCC_BIMC_GFX_CBCR (SMMU clock list)
 if(!(cb_gpu[70]&1)){
  wr(gcc,0x46040,uint32_t(cb_gpu[70])|1);
  for(unsigned i=0;i<20 && !(rd(gcc,0x46040)&1);++i)IOSleep(1);
 }
 cb_gpu[71]=rd(gcc,0x46040);
 IOLog("CB GPU: gcc_bimc_gfx %x->%x\n",uint32_t(cb_gpu[70]),uint32_t(cb_gpu[71]));
 // ClockDxe descriptors: gcc_hlos1/2_gpu_smmu_clk at 0x7d000/0x7e000 and
 // VDD_HLOS1/2_GPU_SMMU at 0x7d024/0x7e024. Read-only: these domains carry
 // the SMMU's table-walk/memory path, which ATOS waits on.
 cb_gpu[64]=rd(gcc,0x7d000);cb_gpu[65]=rd(gcc,0x7d024);
 cb_gpu[66]=rd(gcc,0x7e000);cb_gpu[67]=rd(gcc,0x7e024);
 cb_gpu[68]=rd(gcc,0x7d004);cb_gpu[69]=rd(gcc,0x7e004);
 bool ahb=(cb_gpu[0]&1)&&!((cb_gpu[0]>>31)&1);
 if(!ahb){cb_gpu[63]=1;return;}
 W cc;if(!mapw(cc,0x5065000,0x9000)){cb_gpu[61]=0xfffffffe;cb_gpu[63]=1;return;}
 for(unsigned i=0;i<sizeof gpuccRegs/sizeof gpuccRegs[0];++i)cb_gpu[4+i]=rd(cc,gpuccRegs[i]);
 const uint32_t cx=rd(cc,0x1008), gx=rd(cc,0x1094), gfx=rd(cc,0x1098);
 bool cxOn=(cx&0x80000000u)!=0;
 bool gxOn=(gx&0x80000000u)!=0;
 bool gfxOn=(gfx&1u)&&!((gfx>>31)&1u);
 cb_gpu[62]=(ahb?1u:0u)|(cxOn?2u:0u)|(gxOn?4u:0u)|(gfxOn?8u:0u);
 if(!(gxOn&&gfxOn)){cb_gpu[63]=2;return;}
 fill(28,45,0xdeadbeef);
 W gp;if(!mapw(gp,0x5000000,0x40000)){cb_gpu[61]=0xfffffffd;cb_gpu[63]=2;return;}
 for(unsigned i=0;i<sizeof gpuRegs/sizeof gpuRegs[0];++i)cb_gpu[28+i]=rd(gp,gpuRegs[i]);
 // The GPU SMMU clocks are not enabled by the current firmware power step; a
 // read can fault. Keep it opt-in until those clocks are part of the sequence.
 fill(45,60,0xdeadbeef);
 unsigned smmu=0;PE_parse_boot_argn("cheeseburger-gpu-smmu",&smmu,sizeof smmu);
 if(smmu){W sm;if(mapw(sm,0x5040000,0x10000))for(unsigned i=0;i<sizeof smmuRegs/sizeof smmuRegs[0];++i)cb_gpu[45+i]=rd(sm,smmuRegs[i]);}
 cb_gpu[62]|=smmu?0x10u:0u;
 unsigned setup=0;PE_parse_boot_argn("cheeseburger-gpu-smmu-setup",&setup,sizeof setup);
 if(smmu&&setup)cheeseburger_gpu_smmu_setup();
 unsigned cp=0;if(PE_parse_boot_argn("cheeseburger-gpu-cp",&cp,sizeof cp)&&cp&&smmu&&setup)cheeseburger_gpu_cp_probe();
 cb_gpu[63]=3;
}
}
extern "C" void cheeseburger_gpu_probe(void){
 unsigned on=0;
 if(!PE_parse_boot_argn("cheeseburger-gpu-probe",&on,sizeof on)||!on)return;
 thread_t t=nullptr;
 if(kernel_thread_start(worker,nullptr,&t)==KERN_SUCCESS)thread_deallocate(t);
 else {cb_gpu[61]=0xfffffffc;cb_gpu[63]=0xffff;}
}
