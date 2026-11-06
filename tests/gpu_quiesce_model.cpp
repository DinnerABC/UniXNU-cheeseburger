#include "CheeseburgerGpuQuiesce.h"
#include <map>
#include <cstdio>
using namespace CheeseburgerGpu;
struct IO {
 std::map<unsigned,unsigned> r;
 bool ack=true,resuming=false;
 unsigned writes=0,invalidReg=UINT32_MAX,invalidAt=0,busyReads=0,lateStatus=0;
 std::map<unsigned,unsigned> reads;
 unsigned read(unsigned o){
  const unsigned n=++reads[o];
  if(o==Status&&n<=busyReads)return 0x40000000;
  if(o==Status&&lateStatus&&n>busyReads+1)return lateStatus;
  if(o==invalidReg&&n==invalidAt)return UINT32_MAX;
  if(o==VBIFHalted)return ack?15:0;
  return r[o];
 }
 void write(unsigned o,unsigned v){r[o]=v;++writes;}
 void delay(unsigned){}void publish(){resuming=true;}
};
static IO idle(){IO io;io.r[RBRead]=io.r[RBWrite]=9;io.r[InterruptMask]=42;return io;}
static unsigned failures;
static void check(bool value,const char *name){if(!value){++failures;std::printf("FAIL %s\n",name);}}
int main(){
 DrainState s;IO a=idle();
 check(drainForTables(a,true,s)&&s.stage==4&&s.reason==0&&s.valid==0x7ff&&s.cpValid==0x1f&&s.cpMe==Halt&&s.cpRead==s.cpWrite,"drain/success");
 check(resumeAfterTables(a,s)&&s.stage==6&&!a.r[MEControl]&&!a.r[VBIFHalt]&&a.r[InterruptMask]==42,"resume/success");
 const unsigned initial[]={RBRead,RBWrite,MEControl,VBIFHalt,InterruptMask};
 for(unsigned reg:initial){IO io=idle();io.invalidReg=reg;io.invalidAt=1;
  check(!drainForTables(io,true,s)&&s.reason==DrainInitialInvalid&&!io.writes,"initial/invalid-no-write");}
 IO busy=idle();busy.r[RBWrite]=10;check(!drainForTables(busy,true,s)&&s.reason==DrainRingBusy&&!busy.writes,"initial/ring-busy");
 IO control=idle();control.r[VBIFHalt]=1;check(!drainForTables(control,true,s)&&s.reason==DrainControlMismatch&&!control.writes,"initial/control");
 for(unsigned reg:{MEControl,VBIFHalted}){IO io=idle();io.invalidReg=reg;io.invalidAt=reg==MEControl?3:1;
  check(!drainForTables(io,true,s)&&s.reason==DrainAckInvalid&&io.r[MEControl]==Halt,"ack/invalid-halted");}
 IO timeout=idle();timeout.ack=false;check(!drainForTables(timeout,true,s)&&s.reason==DrainAckTimeout&&s.wait==99990,"ack/timeout");
 for(unsigned reg:{Status,Status3,RBRead,RBWrite}){IO io=idle();io.invalidReg=reg;io.invalidAt=3;
  check(!drainForTables(io,true,s)&&s.reason==DrainFinalInvalid&&io.r[MEControl]==Halt,"final/invalid");}
 IO nonidle=idle();nonidle.r[Status]=0x40000000;check(!drainForTables(nonidle,true,s)&&s.reason==DrainPreIdleTimeout&&s.status==0x40000000&&!nonidle.writes,"preidle/timeout-no-write");
 IO s3=idle();s3.r[Status3]=1u<<24;check(!drainForTables(s3,true,s)&&s.reason==DrainPreIdleTimeout&&!s3.writes,"preidle/status3");
 for(unsigned reg:{MEControl,VBIFHalt,RBRead,RBWrite}){IO io=idle();check(drainForTables(io,true,s),"resume/setup");
  io.invalidReg=reg;io.invalidAt=io.reads[reg]+1;
  check(!resumeAfterTables(io,s)&&s.reason==DrainResumeInvalid&&io.r[InterruptMask]==0,"resume/invalid-mask-retained");}
 IO settling=idle();settling.busyReads=5;check(drainForTables(settling,true,s)&&s.stage==4,"preidle/busy-clears-before-halt");
 IO late=idle();late.lateStatus=0x40000000;check(!drainForTables(late,true,s)&&s.reason==DrainNotIdle&&late.r[MEControl]==Halt,"final/busy-still-rejected");
 IO invalid=idle();invalid.invalidReg=Status;invalid.invalidAt=1;check(!drainForTables(invalid,true,s)&&s.reason==DrainPreIdleInvalid&&!invalid.writes,"preidle/invalid-no-halt");
 IO stopped=idle();stopped.r[MEControl]=Halt;
 check(drainForTables(stopped,false,s)&&!s.resume&&resumeAfterTables(stopped,s)&&stopped.r[VBIFHalt]==15,"stopped/no-resume");
 std::printf("GPU quiesce model %s failures=%u\n",failures?"FAIL":"PASS",failures);return failures?1:0;
}
