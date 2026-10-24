/* P9 first-fatal capture. Reuses the pinned official panic-screen renderer.
 * Fatal-only, allocation-free, no firmware/partition writes and no reset.
 * A successful capture parks: it never resumes a kernel after panic.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <mach/arm/thread_status.h>
#include "Renderer.inc"
#include "Font.h"
extern volatile uint64_t p6_diag_record[];
struct P9FatalRecord {
 uint32_t claimed,ready,kind,ctx_valid;
 uint64_t caller,frame,stack,esr,far,elr,spsr,tcr,sctlr,reason;
 uint64_t fault_pc,fault_lr,fault_sp,fault_esr,fault_far,fault_cpsr;
 uint64_t frames[16];uint32_t depth;
 char first[4096];char last_bootstrap[128];
};
struct P9FatalRecord p9_first_fatal;
char p9_last_bootstrap[128];
void p9_note_stage(const char *s) {
 if (!s || p9_first_fatal.claimed) return;
 unsigned i=0;for(;i<sizeof(p9_last_bootstrap)-1 && s[i];i++)p9_last_bootstrap[i]=s[i];p9_last_bootstrap[i]=0;
}
static int range_readable(const void *p,unsigned n) {
 uintptr_t a=(uintptr_t)p;if(!n||a+n<a)return 0;
 if(!readable((const char*)p,0)||!readable((const char*)(a+n-1),0))return 0;
 for(uintptr_t b=(a&~(uintptr_t)4095)+4096;b<a+n;b+=4096)if(!readable((const char*)b,0))return 0;
 return 1;
}
static void snapshot(unsigned kind,unsigned reason,void *ctx,unsigned long caller) {
 p9_first_fatal.kind=kind;p9_first_fatal.reason=reason;p9_first_fatal.caller=caller;
 __asm__ volatile("mrs %0, esr_el1":"=r"(p9_first_fatal.esr));
 __asm__ volatile("mrs %0, far_el1":"=r"(p9_first_fatal.far));
 __asm__ volatile("mrs %0, elr_el1":"=r"(p9_first_fatal.elr));
 __asm__ volatile("mrs %0, spsr_el1":"=r"(p9_first_fatal.spsr));
 __asm__ volatile("mrs %0, tcr_el1":"=r"(p9_first_fatal.tcr));
 __asm__ volatile("mrs %0, sctlr_el1":"=r"(p9_first_fatal.sctlr));
 __asm__ volatile("mov %0, sp":"=r"(p9_first_fatal.stack));
 p9_first_fatal.frame=(uintptr_t)__builtin_frame_address(0);
 for(unsigned i=0;i<sizeof(p9_last_bootstrap);i++)p9_first_fatal.last_bootstrap[i]=p9_last_bootstrap[i];
 if(ctx&&range_readable(ctx,sizeof(arm_saved_state_t))) {
  const arm_saved_state_t *s=ctx;
  if(is_saved_state64(s)) {
   p9_first_fatal.ctx_valid=1;p9_first_fatal.fault_pc=s->ss_64.pc;p9_first_fatal.fault_lr=s->ss_64.lr;p9_first_fatal.fault_sp=s->ss_64.sp;
   p9_first_fatal.fault_esr=s->ss_64.esr;p9_first_fatal.fault_far=s->ss_64.far;p9_first_fatal.fault_cpsr=s->ss_64.cpsr;
  }
 }
 uintptr_t fp=(uintptr_t)__builtin_frame_address(0);
 for(unsigned i=0;i<16;i++) {
  if((fp&15)||!range_readable((void*)fp,16))break;
  const uintptr_t *frame=(const uintptr_t*)fp;uintptr_t next=frame[0],lr=frame[1];
  p9_first_fatal.frames[p9_first_fatal.depth++]=lr;
  if(next<=fp||next-fp>0x100000)break;fp=next;
 }
}
__attribute__((noreturn)) static void capture(unsigned kind,const char *fmt,const u64 *args,unsigned reason,void *ctx,unsigned long caller) {
 __asm__ volatile("msr daifset,#15":::"memory");
 if(__atomic_exchange_n(&p9_first_fatal.claimed,1,__ATOMIC_ACQ_REL))park();
 snapshot(kind,reason,ctx,caller);
 struct ctx g={0};
 // This VA was obtained by the proven post-VM ml_io_map path, not a fixed VA.
 if(!p6_diag_record[29]||!p6_diag_record[16]||p6_diag_record[26]!=4320||p6_diag_record[17]<4320*1920)park();
 g.base=(u8*)(uintptr_t)p6_diag_record[16];g.rb=4320;g.w=1080;g.h=1920;
 for(unsigned y=0;y<g.h;y++)for(unsigned x=0;x<g.w;x++)*(volatile u32*)(g.base+(u64)y*g.rb+x*4)=0;
 text(&g,"P9 FIRST FATAL - NO RESET\n");
 text(&g,kind==1?"PANIC COMMON FUNNEL\n":kind==2?"DEBUGGER REBOOT (not necessarily panic)\n":"HALT/REBOOT REQUEST (no panic captured)\n");
 g.log=(u8*)p9_first_fatal.first;g.logn=0;
 if(fmt)format(&g,fmt,args);else text(&g,"No original panic format supplied");
 g.log=0;
 hex(&g,"\nCaller=",caller);hex(&g," reason=",reason);
 text(&g,"\nLast bootstrap: ");text(&g,p9_first_fatal.last_bootstrap);
 if(p9_first_fatal.ctx_valid){
  text(&g,"\nException context (saved):");hex(&g,"\nPC=",p9_first_fatal.fault_pc);hex(&g," LR=",p9_first_fatal.fault_lr);
  hex(&g,"\nSP=",p9_first_fatal.fault_sp);hex(&g," ESR=",p9_first_fatal.fault_esr);hex(&g,"\nFAR=",p9_first_fatal.fault_far);hex(&g," CPSR=",p9_first_fatal.fault_cpsr);
 }else text(&g,"\nNo validated exception context; live EL1 values may be stale.");
 hex(&g,"\nESR_EL1=",p9_first_fatal.esr);hex(&g," FAR_EL1=",p9_first_fatal.far);
 hex(&g,"\nELR_EL1=",p9_first_fatal.elr);hex(&g," SPSR_EL1=",p9_first_fatal.spsr);
 hex(&g,"\nTCR=",p9_first_fatal.tcr);hex(&g," SCTLR=",p9_first_fatal.sctlr);
 text(&g,"\nBounded kernel frame chain (raw return addresses):");
 for(unsigned i=0;i<p9_first_fatal.depth;i++)hex(&g,"\nLR=",p9_first_fatal.frames[i]);
 text(&g,"\nSTOPPED. Photograph first error; do not reboot yet.");
 p9_first_fatal.ready=1;__asm__ volatile("dsb sy":::"memory");park();
}
__attribute__((noreturn)) void p9_panic_capture(const char *fmt,va_list *ap,unsigned reason,void *ctx,unsigned long caller) {
 const u64 *slots=ap&&range_readable(ap,sizeof(*ap))?(const u64*)*ap:0;
 capture(1,fmt,slots,reason,ctx,caller);
}
__attribute__((noreturn)) void p9_debugger_reboot(unsigned type,unsigned op,const char *message,unsigned long caller) {
 const u64 slots[]={type,op,(uintptr_t)message};capture(2,"type=%u op=%u message=%s",slots,type,0,caller);
}
__attribute__((noreturn)) void p9_halt_capture(unsigned reboot,unsigned long caller) {
 const u64 slots[]={reboot};capture(3,"halt_all_cpus reboot=%u",slots,reboot,0,caller);
}
const u64 gFont[] = GLYPH_FONT;
