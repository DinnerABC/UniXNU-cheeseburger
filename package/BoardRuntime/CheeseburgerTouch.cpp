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
volatile uint64_t cb_touch_extra[40];
extern volatile uint32_t native_hid_stage;
int cheeseburger_touch_hid_start(void);
IOReturn armcpu_spi_attach(uint32_t,bool,void(*)(void*),void*,uint32_t*,uint64_t*) __attribute__((weak_import));
}
namespace {
struct Window {IOMemoryMap *map=nullptr;volatile uint32_t *v=nullptr;};
static Window qup,gcc,pins[4];
static IOLock *sinkLock;
static void (*sink)(void *);static void *sinkCtx;
static CbRmi rmi;
static CbRmiWire wire;
static unsigned rxBlock,rxFifo,txFifo,clockCtl,transform=3;
static bool irqMode;static volatile uint32_t pending;
static bool map(Window &w,uint64_t pa,unsigned size){IODeviceMemory *m=IODeviceMemory::withRange(pa,size);w.map=m?m->map(kIOMapAnywhere|kIOMapInhibitCache):nullptr;OSSafeReleaseNULL(m);w.v=w.map?(volatile uint32_t*)w.map->getVirtualAddress():nullptr;return w.v;}
static uint32_t rd(Window &w,unsigned off){uint32_t x=w.v[off/4];__asm__ volatile("dmb sy":::"memory");return x;}
static void wr(Window &w,unsigned off,uint32_t x){w.v[off/4]=x;__asm__ volatile("dsb sy":::"memory");}
static int state(unsigned s){for(unsigned i=0;i<200;++i){if(rd(qup,4)&4){wr(qup,4,s);for(unsigned j=0;j<200;++j){if((rd(qup,4)&7)==(s|4))return 0;IODelay(1);}break;}IODelay(1);}return -2;}
// QUP v1 I2C programming taken from the reference i2c-qup.c: every OUT FIFO
// word carries two 16-bit halves, each half is (tag<<8)|byte, with START 0x100,
// DATA 0x200, STOP 0x300 and REC 0x400. A register read is two transactions:
// write the register address (START..STOP), then a separate read transaction
// (START|addrR plus a REC tag). The previous code pushed raw data words with no
// tags or slave address, so nothing was ever addressed.
enum {QUP_CFG_WRITE=0x28F, QUP_CFG_READ=0x20F, QUP_IO_REPACK=0xC000u, QUP_IO_OUT_BLK=0x400u, QUP_IO_IN_BLK=0x1000u};
static int qupPrepare(unsigned config,unsigned io,unsigned outCnt,unsigned writeCnt,unsigned inputCnt,unsigned readCnt){
 int e;
 wr(qup,0xc,1);wire.page=256;if((e=state(0)))return e;
 if(rd(qup,0x404)&0x100)return -4;
 wr(qup,0,config);wr(qup,8,io);
 wr(qup,0x100,outCnt);wr(qup,0x150,writeCnt);wr(qup,0x200,inputCnt);wr(qup,0x208,readCnt);
 wr(qup,0x18,0);wr(qup,0x1c,0x7c);
 if((e=state(1)))return e;
 wr(qup,0x400,clockCtl);
 if((e=state(3)))return e;
 return 0;
}
static int qupFinish(unsigned nr,uint8_t *rx,unsigned &got){
 int e=0;
 for(unsigned ms=0;ms<50;++ms){
  uint32_t op=rd(qup,0x18),status=rd(qup,0x404),err=rd(qup,0x1c);
  cb_touch[17]=status;cb_touch[18]=err;cb_touch[19]=op;
  if((status&0x38000fc)||(err&0x7c)){e=(status&8)?-6:-7;break;}
  if(op&0x100)wr(qup,0x18,0x100);
  if(nr){
   // One REC tag of len bytes: when the input service flag rises the whole
   // block is in the FIFO, two bytes per word. Draining it is the completion.
   if(op&0x200){wr(qup,0x18,0x200);
    while(got<nr){uint32_t w=rd(qup,0x218);rx[got++]=(uint8_t)w;if(got<nr)rx[got++]=(uint8_t)(w>>16);}}
   if(got==nr){state(0);return 0;}
  }else if((op&0x400) && !(status&0x100)){state(0);return 0;}
  IOSleep(1);
 }
 return e?e:-8;
}
static int qupWrite(const uint8_t *tx,unsigned nt){
 // addr + data halves; block mode only when they do not fit the OUT FIFO.
 unsigned halves=nt+1;bool blk=halves>txFifo;int e;
 if((e=qupPrepare(QUP_CFG_WRITE,QUP_IO_REPACK|(blk?QUP_IO_OUT_BLK:0),blk?halves:0,blk?0:halves,0,0)))return e;
 for(unsigned h=0;h<halves;h+=2){
  uint32_t low,high=0;
  if(h==0)low=0x100u|0x40u;                       // START | slave address (write)
  else{unsigned k=h-1;low=(h==halves-1?0x300u:0x200u)|tx[k];}
  if(h+1<halves){unsigned k=h;high=(h+1==halves-1?0x300u:0x200u)|tx[k];}
  wr(qup,0x110,low|(high<<16));
 }
 if((e=state(1)))return e;
 unsigned got=0;return qupFinish(0,nullptr,got);
}
static int qupRead(uint8_t *rx,unsigned nr){
 bool blk=nr>rxFifo;int e;
 if((e=qupPrepare(QUP_CFG_READ,QUP_IO_REPACK|(blk?QUP_IO_IN_BLK:0),0,0,blk?nr:0,blk?0:nr)))return e;
 wr(qup,0x110,(0x100u|0x41u)|((uint32_t)(0x400u|(nr==256?0:nr))<<16));  // START|addrR, REC len
 if((e=state(1)))return e;
 unsigned got=0;e=qupFinish(nr,rx,got);
 if(!e && got!=nr)e=-8;
 return e;
}
static int transfer(const uint8_t *tx,unsigned nt,uint8_t *rx,unsigned nr){
 if(nt>16||nr>256)return -3;
 int e=0;
 if(nt)e=qupWrite(tx,nt);
 if(!e && nr)e=qupRead(rx,nr);
 if(!e){++cb_touch[1];return 0;}
 wr(qup,0xc,1);wire.page=256;++cb_touch[2];return e;
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
 if(fn)fn(owner);if(owner)owner->release();
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
 cb_touch[12]=rd(qup,0x30);unsigned mode=rd(qup,8),sizes[]={4,16,32};unsigned ob=mode&3,ib=(mode>>5)&3;
 if(ob>2||ib>2)return -56;txFifo=sizes[ob]*(2<<((mode>>2)&7));rxBlock=sizes[ib];rxFifo=rxBlock*(2<<((mode>>7)&7));
 cb_touch_extra[2]=mode; // Raw PDT entries as the sensor reports them (page 0, top three entries), so
 // the scan result can be checked against real bytes instead of inference.
 {
  uint8_t pg[2]={0xff,0x00};transfer(pg,2,nullptr,0);
  for(int i=0;i<3;++i){uint8_t d[6]={0},a[1]={(uint8_t)(0xE9-6*i)};transfer(a,1,d,6);
   cb_touch_extra[26+i]=(uint64_t)d[0]|((uint64_t)d[1]<<8)|((uint64_t)d[2]<<16)|((uint64_t)d[3]<<24)|((uint64_t)d[4]<<32)|((uint64_t)d[5]<<40);}
  // Same register with a two-byte address (page, offset), as some vendor
  // transports send it, to see which form actually latches.
  for(int i=0;i<3;++i){uint8_t d[6]={0},a[2]={0x00,(uint8_t)(0xE9-6*i)};transfer(a,2,d,6);
   cb_touch_extra[29+i]=(uint64_t)d[0]|((uint64_t)d[1]<<8)|((uint64_t)d[2]<<16)|((uint64_t)d[3]<<24)|((uint64_t)d[4]<<32)|((uint64_t)d[5]<<40);}
 }
 clockCtl=0x300|93;
 cb_touch_extra[6]=rd(qup,0x0)|((uint64_t)rd(qup,0x4)<<32);cb_touch_extra[7]=rd(qup,0x30);cb_touch_extra[21]=rd(gcc,0x52004);
 // Raw framing probe: read the same page-0 PDT head with the plain address
 // frame and with the previous 0x81/0x82/0x87 frame, so the wire format is
 // chosen from returned bytes instead of assumption.
 {
  uint8_t a[8]={0},b[12]={};int ea,eb;
  uint8_t pg[2]={0xff,0x00};transfer(pg,2,nullptr,0);
  uint8_t t1[1]={0xE9};ea=transfer(t1,1,a,6);
  uint8_t t2[]={0x81,0x40,0x83,2,0xff,0x00};transfer(t2,sizeof t2,nullptr,0);
  uint8_t t3[]={0x81,0x40,0x82,1,0xE9,0x81,0x41,0x87,6};eb=transfer(t3,sizeof t3,b,8);
  cb_touch_extra[24]=(uint64_t)(uint32_t)ea|((uint64_t)a[0]<<32)|((uint64_t)a[1]<<40)|((uint64_t)a[2]<<48)|((uint64_t)a[3]<<56);
  cb_touch_extra[25]=(uint64_t)(uint32_t)eb|((uint64_t)b[2]<<32)|((uint64_t)b[3]<<40)|((uint64_t)b[4]<<48)|((uint64_t)b[5]<<56);
 }
 return 0; // 19.2MHz / (2*(93+3)) = 100kHz.
}
static void worker(void*,wait_result_t){
 thread_set_thread_name(current_thread(),"cheeseburger-S3320");int e=hardware();if(e)goto fail;
 cb_touch[0]=2;wire={nullptr,wireTransfer,256};rmi.bus={nullptr,readRmi,writeRmi};e=cb_discover(rmi);if(e)goto fail;
 cb_touch[0]=3;cb_touch[3]=rmi.f01.query;cb_touch[4]=rmi.f12.query;cb_touch[8]=rmi.maxx;cb_touch[9]=rmi.maxy;cb_touch[20]=rmi.objects;cb_touch[21]=rmi.data1;cb_touch[22]=rmi.packet;
 PE_parse_boot_argn("cheeseburger-touch-transform",&transform,sizeof transform);transform&=7;
 for(unsigned i=0;i<100 && native_hid_stage!=3;++i)IOSleep(50);
 e=cheeseburger_touch_hid_start();if(e)goto fail;
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
extern "C" void native_touch_set_sink(void(*fn)(void*),void *ctx){if(!sinkLock)return;IOLockLock(sinkLock);sink=fn;sinkCtx=ctx;IOLockUnlock(sinkLock);}
extern "C" void cheeseburger_touch_probe(void){unsigned enabled=0;if(!PE_parse_boot_argn("cheeseburger-touch",&enabled,sizeof enabled)||enabled!=1||cb_touch[0])return;cb_touch[0]=1;sinkLock=IOLockAlloc();if(!sinkLock){cb_touch[0]=UINT64_MAX;return;}thread_t t=nullptr;if(kernel_thread_start(worker,nullptr,&t)==KERN_SUCCESS)thread_deallocate(t);else{cb_touch[5]=uint64_t(-60);cb_touch[0]=UINT64_MAX;}}
