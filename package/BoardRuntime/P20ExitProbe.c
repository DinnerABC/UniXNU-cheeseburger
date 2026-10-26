// Bounded observational exit hook. No change to exit status or critical-exit policy.
#include <stdint.h>
#include <string.h>
#include <machine/machine_routines.h>
extern volatile unsigned p20_diag_enabled;
#include "P20FaultExit.h"
struct p20_exit {int pid,status;unsigned ns,sequence;uint64_t code;char name[32];};
static struct p20_exit recent[6],failed[3];
static unsigned gate,total,failures,dropped;
char p20_exit_rows[5][120];
void p20_process_exit(int pid,const char *name,int status,unsigned ns,uint64_t code) {
 if(!p20_diag_enabled)return;
 p20_fault_on_exit(pid,name,status);
 bool irq=ml_set_interrupts_enabled(false);
 if(__atomic_exchange_n(&gate,1,__ATOMIC_ACQUIRE)){__atomic_fetch_add(&dropped,1,__ATOMIC_RELAXED);ml_set_interrupts_enabled(irq);return;}
 struct p20_exit e={.pid=pid,.status=status,.ns=ns,.code=code,.sequence=++total};
 if(name)strlcpy(e.name,name,sizeof(e.name));
 recent[(total-1)%6]=e;if(status || ns || code)failed[(failures++)%3]=e;
 __atomic_store_n(&gate,0,__ATOMIC_RELEASE);ml_set_interrupts_enabled(irq);
}
void p20_exit_snapshot(void) {
 struct p20_exit copy[3],last={0};unsigned n,t,d;
 bool irq=ml_set_interrupts_enabled(false);
 if(__atomic_exchange_n(&gate,1,__ATOMIC_ACQUIRE)){ml_set_interrupts_enabled(irq);return;}
 memcpy(copy,failed,sizeof(copy));n=failures;t=total;d=dropped;if(t)last=recent[(t-1)%6];
 __atomic_store_n(&gate,0,__ATOMIC_RELEASE);ml_set_interrupts_enabled(irq);
 snprintf(p20_exit_rows[0],120,"EXITS total=%u nonzero=%u dropped=%u (raw wait status/ns/code)",t,n,d);
 for(unsigned i=0;i<3;++i){unsigned k=n>3?n-3:0;
  if(k+i<n){struct p20_exit *e=&copy[(k+i)%3];snprintf(p20_exit_rows[1+i],120,"#%u %.20s p%d st=%x ns=%u code=%llx",e->sequence,e->name,e->pid,e->status,e->ns,e->code);}
  else strlcpy(p20_exit_rows[1+i],"No retained nonzero exit",120);
 }
 if(t)snprintf(p20_exit_rows[4],120,"LAST #%u %.20s p%d st=%x ns=%u code=%llx",last.sequence,last.name,last.pid,last.status,last.ns,last.code);
 else strlcpy(p20_exit_rows[4],"No exit observed since probe enabled",120);
}
