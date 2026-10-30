/* SPDX-License-Identifier: GPL-2.0-only
 * Bounded board-neutral RMI4 parser. Descriptor format and F12 object layout:
 * Linux rmi_driver.c/rmi_f12.c, Synaptics Incorporated 2012-2016.
 * See out/p22/reference/linux-pin.json and docs/P22_TOUCH_IMPLEMENTATION.md.
 */
#ifndef CHEESEBURGER_RMI4_H
#define CHEESEBURGER_RMI4_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct CbRmiWire {void *ctx; int (*xfer)(void*,const uint8_t*,unsigned,uint8_t*,unsigned);unsigned page;};
// Plain RMI4-over-I2C framing, as in the reference rmi_i2c.c: the page is
// written to register 0xFF and every access starts with the low address byte.
static inline int cb_wire_page(CbRmiWire &w,unsigned page){if(w.page==page)return 0;uint8_t t[]={0xff,uint8_t(page)};int e=w.xfer(w.ctx,t,sizeof t,nullptr,0);w.page=e?256:page;return e;}
static inline int cb_wire_read(CbRmiWire &w,uint16_t a,uint8_t *p,unsigned n){if(!n||n>255||(a&255)==255)return -9;int e=cb_wire_page(w,a>>8);if(e)return e;uint8_t t[]={uint8_t(a)},raw[257];e=w.xfer(w.ctx,t,sizeof t,raw,n);w.page=e?256:(a>>8);if(!e)memcpy(p,raw,n);return e;}
struct CbRmiBus { void *ctx; int (*read)(void *,uint16_t,uint8_t *,unsigned); int (*write)(void *,uint16_t,const uint8_t *,unsigned); };
struct CbRmiFunction { uint16_t query,command,control,data; unsigned irq,irqs; };
struct CbRmiItem { unsigned number,size,offset,subcount; uint32_t sublow; };
struct CbRmiDesc { CbRmiItem items[64]; unsigned count; };
struct CbTouchFrame { unsigned count; int x[10],y[10],id[10]; };
struct CbRmi { CbRmiBus bus; CbRmiFunction f01,f12; CbRmiDesc query,control,data; unsigned irqcount,maxx,maxy,objects,packet; uint16_t data1; uint8_t identity[21]; };
static inline unsigned cb_le16(const uint8_t *p){return p[0]|unsigned(p[1])<<8;}
static inline const CbRmiItem *cb_item(const CbRmiDesc &d,unsigned n){for(unsigned i=0;i<d.count;++i)if(d.items[i].number==n)return &d.items[i];return nullptr;}
static inline int cb_desc(CbRmi &r,uint16_t addr,CbRmiDesc &d){
 uint8_t n=0,p[35]={},s[255]={};d.count=0;
 if(r.bus.read(r.bus.ctx,addr,&n,1))return -20;
 if(!n||n>sizeof p)return -21;
 if(r.bus.read(r.bus.ctx,addr+1,p,n))return -22;
 unsigned po=p[0]?1:3,sz=p[0]?p[0]:cb_le16(p+1);
 if(n<po||!sz||sz>sizeof s)return -23;
 if(r.bus.read(r.bus.ctx,addr+2,s,sz))return -24;
 unsigned at=0,offset=0;
 for(unsigned bit=0;bit<(n-po)*8;++bit){if(!(p[po+bit/8]&(1u<<(bit%8))))continue;
  if(d.count>=64||at>=sz)return -25;
  CbRmiItem item={bit,s[at++],offset,0,0};
  if(!item.size){if(sz-at<2)return -26;item.size=cb_le16(s+at);at+=2;}
  if(!item.size){if(sz-at<4)return -26;item.size=unsigned(s[at])|unsigned(s[at+1])<<8|unsigned(s[at+2])<<16|unsigned(s[at+3])<<24;at+=4;}
  if(!item.size||item.size>255)return -27;
  unsigned sub=0;uint8_t b;
  do{if(at>=sz||sub>=296)return -28;b=s[at++];for(unsigned k=0;k<7;++k,++sub)if(b&(1u<<k)){++item.subcount;if(sub<32)item.sublow|=1u<<sub;}}while(b&128);
  d.items[d.count++]=item;++offset; // Packet register addresses advance by ONE, not byte size.
 }
 return d.count?0:-29;
}
static inline int cb_discover(CbRmi &r){
 r.f01={};r.f12={};r.irqcount=0;
 unsigned empty=0;
 for(unsigned page=0;page<8 && empty<2;++page){bool any=false;
  for(int off=0xe9;off>=5;off-=6){uint8_t p[6];if(r.bus.read(r.bus.ctx,(page<<8)|off,p,6))return -30;if(!p[5]||p[5]==255)break;
   any=true;CbRmiFunction f={uint16_t((page<<8)|p[0]),uint16_t((page<<8)|p[1]),uint16_t((page<<8)|p[2]),uint16_t((page<<8)|p[3]),r.irqcount,unsigned(p[4]&7)};
   // Function numbers are hex: 0x01 is F01 and 0x12 is F12 (decimal 12 is F0C).
   if(p[5]==0x01)r.f01=f;if(p[5]==0x12)r.f12=f;r.irqcount+=f.irqs;if(r.irqcount>32)return -31;
  }if(any)empty=0;else ++empty;
 }
 if(!r.f01.query||!r.f12.query)return -32;
 uint8_t status=0,q=0;
 if(r.bus.read(r.bus.ctx,r.f01.query,r.identity,sizeof r.identity)||r.bus.read(r.bus.ctx,r.f01.data,&status,1))return -33;
 if(r.identity[0]!=1 || (status&0x40) || (status&15)>1)return -34;
 if(r.bus.read(r.bus.ctx,r.f12.query,&q,1)||!(q&1))return -35;
 int e=cb_desc(r,r.f12.query+1,r.query);if(e)return e;
 e=cb_desc(r,r.f12.query+4,r.control);if(e)return e;
 e=cb_desc(r,r.f12.query+7,r.data);if(e)return e;
 const CbRmiItem *c=cb_item(r.control,8),*d=cb_item(r.data,1);
 if(!c||c->size<4||c->size>15||!(c->sublow&1)||!d||!d->subcount||d->subcount>10||d->size!=8*d->subcount)return -36;
 uint8_t tuning[15]={};if(r.bus.read(r.bus.ctx,r.f12.control+c->offset,tuning,c->size))return -37;
 r.maxx=cb_le16(tuning);r.maxy=cb_le16(tuning+2);r.objects=d->subcount;r.packet=d->size;r.data1=r.f12.data+d->offset;
 if(!r.maxx||!r.maxy)return -38;
 // Only F01 normal/nosleep/configured and its reported interrupt enable bytes.
 // Never issue reset/F34/configuration/firmware commands.
 uint8_t ctrl=0;if(r.bus.read(r.bus.ctx,r.f01.control,&ctrl,1))return -39;
 ctrl=(ctrl&~3u)|0x84;if(r.bus.write(r.bus.ctx,r.f01.control,&ctrl,1))return -40;
 unsigned nb=(r.irqcount+7)/8;uint8_t en[4]={};if(!nb||r.bus.read(r.bus.ctx,r.f01.control+1,en,nb))return -41;
 for(unsigned i=0;i<r.f12.irqs;++i){unsigned b=r.f12.irq+i;en[b/8]|=1u<<(b%8);}
 if(r.bus.write(r.bus.ctx,r.f01.control+1,en,nb))return -42;
 if(r.bus.read(r.bus.ctx,r.f01.data,&status,1)||(status&0xc0))return -43;
 return 0;
}
static inline unsigned cb_axis(unsigned v,unsigned max,bool flip){if(v>max)v=max;unsigned out=unsigned((uint64_t(v)*32767+max/2)/max);return flip?32767-out:out;}
static inline int cb_decode(const CbRmi &r,const uint8_t *p,unsigned bytes,unsigned transform,CbTouchFrame &f){
 f={};if(!r.maxx||!r.maxy||r.objects>10||bytes!=r.objects*8)return -44;
 for(unsigned i=0;i<r.objects;++i){const uint8_t *o=p+8*i;if(o[0]!=1)continue;unsigned x=cb_axis(cb_le16(o+1),r.maxx,transform&1),y=cb_axis(cb_le16(o+3),r.maxy,transform&2);if(transform&4){unsigned t=x;x=y;y=t;}unsigned j=f.count++;f.x[j]=x;f.y[j]=y;f.id[j]=i+1;}
 return 0;
}
#endif
