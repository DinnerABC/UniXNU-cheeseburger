// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "CheeseburgerGpuStart.h"
namespace CheeseburgerGpu {
enum DrainReason : uint32_t {
 DrainNone=0,DrainInitialInvalid,DrainRingBusy,DrainControlMismatch,
 DrainAckInvalid,DrainAckTimeout,DrainFinalInvalid,DrainCursorChanged,
 DrainNotIdle,DrainStatus3Busy,DrainResumeInvalid,DrainResumeControl,
 DrainResumeCursor,DrainPreIdleInvalid,DrainPreIdleTimeout
};
struct DrainState {
 uint32_t mask=0,cursor=0;bool resume=false;
 uint32_t stage=0,reason=DrainNone,valid=0,wait=0;
 uint32_t rptr=0,wptr=0,me=0,vbif=0,ackme=0,ackvb=0,status=0,status3=0,endr=0,endw=0;
 uint32_t preStatus=0,preStatus3=0,cpStatus=0,cpStatus3=0,cpMe=0,cpRead=0,cpWrite=0,cpValid=0;
};
template<class IO> bool drainForTables(IO &io,bool running,DrainState &s) {
 s=DrainState{};s.stage=1;
 s.rptr=io.read(RBRead);s.wptr=io.read(RBWrite);s.me=io.read(MEControl);
 s.vbif=io.read(VBIFHalt);s.mask=io.read(InterruptMask);s.valid=0x1f;
 if(s.rptr==UINT32_MAX||s.wptr==UINT32_MAX||s.me==UINT32_MAX||s.vbif==UINT32_MAX||s.mask==UINT32_MAX){s.reason=DrainInitialInvalid;return false;}
 if(s.rptr!=s.wptr){s.reason=DrainRingBusy;return false;}
 if(running ? (s.me!=0||s.vbif!=0) : ((s.me&Halt)!=Halt)){s.reason=DrainControlMismatch;return false;}
 s.cursor=s.wptr;s.resume=running;
 if(running){
  s.stage=7;bool idle=false;
  for(unsigned n=0;n<10000;++n){
   s.status=io.read(Status);s.status3=io.read(Status3);s.valid|=0x180;s.wait=n*10;
   if(s.status==UINT32_MAX||s.status3==UINT32_MAX){s.reason=DrainPreIdleInvalid;return false;}
   if(idleStatus(s.status)&&!(s.status3&(1u<<24))){idle=true;break;}
   io.delay(10);
  }
  if(!idle){s.reason=DrainPreIdleTimeout;return false;}
 }
 s.preStatus=s.status;s.preStatus3=s.status3;
 s.stage=2;
 io.write(InterruptMask,0);io.write(MEControl,Halt);
 s.cpMe=io.read(MEControl);s.cpStatus=io.read(Status);s.cpStatus3=io.read(Status3);
 s.cpRead=io.read(RBRead);s.cpWrite=io.read(RBWrite);s.cpValid=0x1f;
 io.write(VBIFHalt,15);
 bool ack=false;
 for(unsigned n=0;n<10000;++n){
  s.ackme=io.read(MEControl);s.ackvb=io.read(VBIFHalted);s.valid|=0x60;s.wait=n*10;
  if(s.ackme==UINT32_MAX||s.ackvb==UINT32_MAX){s.reason=DrainAckInvalid;return false;}
  if((s.ackme&Halt)==Halt&&(s.ackvb&15)==15){ack=true;break;}
  io.delay(10);
 }
 if(!ack){s.reason=DrainAckTimeout;return false;}
 s.stage=3;s.status=io.read(Status);s.status3=io.read(Status3);
 s.endr=io.read(RBRead);s.endw=io.read(RBWrite);s.valid|=0x780;
 if(s.status==UINT32_MAX||s.status3==UINT32_MAX||s.endr==UINT32_MAX||s.endw==UINT32_MAX){s.reason=DrainFinalInvalid;return false;}
 if(s.endr!=s.cursor||s.endw!=s.cursor){s.reason=DrainCursorChanged;return false;}
 if(!idleStatus(s.status)){s.reason=DrainNotIdle;return false;}
 if(s.status3&(1u<<24)){s.reason=DrainStatus3Busy;return false;}
 s.stage=4;return true;
}
template<class IO> bool resumeAfterTables(IO &io,DrainState &s) {
 if(!s.resume)return true;
 s.stage=5;io.publish();io.write(VBIFHalt,0);io.write(MEControl,0);
 s.me=io.read(MEControl);s.vbif=io.read(VBIFHalt);s.endr=io.read(RBRead);s.endw=io.read(RBWrite);
 if(s.me==UINT32_MAX||s.vbif==UINT32_MAX||s.endr==UINT32_MAX||s.endw==UINT32_MAX){s.reason=DrainResumeInvalid;return false;}
 if(s.me||s.vbif){s.reason=DrainResumeControl;return false;}
 if(s.endr!=s.cursor||s.endw!=s.cursor){s.reason=DrainResumeCursor;return false;}
 io.write(InterruptMask,s.mask);s.stage=6;return true;
}
}
