// SPDX-License-Identifier: BSD-2-Clause-Patent
// Process traversal can wait in proc_iterate despite the bounded thread scan.
// Run capture only in its separate worker. The display takes a nonblocking copy
// of the last complete result; never expose an in-progress row to the renderer.
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/simple_lock.h>
#include <machine/machine_routines.h>
#include <arm/thread.h>
#include <string.h>
#include <pexpert/pexpert.h>
#include <kern/misc_protos.h>
// Exact private process ABI from this source revision's proc.h/proc_internal.h.
// Do not mix BSD and Mach-private header type universes in one translation unit.
struct proc;typedef struct proc *proc_t;
char *proc_best_name(proc_t);
int proc_pid(proc_t);
task_t proc_task(proc_t);
void proc_iterate(unsigned,int (*)(proc_t,void *),void *,int (*)(proc_t,void *),void *);
#define PROC_RETURNED 0
#define PROC_ALLPROCLIST (1U<<0)
#define PROC_NOWAITTRANS (1U<<2)
char boot_process_rows[24][120];
volatile unsigned boot_process_passes,boot_process_seen,boot_process_attempts,boot_process_busy;
static char capture_rows[24][120],published_rows[24][120];
static unsigned capture_seen,capture_passes,published_passes,published_seen,publish_lock;
static const char *targets[]={"launchd","chronod","SpringBoard","liveactivitiesd","backboardd","lsd","keybagd","cfprefsd","containermanagerd","kernel_task","PreBoard","datamigrator","migrationpluginwrapper","usermanagerd","logd","ReportCrash","installd","installcoordinationd","containermanagerd_system","configd","identityservicesd","locationd","runningboardd","securityd"};
extern void p21_wait_begin(void),p21_wait_publish(void),p21_wait_snapshot(void);
extern void p21_wait_capture(task_t,unsigned,int);
static int selected(proc_t p,void *arg) {
 (void)arg;
 const char *name=proc_best_name(p);
 for(unsigned i=0;i<24;++i)if(!strcmp(name,targets[i]))return 1;
 return proc_pid(p)==0;
}
static int capture(proc_t p,void *arg) {
 (void)arg;
 const char *name=proc_best_name(p);unsigned index=24;
 for(unsigned i=0;i<24;++i)if(!strcmp(name,targets[i])){index=i;break;}
 if(proc_pid(p)==0)index=9;
 if(index==24)return PROC_RETURNED;
 task_t task=proc_task(p);
 if(!task || !task_lock_try(task)) {
  snprintf(capture_rows[index],120,"%.18s p%d task unavailable/busy",name,proc_pid(p));return PROC_RETURNED;
 }
 unsigned count=0,running=0,waiting=0,busy=0;
 thread_t thread;
 queue_iterate(&task->threads,thread,thread_t,task_threads){
  if(count++>=128)break;
  bool irq=ml_set_interrupts_enabled(false);
  if(!simple_lock_try(&thread->sched_lock,&thread_lck_grp)){++busy;ml_set_interrupts_enabled(irq);continue;}
  unsigned flags=(unsigned)thread->state;
  running+=(flags&TH_RUN)!=0;waiting+=(flags&TH_WAIT)!=0;
  thread_unlock(thread);ml_set_interrupts_enabled(irq);
 }
 unsigned total=(unsigned)task->thread_count;task_unlock(task);
 if(index==2 || index==4 || index==1)p21_wait_capture(task,index==1?2:(index==4),proc_pid(p));
 /* Blocked-boot sampling: who is blocking launchd and logd. */
 if(index==0)p21_wait_capture(task,4,proc_pid(p));
 if(index==14)p21_wait_capture(task,3,proc_pid(p));
 snprintf(capture_rows[index],120,"%.15s p%d threads=%u RUN=%u WAIT=%u busy=%u",name,proc_pid(p),total,running,waiting,busy);
 capture_seen=capture_seen+1;return PROC_RETURNED;
}
void boot_process_capture(void) {
 if(__atomic_exchange_n(&boot_process_busy,1,__ATOMIC_ACQUIRE))return;
 __atomic_fetch_add(&boot_process_attempts,1,__ATOMIC_RELAXED);
 for(unsigned i=0;i<24;++i)snprintf(capture_rows[i],120,"%.17s: absent from this process pass",targets[i]);
 capture_seen=0;p21_wait_begin();
 // No waiting for exec/exit transitions; callouts run outside proc_list_lock.
 proc_iterate(PROC_ALLPROCLIST|PROC_NOWAITTRANS,capture,NULL,selected,NULL);
 
 ++capture_passes;
 if(!__atomic_exchange_n(&publish_lock,1,__ATOMIC_ACQUIRE)) {
  memcpy(published_rows,capture_rows,sizeof(published_rows));p21_wait_publish();
  published_passes=capture_passes;published_seen=capture_seen;
  __atomic_store_n(&publish_lock,0,__ATOMIC_RELEASE);
 }
 __atomic_store_n(&boot_process_busy,0,__ATOMIC_RELEASE);
}
void boot_process_snapshot(void) {
 // One reader: the diagnostic monitor. Never allocate, walk processes or wait
 // for the worker; leave the previous complete display snapshot if it is busy.
 if(__atomic_exchange_n(&publish_lock,1,__ATOMIC_ACQUIRE))return;
 memcpy(boot_process_rows,published_rows,sizeof(boot_process_rows));p21_wait_snapshot();
 boot_process_passes=published_passes;boot_process_seen=published_seen;
 __atomic_store_n(&publish_lock,0,__ATOMIC_RELEASE);
}
