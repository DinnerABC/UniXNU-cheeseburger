#include "SynapticsRMI4.h"
#include <assert.h>
#include <stdio.h>
struct Mock {unsigned reads=0,writes=0;bool fail=false;};
static int rd(void *ctx,uint16_t a,uint8_t *p,unsigned n){Mock &m=*(Mock*)ctx;++m.reads;if(m.fail)return -1;memset(p,0,n);
 if(a==0xe9&&n==6){uint8_t v[]={0x10,0x30,0x40,0x50,1,1};memcpy(p,v,6);}
 if(a==0xe3&&n==6){uint8_t v[]={0x60,0x70,0x80,0xa0,1,12};memcpy(p,v,6);}
 if(a==0x10&&n==21)p[0]=1;
 if(a==0x60&&n==1)p[0]=1;
 // One query register, Ctrl8, Data1 with two 8-byte subpackets.
 if((a==0x61||a==0x64||a==0x67)&&n==1)p[0]=(a==0x64?3:2);
 if(a==0x62&&n==2){p[0]=2;p[1]=1;}
 if(a==0x63&&n==2){p[0]=1;p[1]=1;}
 if(a==0x65&&n==3){p[0]=2;p[1]=0;p[2]=1;}
 if(a==0x66&&n==2){p[0]=4;p[1]=1;}
 if(a==0x68&&n==2){p[0]=2;p[1]=2;}
 if(a==0x69&&n==2){p[0]=16;p[1]=3;}
 if(a==0x80&&n==4){p[0]=0x37;p[1]=4;p[2]=0x7f;p[3]=7;}
 return 0;
}
static int wr(void *ctx,uint16_t a,const uint8_t*,unsigned n){Mock&m=*(Mock*)ctx;++m.writes;assert((a==0x40&&n==1)||(a==0x41&&n==1));return m.fail?-1:0;}
struct WireMock {unsigned pages=0,reads=0;bool fail=false;};
static int xfer(void *c,const uint8_t *t,unsigned nt,uint8_t *p,unsigned n){WireMock&m=*(WireMock*)c;if(m.fail)return -1;if(nt==6){assert(t[0]==0x81&&t[1]==0x40&&t[2]==0x83&&t[3]==2&&t[4]==255);++m.pages;}else{assert(nt==9&&t[0]==0x81&&t[1]==0x40&&t[2]==0x82&&t[3]==1&&t[5]==0x81&&t[6]==0x41&&t[7]==0x87&&n==unsigned(t[8])+2);++m.reads;memset(p,0,n);p[2]=42;}return 0;}
int main(){WireMock wm;CbRmiWire w={&wm,xfer,256};uint8_t b[255];assert(!cb_wire_read(w,0x10,b,21)&&b[0]==42&&wm.pages==1);assert(!cb_wire_read(w,0x20,b,1)&&wm.pages==1);assert(!cb_wire_read(w,0x110,b,255)&&wm.pages==2);wm.fail=true;assert(cb_wire_read(w,0x10,b,1)<0&&w.page==256);assert(cb_wire_read(w,0xff,b,1)<0);Mock m;CbRmi r={};r.bus={&m,rd,wr};assert(cb_discover(r)==0);assert(r.maxx==1079&&r.maxy==1919&&r.objects==2&&r.data1==0xa0&&r.packet==16&&m.writes==2);
 uint8_t data[16]={1,0,0,0,0,1,1,1,1,0x37,4,0x7f,7,1,1,1};CbTouchFrame f;
 assert(!cb_decode(r,data,16,0,f)&&f.count==2&&f.x[0]==0&&f.y[0]==0&&f.x[1]==32767&&f.y[1]==32767&&f.id[1]==2);
 assert(!cb_decode(r,data,16,3,f)&&f.x[0]==32767&&f.y[0]==32767&&f.x[1]==0);
 data[1]=0x37;data[2]=4;assert(!cb_decode(r,data,16,4,f)&&f.x[0]==0&&f.y[0]==32767);
 data[1]=255;data[2]=255;assert(!cb_decode(r,data,16,0,f)&&f.x[0]==32767);
 data[0]=3;assert(!cb_decode(r,data,16,0,f)&&f.count==1&&f.id[0]==2);data[8]=0;assert(!cb_decode(r,data,16,0,f)&&!f.count);
 assert(cb_decode(r,data,15,0,f)<0);m.fail=true;assert(cb_discover(r)<0);
 for(unsigned t=0;t<8;++t)for(unsigned v=0;v<2;++v){assert(cb_axis(v?1079:0,1079,t&1)==((v!=bool(t&1))?32767:0));}
 assert(cb_axis(540,1080,false)==16384);puts("RMI4 descriptor/PDT/F01/F12/multitouch/release/clamp/orientation/error PASS");}
