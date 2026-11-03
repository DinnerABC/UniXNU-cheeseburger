// SPDX-License-Identifier: GPL-2.0-only
// A540 CP/VBIF drain sequence follows sagit AdrenoSubmit::idlePowerTransition.
#pragma once
#include "CheeseburgerGpuStart.h"
namespace CheeseburgerGpu {
struct DrainState { uint32_t mask=0, cursor=0; bool resume=false; };
template<class IO> bool drainForTables(IO &io, bool running, DrainState &s) {
 const uint32_t r=io.read(RBRead),w=io.read(RBWrite),me=io.read(MEControl);
 const uint32_t vb=io.read(VBIFHalt),mask=io.read(InterruptMask);
 if(r==UINT32_MAX||w==UINT32_MAX||me==UINT32_MAX||vb==UINT32_MAX||mask==UINT32_MAX||r!=w)return false;
 if(running ? (me!=0||vb!=0) : ((me&Halt)!=Halt))return false;
 s.mask=mask;s.cursor=w;s.resume=running;
 io.write(InterruptMask,0);io.write(MEControl,Halt);io.write(VBIFHalt,15);
 bool ack=false;
 for(unsigned n=0;n<10000;++n){
  uint32_t m=io.read(MEControl),v=io.read(VBIFHalted);
  if(m==UINT32_MAX||v==UINT32_MAX)return false;
  if((m&Halt)==Halt&&(v&15)==15){ack=true;break;}
  io.delay(10);
 }
 if(!ack)return false;
 const uint32_t status=io.read(Status),status3=io.read(Status3);
 const uint32_t rr=io.read(RBRead),ww=io.read(RBWrite);
 return status!=UINT32_MAX&&status3!=UINT32_MAX&&rr==s.cursor&&ww==s.cursor&&
        idleStatus(status)&&!(status3&(1u<<24));
}
template<class IO> bool resumeAfterTables(IO &io,const DrainState &s) {
 if(!s.resume)return true;
 io.publish();io.write(VBIFHalt,0);io.write(MEControl,0);
 if(io.read(MEControl)!=0||io.read(VBIFHalt)!=0||io.read(RBRead)!=s.cursor||io.read(RBWrite)!=s.cursor)return false;
 io.write(InterruptMask,s.mask);return true;
}
}
