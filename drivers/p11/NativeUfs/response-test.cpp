#include "ResponsePolicy.h"
#include "ReadPolicy.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
static void put(unsigned char *p,unsigned n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
int main(){
 unsigned char cdb[16]={};
 const unsigned char expected[16]={0x28,0,1,2,3,4,0,0,32,0,0,0,0,0,0,0};
 assert(p11Read10(cdb,0x01020304,32));
 for(unsigned i=0;i<16;i++)assert(cdb[i]==expected[i]);
 assert(p11Read10(cdb,0xffffffffULL,1));
 assert(!p11Read10(cdb,0xffffffffULL,2));
 assert(!p11Read10(cdb,0x100000000ULL,1));
 assert(!p11Read10(cdb,0,0));
 assert(!p11Read10(cdb,0,65536));
 assert(p11Read10(cdb,0,65535)&&cdb[7]==255&&cdb[8]==255);
 unsigned char r[512]={},data[256]={};P11UfsResponse out;
 r[0]=0x21;r[3]=1;
 assert(p11UfsResponse(r,512,0,0xa0,256,out)&&out.transferred==256);
 // A device reporting two LUNs transfers 24 bytes, not the allocation length.
 r[1]=0x20;put(r+12,232);put(data,16);
 assert(p11UfsResponse(r,512,0,0xa0,256,out)&&out.transferred==24);
 assert(p11UfsLunList(data,out.transferred));
 assert(!p11UfsResponse(r,512,0,0x88,256,out));
 r[1]=0;assert(p11UfsResponse(r,512,0,0xa0,256,out)); // Linux supports missing U flag.
 put(data,24);assert(!p11UfsLunList(data,24));
 put(data,15);assert(!p11UfsLunList(data,24));
 assert(!p11UfsLunList(data,7));
 put(r+12,257);assert(!p11UfsResponse(r,512,0,0xa0,256,out));
 put(r+12,249);assert(!p11UfsResponse(r,512,0,0xa0,256,out));
 put(r+12,0);
 r[1]=0x40;assert(!p11UfsResponse(r,512,0,0xa0,256,out));r[1]=0;
 r[3]=2;assert(!p11UfsResponse(r,512,0,0xa0,256,out));r[3]=1;
 r[2]=1;assert(!p11UfsResponse(r,512,0,0xa0,256,out));r[2]=0;
 r[7]=2;r[11]=20;r[33]=18;r[34]=0x70;r[36]=6;
 assert(!p11UfsResponse(r,512,0,0xa0,256,out)); // Unit Attention is not hidden.
 r[7]=0;assert(!p11UfsResponse(r,512,0,0xa0,256,out)); // sense cannot become success.
 r[33]=0;r[11]=1;assert(!p11UfsResponse(r,512,0,0xa0,256,out));
 r[10]=2;r[11]=0;assert(!p11UfsResponse(r,512,0,0xa0,256,out));
 r[10]=r[11]=0;assert(!p11UfsResponse(r,32,0,0xa0,256,out));
 assert(p11UfsResponse(r,512,0,0x88,4096,out)&&out.transferred==4096);
 // Every outbound command remains refused, regardless of opcode.
 for(unsigned op=0;op<256;op++)for(unsigned bytes: {0U,256U,4096U})
  assert(!p8ReadCommand(op,1,bytes));
 puts("PASS: short REPORT LUNS, exact block reads, malformed/status rejection, outbound refusal");
}
