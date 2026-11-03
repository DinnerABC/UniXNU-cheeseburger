#include "CheeseburgerGpuQuiesce.h"
#include <map>
#include <cstdio>
using namespace CheeseburgerGpu;
struct IO {
 std::map<unsigned,unsigned> r; bool ack=true;unsigned writes=0;
 unsigned read(unsigned o){if(o==VBIFHalted)return ack?15:0;return r[o];}
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
 std::puts("GPU quiesce model PASS");return 0;
}
