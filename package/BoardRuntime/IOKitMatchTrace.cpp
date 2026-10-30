// Passive IOKit boundary recorder: no matching, iteration or return is repeated/changed.
#include "IOKitMatchTrace.h"
#include <IOKit/IOLib.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSObject.h>
#include <libkern/c++/OSMetaClass.h>
#include <kern/simple_lock.h>
#include <machine/machine_routines.h>
#include <sys/proc.h>
#include <pexpert/pexpert.h>
#include "BoardBootArgs.h"
namespace {
enum { Events=128, First=16, Recent=Events-First, Handles=64 };
struct Event {uint64_t seq,ticks,handle;int pid,status;unsigned done,hits;char who[20],kind[20],query[168],result[40];};
struct Handle {uintptr_t object;int pid;uint64_t seq;unsigned results;};
static Event events[Events];static Handle handles[Handles];
static unsigned enabled,initialized,lockword;static uint64_t serial,skipped,overwritten,accelerator_requests,accelerator_notfound,iterator_ends;
static bool contains(const char *s,const char *needle){unsigned n=(unsigned)strlen(needle);for(unsigned i=0;s[i];++i)if(!strncmp(s+i,needle,n))return true;return false;}
static unsigned index(uint64_t seq){return seq<=First?(unsigned)seq-1:First+(unsigned)((seq-First-1)%Recent);}
static bool enter(){return !__atomic_exchange_n(&lockword,1,__ATOMIC_ACQUIRE);}
static void leave(){__atomic_store_n(&lockword,0,__ATOMIC_RELEASE);}
static bool selected(char *name,int *pid){
 if(!initialized){unsigned arg=0;cb_board_arg("p21-iokit-trace",&arg,sizeof(arg));enabled=arg==1;initialized=1;}
 if(!enabled)return false;*pid=proc_selfpid();proc_name(*pid,name,20);
 return !strcmp(name,"SpringBoard")||!strcmp(name,"backboardd")||!strcmp(name,"PreBoard")||!strcmp(name,"lsd")||!strcmp(name,"datamigrator")||!strcmp(name,"usermanagerd")||!strcmp(name,"containermanagerd")||!strncmp(name,"com.apple.datamigr",18)||!strncmp(name,"migrationplugin",15);
}
static void field(char *out,unsigned cap,OSDictionary *dict,const char *key){
 OSString *s=OSDynamicCast(OSString,dict->getObject(key));if(!s)return;
 unsigned at=(unsigned)strlen(out);if(at<cap)snprintf(out+at,cap-at," %s=%.44s",key,s->getCStringNoCopy());
}
}
extern "C" uint64_t p21_match_begin(OSDictionary *dict,const char *kind){
 char name[20]={0};int pid=0;if(!dict||!selected(name,&pid))return 0;
 char query[168]={0};field(query,sizeof(query),dict,"IOProviderClass");field(query,sizeof(query),dict,"IONameMatch");field(query,sizeof(query),dict,"IOClass");field(query,sizeof(query),dict,"IOResourceMatch");
 OSDictionary *props=OSDynamicCast(OSDictionary,dict->getObject("IOPropertyMatch"));if(props){field(query,sizeof(query),props,"IOClass");field(query,sizeof(query),props,"IOUserClientClass");}
 if(!enter()){__atomic_add_fetch(&skipped,1,__ATOMIC_RELAXED);return 0;}
 uint64_t seq=++serial;if(seq>Events)++overwritten;Event &e=events[index(seq)];memset(&e,0,sizeof e);e.seq=seq;e.pid=pid;e.status=-1;__asm__ volatile("mrs %0,cntpct_el0":"=r"(e.ticks));strlcpy(e.who,name,sizeof e.who);strlcpy(e.kind,kind,sizeof e.kind);strlcpy(e.query,query,sizeof e.query);
 if(contains(query,"Accelerator")||contains(query,"AGX"))++accelerator_requests;
 leave();return seq;
}
extern "C" void p21_match_end(uint64_t seq,int status,OSObject *result){
 if(!seq)return;char cls[40]={0};if(result)strlcpy(cls,result->getMetaClass()->getClassName(),sizeof cls);
 if(!enter()){__atomic_add_fetch(&skipped,1,__ATOMIC_RELAXED);return;}
 Event &e=events[index(seq)];if(e.seq==seq){e.status=status;e.done=1;e.handle=(uint64_t)(uintptr_t)result;strlcpy(e.result,cls,sizeof e.result);if(status && (contains(e.query,"Accelerator")||contains(e.query,"AGX")))++accelerator_notfound;
  if(result && (!strcmp(e.kind,"multi")||!strncmp(e.kind,"IOService",9))){unsigned slot=(unsigned)(seq%Handles);for(unsigned i=0;i<Handles;++i)if(!handles[i].object||handles[i].object==(uintptr_t)result){slot=i;break;}handles[slot]={(uintptr_t)result,e.pid,seq,0};}}
 leave();
}
extern "C" void p21_match_next(OSObject *iterator,OSObject *object,int status){
 char name[20]={0};int pid=0;if(!selected(name,&pid))return;
 char cls[40]={0};if(object)strlcpy(cls,object->getMetaClass()->getClassName(),sizeof cls);
 if(!enter()){__atomic_add_fetch(&skipped,1,__ATOMIC_RELAXED);return;}
 for(unsigned i=0;i<Handles;++i)if(handles[i].object==(uintptr_t)iterator && handles[i].pid==pid){Handle &h=handles[i];if(object)++h.results;else ++iterator_ends;
  Event &parent=events[index(h.seq)];if(parent.seq==h.seq){parent.done=object?2:3;parent.hits=h.results;parent.status=status;if(object)strlcpy(parent.result,cls,sizeof parent.result);else if(!h.results&&(contains(parent.query,"Accelerator")||contains(parent.query,"AGX")))++accelerator_notfound;}
  if(!object)h.object=0;break;}
 leave();
}
extern "C" unsigned p21_match_dump(char *out,unsigned cap){
 if(!out||!cap)return 0;out[0]=0;if(!enter()){snprintf(out,cap,"IOKIT snapshot busy; previous events retained\n");return (unsigned)strlen(out);}
 unsigned at=(unsigned)snprintf(out,cap,"IOKIT seq=%llu contention_drop=%llu ring_overwritten=%llu accelerator_requests=%llu empty_or_error=%llu iterator_ends=%llu\n",serial,skipped,overwritten,accelerator_requests,accelerator_notfound,iterator_ends);
 uint64_t ids[128];unsigned n=0;for(uint64_t s=1;s<=serial&&s<=First;++s)ids[n++]=s;
 uint64_t start=serial>112?serial-111:First+1;if(start<=First)start=First+1;
 for(uint64_t s=start;s<=serial&&n<128;++s)ids[n++]=s;
 for(unsigned j=0;j<n&&at+512<cap;++j){Event &e=events[index(ids[j])];if(e.seq!=ids[j])continue;at+=(unsigned)snprintf(out+at,cap-at,"M%llu t=%llu %s p%d %s done=%u hits=%u rc=%x handle=%llx result=%s query:%s\n",e.seq,e.ticks,e.who,e.pid,e.kind,e.done,e.hits,(unsigned)e.status,e.handle,e.result,e.query);}
 leave();return at<cap?at:cap-1;
}
