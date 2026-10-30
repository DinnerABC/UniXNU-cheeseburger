// SPDX-License-Identifier: GPL-2.0-only
// MSM8998 QUP v2 PIO and RMI4 input. Register definitions/sequence adapted
// from Linux i2c-qup.c, pinctrl-msm8998.c, gcc-msm8998.c; fixed pin in P22 docs.
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <libkern/c++/OSObject.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <pexpert/pexpert.h>
#include "SynapticsRMI4.h"
extern "C" {
volatile uint32_t native_touch_seq,native_touch_count,native_touch_hid_reports;
volatile int native_touch_xs[12],native_touch_ys[12],native_touch_ids[12];
volatile uint64_t cb_touch[28];
volatile uint64_t cb_touch_extra[80];
extern volatile uint32_t native_hid_stage;
int cheeseburger_touch_hid_start(void);
int cheeseburger_touch_sink_ready(void);
IOReturn armcpu_spi_attach(uint32_t,bool,void(*)(void*),void*,uint32_t*,uint64_t*) __attribute__((weak_import));
}
namespace {
struct Window {IOMemoryMap *map=nullptr;volatile uint32_t *v=nullptr;};
static Window qup,gcc,pins[4];
static IOLock *sinkLock;
static void (*sink)(void *);static void *sinkCtx;
// A sink request can arrive before this driver allocates sinkLock (the HID probe
// starts the combined keyboard+digitizer device first); keep it and apply it later.
static void (*pendingSink)(void *);static void *pendingCtx;
static CbRmi rmi;
static CbRmiWire wire;
static unsigned clockCtl,transform=3;
static bool irqMode;static volatile uint32_t pending;
static bool map(Window &w,uint64_t pa,unsigned size){IODeviceMemory *m=IODeviceMemory::withRange(pa,size);w.map=m?m->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;OSSafeReleaseNULL(m);w.v=w.map?(volatile uint32_t*)w.map->getVirtualAddress():nullptr;return w.v;}
static uint32_t rd(Window &w,unsigned off){uint32_t x=w.v[off/4];__asm__ volatile("dmb sy":::"memory");return x;}
static void wr(Window &w,unsigned off,uint32_t x){w.v[off/4]=x;__asm__ volatile("dsb sy":::"memory");}
// msm8998 BLSP1 QUP5 is "qcom,i2c-qup-v2.2.1" (captured DTS), so the QUP v2
// programming of the reference i2c-qup.c applies: QUP_CONFIG is I2C mini core
// with N=7 (0x207), QUP_I2C_MASTER_GEN(0x408) bit0 enables the v2 tag format,
// and every FIFO byte stream is a tag stream (0x81 addr, 0x82/0x83 len,
// 0x87 len) followed by raw bytes packed four per word. A register read is a
// single i2c transfer with two messages, exactly like rmi_i2c.c: address write
// without STOP, then the read message with STOP (repeated start).
enum {
 QP_CONFIG=0x207, QP_NO_INPUT=0x80,
 QP_TAG_START=0x81, QP_TAG_WRITE=0x82, QP_TAG_WRITE_STOP=0x83, QP_TAG_READ_STOP=0x87,
 QP_IO_REPACK=0xC000u, QP_IO_OUT_BLK=0x400u, QP_IO_IN_BLK=0x1000u,
 QP_OP_OUT_SVC=0x100u, QP_OP_IN_SVC=0x200u, QP_OP_OUT_DONE=0x400u, QP_OP_IN_BLK_REQ=0x2000u,
 QP_STATUS_NACK=0x8u, QP_STATUS_MASK=0x38000fcu, QP_STATUS_BUSY=0x100u,
};
static unsigned outBlock,inBlock,outFifo,inFifo;
static int qupState(unsigned req,unsigned mask){
 for(unsigned i=0;i<200;++i){uint32_t v=rd(qup,4);if((v&4)&&((v&mask)==req))return 0;IODelay(1);}
 return -2;
}
static int qupSetState(unsigned s){int e;if((e=qupState(0,0)))return e;wr(qup,4,s);return qupState(s,3);}
// FIFO/block sizes are read from QUP_IO_MODE, as the reference probe does:
// block size is {4,16,32}, FIFO size is block * (2 << fifo_size_field).
static int qupSizes(unsigned m){
 static const unsigned bs[]={4,16,32};
 unsigned ob=m&3,ib=(m>>5)&3;
 if(ob>2||ib>2)return -56;
 outBlock=bs[ob];outFifo=outBlock*(2<<((m>>2)&7));
 inBlock=bs[ib];inFifo=inBlock*(2<<((m>>7)&7));
 cb_touch_extra[40]=outFifo;cb_touch_extra[41]=inFifo;
 cb_touch_extra[42]=outBlock;cb_touch_extra[43]=inBlock;
 return 0;
}
static int qupInit(){
 int e;
 wr(qup,0xc,1);
 if((e=qupState(0,3)))return e;
 if(!outFifo){unsigned m=rd(qup,8);   // defaults are only valid in reset state
  if(!qupSizes(m))cb_touch_extra[2]=m;
  else if(qupSizes(unsigned(cb_touch_extra[44])))return -56;}
 uint32_t st=rd(qup,0x404)&QP_STATUS_MASK;if(st)wr(qup,0x404,st);
 wr(qup,0,unsigned(QP_CONFIG));wr(qup,0x408,1);
 if((e=qupState(0x10,0x10)))return e;   // HW selected I2C master mode
 wr(qup,0x1c,0x7c);
 return 0;
}
static void qupPush(const uint8_t *p,unsigned n){
 for(unsigned i=0;i<n;i+=4){uint32_t w=0;for(unsigned k=0;k<4&&i+k<n;++k)w|=uint32_t(p[i+k])<<(8*k);wr(qup,0x110,w);}
}
static uint64_t packBytes(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n&&i<8;++i)v|=uint64_t(p[i])<<(8*i);return v;}
static int qupErrors(){
 uint32_t st=rd(qup,0x404),er=rd(qup,0x1c);cb_touch[17]=st;cb_touch[18]=er;
 if(st&QP_STATUS_MASK){wr(qup,0x404,st&QP_STATUS_MASK);return (st&QP_STATUS_NACK)?-6:-7;}
 if(er&0x7c){wr(qup,0x1c,er&0x7c);return -7;}
 return 0;
}
static int qupTransfer(uint8_t addr,const uint8_t *tx,unsigned nt,uint8_t *rx,unsigned nr){
 unsigned txLen=nt?nt+4:0,rxLen=nr?nr+2:0;
 if(!nt&&!nr)return -3;
 int e=qupInit();if(e)return e;
 // 254 is RECV_MAX_DATA_LEN: a longer read would need the two-block tag split,
 // and a write above the OUT FIFO would need the OUT block handshake.
 if(txLen>outFifo||nr>254)return -4;
 bool rxBlk=rxLen>inFifo;
 wr(qup,8,unsigned(QP_IO_REPACK|(rxBlk?QP_IO_IN_BLK:0)));
 wr(qup,0x100,0);wr(qup,0x150,0);wr(qup,0x200,0);wr(qup,0x208,0);
 uint8_t wa=uint8_t(0x40u|(addr<<1)),ra=uint8_t(wa|1);
 if(nt){
  wr(qup,0x150,txLen);wr(qup,0x208,0);wr(qup,0,unsigned(QP_CONFIG)|(nr?0:QP_NO_INPUT));
  if((e=qupSetState(1)))return e;
  wr(qup,0x400,clockCtl);
  if((e=qupSetState(3)))return e;
  uint8_t h[4]={QP_TAG_START,wa,uint8_t(nr?QP_TAG_WRITE:QP_TAG_WRITE_STOP),uint8_t(nt)};
  qupPush(h,sizeof h);qupPush(tx,nt);
  if((e=qupSetState(1)))return e;
  e=-8;
  for(unsigned ms=0;ms<50;++ms){
   if((e=qupErrors()))break;
   uint32_t op=rd(qup,0x18);cb_touch[19]=op;
   if(op&QP_OP_OUT_SVC)wr(qup,0x18,QP_OP_OUT_SVC);
   if(op&QP_OP_OUT_DONE){wr(qup,0x18,QP_OP_OUT_DONE);e=0;break;}
   IOSleep(1);
  }
  if(e)return e;
  if(nr&&(e=qupSetState(3)))return e;
 }
 if(nr){
  // QUP_I2C_MX_CONFIG_DURING_RUN on every count of a re-configured sub-transfer.
  unsigned run=nt?0x80000000u:0;
  wr(qup,0x150,4|run);wr(qup,0,unsigned(QP_CONFIG));
  if(rxBlk){wr(qup,0x208,0);wr(qup,0x200,rxLen|run);}
  else{wr(qup,0x200,0);wr(qup,0x208,rxLen|run);}
  if(!nt){
   if((e=qupSetState(1)))return e;
   wr(qup,0x400,clockCtl);
   if((e=qupSetState(3)))return e;
  }
  uint8_t h[4]={QP_TAG_START,ra,QP_TAG_READ_STOP,uint8_t(nr)};
  qupPush(h,sizeof h);
  if((e=qupSetState(1)))return e;
  // The first two bytes in the RX FIFO are the received tags; data follows.
  unsigned got=0,skip=2,stream=rxLen,used=0;e=-8;
  for(unsigned ms=0;ms<100;++ms){
   if((e=qupErrors()))break;
   uint32_t op=rd(qup,0x18);cb_touch[19]=op;
   if(op&QP_OP_OUT_SVC)wr(qup,0x18,QP_OP_OUT_SVC);
   // FIFO mode grants the FIFO worth of bytes on IN_SVC; block mode hands one
   // block at a time and the block request is the handshake (never cleared here).
   bool have=rxBlk?((op&QP_OP_IN_BLK_REQ)!=0):((op&QP_OP_IN_SVC)!=0);
   if(op&QP_OP_IN_SVC)wr(qup,0x18,QP_OP_IN_SVC);
   if(have){
    unsigned grant=stream-used;
    if(rxBlk){if(grant>inBlock)grant=inBlock;}
    else if(grant>inFifo)grant=inFifo;
    for(unsigned i=0;i<(grant+3)/4;++i){uint32_t w=rd(qup,0x218);
     for(unsigned k=0;k<4&&i*4+k<grant;++k){if(skip)--skip;else if(got<nr)rx[got++]=(w>>(8*k))&255;}}
    used+=grant;
    if(got==nr){e=0;break;}
   }
   IOSleep(1);
  }
  if(e)return e;
 }
 // Same end-of-transfer check as the reference: the STOP must have been sent.
 for(unsigned i=0;i<2000&&(rd(qup,0x404)&QP_STATUS_BUSY);++i)IODelay(10);
 if(rd(qup,0x404)&QP_STATUS_BUSY)return -9;
 qupSetState(0);
 return 0;
}
static int transfer(const uint8_t *tx,unsigned nt,uint8_t *rx,unsigned nr){
 int e=qupTransfer(0x20,tx,nt,rx,nr);
 if(e){wr(qup,0xc,1);wire.page=256;++cb_touch[2];return e;}
 ++cb_touch[1];return 0;
}
static int wireTransfer(void*,const uint8_t *t,unsigned nt,uint8_t *p,unsigned np){return transfer(t,nt,p,np);}
static int readRmi(void*,uint16_t a,uint8_t *p,unsigned n){return cb_wire_read(wire,a,p,n);}
static int writeRmi(void *,uint16_t a,const uint8_t *v,unsigned n){
 // This backend deliberately cannot write arbitrary function/flash registers.
 bool ctrl=a==rmi.f01.control&&n==1,irq=a==rmi.f01.control+1&&n<=4;
 if(!rmi.f01.control||(!ctrl&&!irq)||!n)return -10;
 int e=cb_wire_page(wire,a>>8);if(e)return e;uint8_t t[10]={uint8_t(a)};memcpy(t+1,v,n);e=transfer(t,n+1,nullptr,0);if(!e)wire.page=a>>8;return e;
}
static void publish(const CbTouchFrame &f){
 bool same=native_touch_count==f.count;for(unsigned i=0;i<f.count&&same;++i)same=native_touch_xs[i]==f.x[i]&&native_touch_ys[i]==f.y[i]&&native_touch_ids[i]==f.id[i];if(same)return;
 __atomic_add_fetch(&native_touch_seq,1,__ATOMIC_ACQ_REL);native_touch_count=f.count;
 for(unsigned i=0;i<f.count;++i){native_touch_xs[i]=f.x[i];native_touch_ys[i]=f.y[i];native_touch_ids[i]=f.id[i];}
 __atomic_add_fetch(&native_touch_seq,1,__ATOMIC_RELEASE);cb_touch[6]=f.count;++cb_touch[7];
 IOLockLock(sinkLock);auto fn=sink;OSObject *owner=fn?static_cast<OSObject*>(sinkCtx):nullptr;if(owner)owner->retain();IOLockUnlock(sinkLock);
 if(fn){++cb_touch_extra[74];fn(owner);}if(owner)owner->release();
}
static void interrupt(void*){
 // Mask only this GPIO immediately; all I2C/HID work stays in worker context.
 uint32_t st=rd(pins[3],12);if(st&1){wr(pins[3],8,rd(pins[3],8)&~1u);wr(pins[3],12,0);__atomic_store_n(&pending,1,__ATOMIC_RELEASE);++cb_touch[10];}
}
static int hardware(){
 // Actual captured FDT + mainline MSM8998 pin tiles: EAST base + gpio*0x1000.
 if(!map(gcc,0x100000,0x53000))return -50;
 cb_touch[13]=rd(gcc,0x21024);unsigned cfg=cb_touch[13];
 // Require firmware's XO/1 19.2MHz source. Never retune shared PLL/RCG.
 // Linux clk-rcg2 calc_rate: HID divider zero bypasses division, XO stays 19.2MHz.
 if((cfg&0x700)!=0 || (cfg&0x1f)>1)return -51;
 wr(gcc,0x52004,rd(gcc,0x52004)|(1u<<17));wr(gcc,0x21008,rd(gcc,0x21008)|1);
 for(unsigned i=0;i<20 && ((rd(gcc,0x21008)|rd(gcc,0x17004))&0x80000000);++i)IOSleep(1);
 if((rd(gcc,0x21008)|rd(gcc,0x17004))&0x80000000)return -52;
 const unsigned gp[]={87,88,89,125};for(unsigned i=0;i<4;++i)if(!map(pins[i],0x3400000+0x900000+gp[i]*0x1000,0x1000))return -53;
 // Pad levels as the firmware left them, before any write of ours.
 cb_touch_extra[14]=(rd(pins[0],4)&1)|((rd(pins[1],4)&1)<<1)|((rd(pins[2],4)&1)<<2)|((rd(pins[3],4)&1)<<3);
 for(unsigned i=0;i<2;++i)wr(pins[i],0,(rd(pins[i],0)&~(0x3cu|0x200u))|(3u<<2));
 cb_touch_extra[20]=(rd(pins[0],4)&1)|((rd(pins[1],4)&1)<<1);
 {
  unsigned b0=rd(pins[0],0),b1=rd(pins[1],0);
  wr(pins[0],0,(b0&~(0x3fu|0x200u))|3u);   // pull bits are [1:0] on this TLMM
  wr(pins[1],0,(b1&~(0x3fu|0x200u))|3u);
  IOSleep(1);
  cb_touch_extra[15]=(rd(pins[0],4)&1)|((rd(pins[1],4)&1)<<1);
  wr(pins[0],0,b0);wr(pins[1],0,b1);
 }
 // Read-only idle levels of the I2C pads: with the bus muxed but the controller
 // still disabled they show whether the sensor's IO rail (and its pull-ups) is
 // powered. bit0 SDA87, bit1 SCL88, bit2 reset89, bit3 attention125.
 cb_touch[27]=(rd(pins[0],4)&1)|((rd(pins[1],4)&1)<<1);
 // Captured FDT ts_reset_active: GPIO89, function gpio (mux 0), 8 mA, bias-pull-up.
 // msm8998 gpio89 has no i2c/blsp alternate: mux 3 would hand the pad to nothing.
 // pinctrl-msm reads the pad in IO bit 0 and drives it from bit 1, so the level
 // must be written to 0x2. Writing 0x1 only touches the read-only input field
 // and the pad stayed low (TOUCH error=-58).
 const unsigned cfg89=(rd(pins[2],0)&~(0x3fu|(7u<<6)|0x200u))|(3u<<6)|3u;
 wr(pins[2],4,0);IOSleep(2);
 wr(pins[2],0,cfg89|0x200u);
 wr(pins[2],4,2);IOSleep(50);
 cb_touch[14]=rd(pins[2],4)&1;
 if(!(cb_touch[14]&1)) { wr(pins[2],0,cfg89);return -58; }
 wr(pins[3],0,(rd(pins[3],0)&~(0x3fu|0x200u))|3u);
 cb_touch[15]=rd(pins[3],0);cb_touch[16]=rd(pins[3],8);
 cb_touch[27]|=((rd(pins[2],4)&1)<<2)|((rd(pins[3],4)&1)<<3);
 // Pull-up experiment: with the controller idle a weak SoC pull-up separates
 // "the external pull-up supply is missing" (line rises) from "an unpowered
 // device clamps the pad" (line stays low). The original pad setting is
 // restored immediately; nothing is left driven.
 {
  unsigned a0=rd(pins[0],0),a1=rd(pins[1],0);
  wr(pins[0],0,(a0&~3u)|3u);wr(pins[1],0,(a1&~3u)|3u);IOSleep(1);
  cb_touch_extra[12]=(rd(pins[0],4)&1)|((rd(pins[1],4)&1)<<1)|((rd(pins[2],4)&1)<<2)|((rd(pins[3],4)&1)<<3);
  wr(pins[0],0,a0);wr(pins[1],0,a1);
  cb_touch_extra[13]=(rd(pins[0],4)&1)|((rd(pins[1],4)&1)<<1);
 }
 // Drive test: push both pads high as GPIO outputs for about a millisecond.
 // Reading 0 back means something outside the SoC holds the bus low; reading 1
 // means the pads are free and the earlier low level came from our own
 // controller configuration. The pads are restored immediately.
 {
  unsigned d0=rd(pins[0],0),d1=rd(pins[1],0);
  wr(pins[0],0,(d0&~(0x3fu|(7u<<6)))|(3u<<6)|0x200u);
  wr(pins[1],0,(d1&~(0x3fu|(7u<<6)))|(3u<<6)|0x200u);
  wr(pins[0],4,2);wr(pins[1],4,2);IOSleep(1);
  cb_touch_extra[22]=(rd(pins[0],4)&1)|((rd(pins[1],4)&1)<<1);
  wr(pins[0],0,d0);wr(pins[1],0,d1);
  wr(pins[0],4,0);wr(pins[1],4,0);
 }
 cb_touch_extra[23]=rd(gcc,0x21000);   // GCC_BLSP1_QUP5_BCR (block reset)
 cb_touch_extra[0]=rd(gcc,0x21008);cb_touch_extra[1]=rd(gcc,0x21024);
 cb_touch_extra[16]=rd(pins[0],0);cb_touch_extra[17]=rd(pins[1],0);cb_touch_extra[18]=rd(pins[2],0);cb_touch_extra[19]=rd(pins[3],0);
 if(!map(qup,0xc179000,0x1000))return -55;
 cb_touch[12]=rd(qup,0x30);cb_touch_extra[44]=rd(qup,8);   // IO_MODE before our reset
 clockCtl=0x300|93;   // must be set before any probe: a zero clock_ctl sends no SCL
 // Raw PDT entries as the sensor reports them (page 0, top three entries), so
 // the scan result can be checked against real bytes instead of inference.
 {
  uint8_t pg[2]={0xff,0x00};cb_touch_extra[45]=(uint64_t)(int64_t)qupTransfer(0x20,pg,2,nullptr,0);
  for(int i=0;i<3;++i){uint8_t d[6]={0},a[1]={(uint8_t)(0xE9-6*i)};int e=qupTransfer(0x20,a,1,d,6);
   if(!i)cb_touch_extra[46]=(uint64_t)(int64_t)e;
   cb_touch_extra[26+i]=(uint64_t)d[0]|((uint64_t)d[1]<<8)|((uint64_t)d[2]<<16)|((uint64_t)d[3]<<24)|((uint64_t)d[4]<<32)|((uint64_t)d[5]<<40);}
  // Comprehensive probe in one pass: which addresses acknowledge, whether a
  // plain register read returns bytes, and the controller state/tag counters.
  {
   uint64_t m0=0,m1=0;
   for(unsigned a=0x08;a<=0x77;++a){uint8_t z=0;
    if(!qupTransfer((uint8_t)a,nullptr,0,&z,1)){unsigned b=a-0x08;if(b<64)m0|=1ull<<b;else m1|=1ull<<(b-64);}}
   cb_touch_extra[32]=m0;cb_touch_extra[39]=m1;
   uint8_t cmd[1]={0xE9};   // PDT head, not the Device Command register
   uint8_t pgw[2]={0xff,0x00};
   cb_touch_extra[33]=(uint64_t)(int64_t)qupTransfer(0x20,pgw,2,nullptr,0);
   uint8_t r8[8]={0};uint64_t v=0;
   cb_touch_extra[34]=(uint64_t)(int64_t)qupTransfer(0x20,cmd,1,r8,8);
   for(unsigned i=0;i<8;++i)v|=(uint64_t)r8[i]<<(8*i);
   cb_touch_extra[35]=v;
   cb_touch_extra[36]=(uint64_t)(rd(qup,0x4)&0xffff)|((uint64_t)(rd(qup,0x18)&0xffff)<<16)|((uint64_t)(rd(qup,0x1c)&0xffff)<<32);
   cb_touch_extra[37]=rd(qup,0x404);
   cb_touch_extra[38]=(uint64_t)(rd(qup,0x150))|((uint64_t)(rd(qup,0x208))<<32);
  }
  // Register-map probes. The last round returned different bytes for the same
  // address on different accesses (0xE3 read as ...2a once and as fn=01 later),
  // so this pass fixes the access model instead of guessing an orientation:
  // 1. the page register at 0xFF must stay 0x00 across repeated single-byte
  //    reads (a rolling internal pointer would advance);
  // 2. single-byte reads at consecutive addresses build the map without any
  //    multi-byte ordering question;
  // 3. the same six bytes are read twice around the map to show whether the
  //    multi-byte result is repeatable at all;
  // 4. the address is also written with STOP and the block read started with
  //    no address, the way a device that latches only on STOP would need.
  {
   uint8_t pg0[2]={0xff,0x00},pg1[2]={0xff,0x01},ad[1],z=0,m[12];
   uint8_t r4[4]={0},r1=0;
   qupTransfer(0x20,pg0,2,nullptr,0);
   ad[0]=0xfc;qupTransfer(0x20,ad,1,r4,4);cb_touch_extra[48]=packBytes(r4,4);
   ad[0]=0xfc;qupTransfer(0x20,ad,1,&r1,1);cb_touch_extra[50]=r1;
   qupTransfer(0x20,pg1,2,nullptr,0);
   ad[0]=0xfc;qupTransfer(0x20,ad,1,r4,4);cb_touch_extra[49]=packBytes(r4,4);
   qupTransfer(0x20,pg0,2,nullptr,0);
   ad[0]=0xdd;qupTransfer(0x20,ad,1,m,6);cb_touch_extra[26]=packBytes(m,6);
   ad[0]=0xdd;qupTransfer(0x20,ad,1,m,6);cb_touch_extra[60]=packBytes(m,6);
   for(unsigned i=0;i<12;++i){uint8_t a1=(uint8_t)(0xdd+i);qupTransfer(0x20,&a1,1,&z,1);m[i]=z;}
   cb_touch_extra[56]=packBytes(m,8);cb_touch_extra[57]=packBytes(m+8,4);
   ad[0]=0xdd;qupTransfer(0x20,ad,1,m,6);cb_touch_extra[61]=packBytes(m,6);
   ad[0]=0xe9;qupTransfer(0x20,ad,1,m,6);cb_touch_extra[27]=packBytes(m,6);
   for(unsigned i=0;i<12;++i){uint8_t a1=(uint8_t)(0xe9+i);qupTransfer(0x20,&a1,1,&z,1);m[i]=z;}
   cb_touch_extra[58]=packBytes(m,8);cb_touch_extra[59]=packBytes(m+8,4);
   ad[0]=0xe9;qupTransfer(0x20,ad,1,m,6);cb_touch_extra[28]=packBytes(m,6);
   {uint8_t p3[3]={0};for(unsigned i=0;i<3;++i){uint8_t a1=0xff;qupTransfer(0x20,&a1,1,&p3[i],1);}cb_touch_extra[62]=packBytes(p3,3);}
   ad[0]=0xe9;qupTransfer(0x20,ad,1,nullptr,0);      // address write with STOP
   qupTransfer(0x20,nullptr,0,m,6);cb_touch_extra[63]=packBytes(m,6);  // pointer read
   qupTransfer(0x20,pg0,2,nullptr,0);
   ad[0]=0x47;qupTransfer(0x20,ad,1,m,6);cb_touch_extra[53]=packBytes(m,6);
   ad[0]=0x15;qupTransfer(0x20,ad,1,m,6);cb_touch_extra[54]=packBytes(m,6);
   // Where either field order would see F01 (0x01) and F12 (0x12): normal uses
   // byte 5 as the function number, reversed uses byte 0.
   unsigned f01n=0xff,f12n=0xff,f01r=0xff,f12r=0xff;
   for(int off=0xe9;off>=5;off-=6){uint8_t d[6]={0},a[1]={(uint8_t)off};
    if(qupTransfer(0x20,a,1,d,6))continue;
    if(d[5]==1&&f01n==0xff)f01n=unsigned(off);
    if(d[5]==0x12&&f12n==0xff)f12n=unsigned(off);
    if(d[0]==1&&f01r==0xff)f01r=unsigned(off);
    if(d[0]==0x12&&f12r==0xff)f12r=unsigned(off);}
   cb_touch_extra[55]=f01n|(f12n<<8)|(f01r<<16)|(f12r<<24);
  }
 }
 cb_touch_extra[6]=rd(qup,0x0)|((uint64_t)rd(qup,0x4)<<32);cb_touch_extra[7]=rd(qup,0x30);cb_touch_extra[21]=rd(gcc,0x52004);

 return 0; // 19.2MHz / (2*(93+3)) = 100kHz.
}
static void worker(void*,wait_result_t){
 thread_set_thread_name(current_thread(),"cheeseburger-S3320");int e=hardware();if(e)goto fail;
 cb_touch[0]=2;wire={nullptr,wireTransfer,256};rmi.bus={nullptr,readRmi,writeRmi};e=cb_discover(rmi);if(e)goto fail;
 cb_touch[0]=3;cb_touch[3]=rmi.f01.query;cb_touch[4]=rmi.f12.query;cb_touch[8]=rmi.maxx;cb_touch[9]=rmi.maxy;cb_touch[20]=rmi.objects;cb_touch[21]=rmi.data1;cb_touch[22]=rmi.packet;
 PE_parse_boot_argn("cheeseburger-touch-transform",&transform,sizeof transform);transform&=7;
 for(unsigned i=0;i<100 && native_hid_stage!=3;++i)IOSleep(50);
 if(!cheeseburger_touch_sink_ready()){e=cheeseburger_touch_hid_start();if(e)goto fail;}
 cb_touch[0]=4;
 // Optional real GPIO125 -> TLMM summary SPI208 + GIC32. Disabled by default
 // until board IRQ ownership is qualified. Refuse a shared enabled summary.
 {unsigned requested=0;PE_parse_boot_argn("cheeseburger-touch-irq",&requested,sizeof requested);
  if(requested==1 && &armcpu_spi_attach){bool exclusive=true;
   Window tlmm;if(map(tlmm,0x3400000,0xc00000)){
    // Tile locations from pinctrl-msm8998.c; conservatively refuse ANY enabled
    // input outside this pin across all tiles, including unimplemented holes.
    static const uint8_t tiles[150]={9,9,9,9,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,5,5,5,1,1,9,9,9,9,9,9,9,9,9,5,5,5,5,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,1,1,1,1,1,1,1,1,5,5,5,5,5,5,5,5,5,1,1,1,9,9,9,9,9,9,9,9,9,9,1,1,1,5,5,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
    for(unsigned g=0;g<150;++g)if(g!=125 && (rd(tlmm,unsigned(tiles[g])*0x100000+g*0x1000+8)&1))exclusive=false;
    OSSafeReleaseNULL(tlmm.map);
   }else exclusive=false;
   uint32_t rb=0;uint64_t route=0;if(exclusive){wr(pins[3],8,0x7a);wr(pins[3],12,0);if(!armcpu_spi_attach(240,false,interrupt,nullptr,&rb,&route)){irqMode=true;wr(pins[3],8,0x7b);}}
  }cb_touch[23]=irqMode?1:0;
 }
 {unsigned failures=0,tick=0;for(;;){IOSleep(25);++cb_touch[11];
   bool requested=!irqMode||__atomic_exchange_n(&pending,0,__ATOMIC_ACQ_REL)||!(rd(pins[3],4)&1)||((++tick%20)==0);if(!requested)continue;
   uint8_t status[5]={},buf[80]={};e=readRmi(nullptr,rmi.f01.data,status,1+(rmi.irqcount+7)/8);
   if(!e && (status[0]&0xcf))e=-57;
   if(!e)e=readRmi(nullptr,rmi.data1,buf,rmi.packet);
   if(e){publish(CbTouchFrame{});cb_touch[5]=(uint64_t)(int64_t)e;if(++failures>=3)break;IOSleep(25);continue;}
   failures=0;CbTouchFrame frame;e=cb_decode(rmi,buf,rmi.packet,transform,frame);if(e)break;++cb_touch[24];if(frame.count){cb_touch[25]=cb_le16(buf+1)|(uint64_t(cb_le16(buf+3))<<32);cb_touch[26]=unsigned(frame.x[0])|(uint64_t(unsigned(frame.y[0]))<<32);}
   // Keep the first non-zero packet seen without a decoded contact: this tells a
   // silent panel apart from a status/offset mismatch in the decoder.
   if(!frame.count){bool nz=false;for(unsigned i=0;i<rmi.packet;++i)if(buf[i]){nz=true;break;}
    if(nz){++cb_touch_extra[72];cb_touch_extra[64]=packBytes(buf,8);cb_touch_extra[65]=packBytes(buf+8,8);}}
   if(status[0]&&!cb_touch_extra[73])cb_touch_extra[73]=status[0];
   publish(frame);if(irqMode)wr(pins[3],8,rd(pins[3],8)|1);
  }}
 fail:
 // State of the controller and pads at the moment the probe gave up: the
 // registers above 0x400 are only meaningful after a real transaction.
 if(qup.v){cb_touch_extra[8]=rd(qup,0x0);cb_touch_extra[9]=rd(qup,0x400);cb_touch_extra[10]=rd(qup,0x408);cb_touch_extra[11]=rd(qup,0xc);
  cb_touch_extra[3]=rd(qup,0x404);cb_touch_extra[4]=rd(qup,0x18);}
 if(pins[0].v)cb_touch_extra[5]=(rd(pins[0],4)&1)|((rd(pins[1],4)&1)<<1)|((rd(pins[2],4)&1)<<2)|((rd(pins[3],4)&1)<<3);
 if(sinkLock)publish(CbTouchFrame{});if(pins[3].v && irqMode)wr(pins[3],8,rd(pins[3],8)&~1u);
 cb_touch[5]=(uint64_t)(int64_t)e;cb_touch[0]=UINT64_MAX;
 // Keep maps/IRQ context valid for boot lifetime; no callbacks into freed memory.
}
}
extern "C" void native_touch_set_sink(void(*fn)(void*),void *ctx){if(!sinkLock){pendingSink=fn;pendingCtx=ctx;return;}IOLockLock(sinkLock);sink=fn;sinkCtx=ctx;IOLockUnlock(sinkLock);}
// The combined keyboard+digitizer device (sheng-touch-hid=1) attaches its event
// driver on hardware; the standalone digitizer device (sheng-touch-split) does
// not, so a sink installed by the combined device must not be replaced.
extern "C" int cheeseburger_touch_sink_ready(void){return sink?1:0;}
extern "C" void cheeseburger_touch_probe(void){unsigned enabled=0;if(!PE_parse_boot_argn("cheeseburger-touch",&enabled,sizeof enabled)||enabled!=1||cb_touch[0])return;cb_touch[0]=1;sinkLock=IOLockAlloc();if(!sinkLock){cb_touch[0]=UINT64_MAX;return;}
 if(pendingSink){sink=pendingSink;sinkCtx=pendingCtx;pendingSink=nullptr;cb_touch_extra[78]=1;}thread_t t=nullptr;if(kernel_thread_start(worker,nullptr,&t)==KERN_SUCCESS)thread_deallocate(t);else{cb_touch[5]=uint64_t(-60);cb_touch[0]=UINT64_MAX;}}
