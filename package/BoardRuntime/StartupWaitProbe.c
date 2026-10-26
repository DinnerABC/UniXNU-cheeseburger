// Bounded live user-thread samples; no thread is stopped or retained.
#include <stdint.h>
#include <string.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/simple_lock.h>
#include <machine/machine_routines.h>
#include <arm/thread.h>
#include <vm/vm_map.h>
#include <pexpert/pexpert.h>
#include "BootUiTraceCore.h"
enum { WaitThreads=24, WaitFrames=16, WaitRows=360 };
char p21_wait_rows[WaitRows][91];
static char draft[WaitRows][91],published[WaitRows][91];
// Only the process capture worker writes these buffers.
static struct bt_entry samples[WaitThreads];
static uint64_t tids[WaitThreads];
void p21_wait_begin(void){memset(draft,0,sizeof draft);}
void p21_wait_publish(void){memcpy(published,draft,sizeof draft);}
void p21_wait_snapshot(void){memcpy(p21_wait_rows,published,sizeof published);}
static int read_stack(void *context,uint64_t address,void *bytes,unsigned size){return vm_map_read_user((vm_map_t)context,address,bytes,size)!=KERN_SUCCESS;}
void p21_wait_capture(task_t task,unsigned role,int pid){
 if(role>2)return;
 const unsigned base=role*WaitThreads*5;const char letter="SBC"[role];
 snprintf(draft[base],91,"STK %c p%d unavailable/busy",letter,pid);
 if(!task || !task_lock_try(task))return;
 vm_map_t map=task->map;if(!map){task_unlock(task);return;}
 vm_map_reference(map);unsigned slide=task->task_shared_region_slide,count=0;
 memset(samples,0,sizeof samples);memset(tids,0,sizeof tids);
 thread_t thread;
 queue_iterate(&task->threads,thread,thread_t,task_threads){
  if(count>=WaitThreads)break;
  unsigned i=count++;samples[i].error=4;
  bool irq=ml_set_interrupts_enabled(false);
  if(simple_lock_try(&thread->sched_lock,&thread_lck_grp)){
   arm_saved_state_t *state=thread->machine.upcb;tids[i]=thread->thread_id;
   if(state && is_saved_state64(state)){
    samples[i].pc=get_saved_state_pc(state);samples[i].lr=get_saved_state_lr(state);
    samples[i].fp=get_saved_state_fp(state);samples[i].error=0;
   }
   thread_unlock(thread);
  }
  ml_set_interrupts_enabled(irq);
 }
 task_unlock(task);
 for(unsigned i=0;i<count;++i){
  struct bt_entry *entry=&samples[i];uint64_t frames[WaitFrames]={0};
  if(!entry->error && bt_user(entry->pc,4) && bt_user(entry->lr,4))bt_walk_n(entry,read_stack,map,frames,WaitFrames);else entry->error=4;
  unsigned at=base+i*5;
  snprintf(draft[at],91,"STK %c p%d tid=%llu PC=%llx LR=%llx slide=%x e=%u",letter,pid,tids[i],entry->pc,entry->lr,slide,entry->error);
  for(unsigned row=0;row<4;++row)snprintf(draft[at+1+row],91,"FP%u %c %llu %llx %llx %llx %llx",row*4,letter,tids[i],frames[row*4],frames[row*4+1],frames[row*4+2],frames[row*4+3]);
 }
 vm_map_deallocate(map);
}
