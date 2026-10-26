// Exit-context user-memory inspection, never IRQ/panic context; first two per target only.
extern void boot_progress_exit_fault(int,unsigned,char *,unsigned long);
extern void boot_progress_exit_reason(int,unsigned,char *,unsigned long);
extern volatile uint64_t p20_reason_scan_bytes;
extern volatile unsigned p20_reason_scan_regions,p20_reason_copy_failures,p20_reason_found;
static unsigned detail_gate,detail_attempts[2],detail_exits[2],detail_dropped,reason_attempts;
static char detail_text[2][480],reason_text[768];
static unsigned abort_seen,abort_gate;
static char abort_text[768];
extern int proc_selfpid(void);
extern void proc_selfname(char *,int);
extern volatile unsigned p20_diag_enabled;
#include "P20UiFault.h"
void p20_abort_payload(unsigned ns,uint64_t code,uint64_t payload,unsigned size,uint64_t reason,uint64_t flags,unsigned internal) {
 if(!p20_diag_enabled || internal || !ml_get_interrupts_enabled())return;
 char name[32]={0};proc_selfname(name,sizeof(name));if(strcmp(name,"usermanagerd")){p20_ui_abort_payload(name,ns,code,reason);return;}
 if(__atomic_exchange_n(&abort_gate,1,__ATOMIC_ACQUIRE))return;
 if(abort_seen){__atomic_store_n(&abort_gate,0,__ATOMIC_RELEASE);return;}abort_seen=1;
 char message[385]={0};unsigned copied=0;int error=0;
 if(reason)for(;copied<384;++copied){error=copyin(reason+copied,message+copied,1);if(error || !message[copied])break;if((unsigned char)message[copied]<32 || (unsigned char)message[copied]>126)message[copied]='?';}
 unsigned char bytes[32]={0};unsigned len=size<sizeof(bytes)?size:sizeof(bytes);int pe=payload&&len?copyin(payload,bytes,len):0;
 unsigned long n=snprintf(abort_text,sizeof(abort_text),"ABORT p%d ns=%u code=%llx flags=%llx reason=%llx bytes=%u err=%d text=%s\nPAYLOAD ptr=%llx size=%u read=%u err=%d hex=",proc_selfpid(),ns,code,flags,reason,copied,error,message,payload,size,len,pe);
 if(!pe)for(unsigned i=0;i<len && n+3<sizeof(abort_text);++i)n+=snprintf(abort_text+n,sizeof(abort_text)-n,"%02x",bytes[i]);
 __atomic_store_n(&abort_gate,0,__ATOMIC_RELEASE);
}

char p20_fault_rows[28][91];
static void p20_fault_on_exit(int pid,const char *name,int status) {
 p20_ui_on_exit(pid,name,status);
 unsigned signal=(unsigned)status&0x7f;int slot=name&&!strcmp(name,"usermanagerd")?0:name&&!strcmp(name,"IOMFB_bics_daemon")?1:-1;
 if(slot<0 || signal<3 || signal>12 || signal==9)return;
 __atomic_fetch_add(&detail_exits[slot],1,__ATOMIC_RELAXED);
 if(!ml_get_interrupts_enabled() || __atomic_exchange_n(&detail_gate,1,__ATOMIC_ACQUIRE)){__atomic_fetch_add(&detail_dropped,1,__ATOMIC_RELAXED);return;}
 if(detail_attempts[slot]>=2){__atomic_store_n(&detail_gate,0,__ATOMIC_RELEASE);return;}
 ++detail_attempts[slot];char text[480]={0};
 boot_progress_exit_fault(pid,signal,text,sizeof(text));
 if(text[0])strlcpy(detail_text[slot],text,sizeof(detail_text[slot]));
 else snprintf(detail_text[slot],sizeof(detail_text[slot]),"p%d sig%u no matching saved user registers",pid,signal);

 __atomic_store_n(&detail_gate,0,__ATOMIC_RELEASE);
}
static void p20_fault_wrap(unsigned row,unsigned count,const char *text) {
 size_t size=strnlen(text,767),at=0;
 for(unsigned i=0;i<count && row+i<28;++i){size_t n=size-at;if(n>90)n=90;memcpy(p20_fault_rows[row+i],text+at,n);p20_fault_rows[row+i][n]=0;at+=n;}
}
void p20_fault_snapshot(void) {
 p20_ui_snapshot();
 char text[2][480],reason[768],payload_reason[768]={0};unsigned attempts[2],exits[2],dropped;
 bool irq=ml_set_interrupts_enabled(false);
 if(__atomic_exchange_n(&detail_gate,1,__ATOMIC_ACQUIRE)){ml_set_interrupts_enabled(irq);snprintf(p20_fault_rows[0],91,"FAULT DETAIL worker busy; exits U=%u B=%u; previous complete detail retained",detail_exits[0],detail_exits[1]);return;}
 memcpy(text,detail_text,sizeof(text));memcpy(reason,reason_text,sizeof(reason));memcpy(attempts,detail_attempts,sizeof(attempts));memcpy(exits,detail_exits,sizeof(exits));dropped=detail_dropped;
 __atomic_store_n(&detail_gate,0,__ATOMIC_RELEASE);ml_set_interrupts_enabled(irq);
 if(!__atomic_exchange_n(&abort_gate,1,__ATOMIC_ACQUIRE)){memcpy(payload_reason,abort_text,sizeof(payload_reason));__atomic_store_n(&abort_gate,0,__ATOMIC_RELEASE);}
 memset(p20_fault_rows,0,sizeof(p20_fault_rows));
 snprintf(p20_fault_rows[0],91,"FAULTS exits U=%u B=%u detail U=%u B=%u skipped=%u",exits[0],exits[1],attempts[0],attempts[1],dropped);
 strlcpy(p20_fault_rows[1],"USERMANAGERD: PC/LR, ESR(E), FAR(F), cache slide(+), bounded return addresses",91);
 p20_fault_wrap(2,6,text[0][0]?text[0]:"No matching usermanagerd fault exit captured");
 strlcpy(p20_fault_rows[8],"IOMFB_bics_daemon: lifetime matching fault-exit count retained above",91);
 p20_fault_wrap(9,6,text[1][0]?text[1]:"No matching bics fault exit captured since probe enabled");
 strlcpy(p20_fault_rows[15],"USERMANAGERD direct abort payload (fallback: bounded malloc scan)",91);
 p20_fault_wrap(16,9,payload_reason[0]?payload_reason:reason[0]?reason:"No abort payload or reason scan captured yet");
 strlcpy(p20_fault_rows[25],"SCAN max4MiB attempted reads / max128regions / once per boot, not full crash report",91);
 snprintf(p20_fault_rows[27],91,"SCAN bytes=%llu regions=%u copyfail=%u rank=%u",p20_reason_scan_bytes,p20_reason_scan_regions,p20_reason_copy_failures,p20_reason_found);
 strlcpy(p20_fault_rows[26],"Only matched current-task user memory read; failed reads never suppress original exit",91);
}
