#include <initializer_list>

#include <cstdint>
#include <cassert>
#include "ReadPolicy.h"
#include "TransferPoll.h"
struct Port {
 unsigned n=0,mode=0;uint32_t doorbell(){return mode==0?0:1;}
 uint32_t interrupt(){return mode==1?kUfsFatalStatus:0;}
 uint32_t completedOcs(){return 0;}
 bool expired(){return n>=4;}
 void delay(){n++;}
};
int main(){
 for(unsigned op=0;op<256;op++)for(unsigned bytes:{0U,8U,32U,96U,256U,4096U,131072U,131076U}) {
  assert(!p8ReadCommand(op,1,bytes)); // all outbound data rejected
  if(p8ReadCommand(op,2,bytes))assert(op==0x12||op==0x25||op==0x9e||op==0x28||op==0x88||op==0xa0);
 }
 assert(!p8ReadCommand(0x2a,2,4096));assert(!p8ReadCommand(0x35,0,0));assert(!p8ReadCommand(0x42,0,0));
 assert(p8BlockRange(0,1,0,4096));assert(!p8BlockRange(~0ULL,2,~0ULL,4096));assert(!p8BlockRange(1,1,0,4096));assert(!p8BlockRange(0,1,99,3000));
 Port p;UfsTransferPoll s;assert(pollUfsTransfer(p,s)==UfsTransferEnd::Completed);
 Port f;f.mode=1;UfsTransferPoll fs;assert(pollUfsTransfer(f,fs)==UfsTransferEnd::Fatal);
 Port t;t.mode=2;UfsTransferPoll ts;assert(pollUfsTransfer(t,ts)==UfsTransferEnd::Timeout&&t.n==4);
}
