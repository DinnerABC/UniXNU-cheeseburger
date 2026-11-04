#include "CheeseburgerGpuQuiesce.h"
#include <map>
#include <cstdio>
using namespace CheeseburgerGpu;
struct IO {
 std::map<unsigned,unsigned> r; bool ack=true;unsigned writes=0;
 // The device's VBIF registers carry a per-client status in bits 19:16, so an
 // idle VBIF reads 0xf0000 rather than 0 (P21-Nighthawk).
 unsigned read(unsigned o){
  if(o==VBIFHalted)return 0xf0000u|(ack?15u:0u);
  if(o==VBIFHalt)return 0xf0000u|(r[VBIFHalt]&0xfu);
  return r[o];
 }
 void write(unsigned o,unsigned v){r[o]=v;++writes;}
 void delay(unsigned){} void publish(){}
};
int main(){
 IO a;DrainState s;a.r[RBRead]=a.r[RBWrite]=9;a.r[InterruptMask]=42;
 if(!drainForTables(a,true,s)||!s.resume||a.r[MEControl]!=Halt||a.r[VBIFHalt]!=15)return 1;
 if(!resumeAfterTables(a,s)||a.r[MEControl]||a.r[VBIFHalt]||a.r[InterruptMask]!=42)return 2;
 IO b;b.r[RBRead]=1;b.r[RBWrite]=2;if(drainForTables(b,true,s)||b.writes)return 3;
 IO c;c.r[RBRead]=c.r[RBWrite]=9;c.ack=false;if(drainForTables(c,true,s)||c.r[VBIFHalt]!=15)return 4;
 IO d;d.r[MEControl]=Halt;if(!drainForTables(d,false,s)||s.resume||!resumeAfterTables(d,s)||d.r[VBIFHalt]!=15)return 5;
 IO e;e.r[RBRead]=e.r[RBWrite]=9;e.r[VBIFHalt]=0xf0000;
 if(!drainForTables(e,true,s)||e.r[VBIFHalt]!=15)return 6;
 if(!resumeAfterTables(e,s)||((e.r[VBIFHalt]&0xfu)!=0))return 7;
 std::puts("GPU quiesce model PASS");return 0;
}
