/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Arithmetic tests against pinned original-source oracle, no platform access. */
#include "AdrenoHfiVotes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
extern int source_arc(const uint32_t *,unsigned,const uint8_t *,size_t,const uint8_t *,size_t,uint32_t *);
extern int source_bandwidth(const uint32_t *,unsigned,const AdrenoHfiBcm *,unsigned,uint32_t,uint32_t [159]);
static uint32_t seed=0x740;
static uint32_t random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void put16(uint8_t *p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void arc_tests(void)
{
    uint8_t primary[513]={0},secondary[513]={0};uint32_t levels[16]={0},votes[16],expected[16];
    /* Unaligned original aux data, duplicated levels, trailing zero padding,
     * secondary fallback and maximum representable 8-bit rail index. */
    for(unsigned i=1;i<256;++i)put16(primary+1+2*i,i*257);
    put16(secondary+3,128);put16(secondary+5,500);
    for(unsigned run=0;run<1000;++run) {
        unsigned count=1+random32()%16;levels[0]=0;
        for(unsigned i=1;i<count;++i)levels[i]=levels[i-1]+random32()%4000;
        memset(votes,0xa5,sizeof(votes));memset(expected,0xa5,sizeof(expected));
        CHECK(AdrenoHfiArcVotes(levels,count,primary+1,512,secondary+1,512,votes,16)==AdrenoHfiStartupSuccess);
        CHECK(!source_arc(levels,count,primary+1,512,secondary+1,512,expected));
        CHECK(!memcmp(votes,expected,sizeof(votes)));
    }
    levels[1]=65535;
    CHECK(AdrenoHfiArcVotes(levels,2,primary+1,512,secondary+1,512,votes,16)==AdrenoHfiStartupSuccess && votes[1]==0xffff02ff);
    for(unsigned mode=0;mode<8;++mode) {
        uint8_t p[8]={0,0,100,0,200,0,0,0},s[8]={0,0,100,0,0,0,0,0};
        uint32_t l[3]={0,50,200},before[16];size_t bytes=8,capacity=16,count=3;
        switch(mode) {
        case 0:bytes=7;break;case 1:bytes=514;break;case 2:p[0]=1;break;
        case 3:p[2]=201;break;case 4:p[2]=0;break;case 5:l[2]=201;break;
        case 6:l[2]=1;break;case 7:capacity=2;break;
        }
        memset(votes,0x5a,sizeof(votes));memcpy(before,votes,sizeof(votes));
        CHECK(AdrenoHfiArcVotes(l,count,p,bytes,s,8,votes,capacity)==AdrenoHfiStartupInvalid && !memcmp(before,votes,sizeof(votes)));
    }
}
static void bandwidth_tests(void)
{
    uint32_t bw[16]={0},expected[159],actual[159],id;size_t words;
    AdrenoHfiBcm b[3];AdrenoHfiStartupConfig c;
    for(unsigned run=0;run<1000;++run) {
        unsigned count=2+random32()%15,n=1+random32()%3;
        for(unsigned i=1;i<count;++i)bw[i]=bw[i-1]+1+random32()%20000000;
        if(run%5==0){count=2;bw[1]=UINT32_MAX;}
        for(unsigned j=0;j<n;++j) {
            b[j]=(AdrenoHfiBcm){.address=0x1000+4*j,.unit=1+random32(),.width=1+random32()%65535,
                .buswidth=1+random32()%65535,.vcd=random32()%3,.fixed=j==2,.perfmode=j==2?8:0,.perfmode_kbps=j==2?16500000:0};
            if(run%7==0){b[j].unit=1;b[j].width=65535;b[j].buswidth=1;}
            if(run%11==0){b[j].unit=UINT32_MAX;b[j].width=1;b[j].buswidth=65535;}
        }
        c=(AdrenoHfiStartupConfig){.gpu_count=2,.gmu_count=2,.gpu={{0,0},{1000,1}},.gmu={{0,0},{2000,1}},.enable_ifpc=1};
        CHECK(AdrenoHfiBuildBandwidth(bw,count,b,n,0x2000,&c)==AdrenoHfiStartupSuccess);
        CHECK(c.enable_ifpc==1 && c.gpu[1].hz==1000 && c.gmu[1].vote==1);
        CHECK(AdrenoHfiStartupEncode(&c,AdrenoHfiStageBandwidth,&id,actual,159,&words)==AdrenoHfiStartupSuccess && id==3 && words==159);
        CHECK(!source_bandwidth(bw,count,b,n,0x2000,expected) && !memcmp(actual,expected,sizeof(actual)));
    }
    for(unsigned mode=0;mode<12;++mode) {
        AdrenoHfiStartupConfig before;c=(AdrenoHfiStartupConfig){.gpu_count=7};before=c;
        b[0]=(AdrenoHfiBcm){.address=0x1000,.unit=1000,.width=4,.buswidth=4};b[1]=b[0];b[1].address+=4;
        bw[0]=0;bw[1]=16500000;unsigned n=2,count=2,cn0=0x2000;
        switch(mode) {
        case 0:b[0].unit=0;break;case 1:b[0].width=0;break;case 2:b[0].buswidth=0;break;
        case 3:b[0].fixed=2;break;case 4:b[0].address=0;break;case 5:b[0].address=0x1002;break;
        case 6:b[1].address=b[0].address;break;case 7:cn0=b[1].address;break;
        case 8:bw[1]=0;break;case 9:count=17;break;case 10:n=4;break;case 11:b[0].perfmode=0xffff;break;
        }
        CHECK(AdrenoHfiBuildBandwidth(bw,count,b,n,cn0,&c)==AdrenoHfiStartupInvalid && !memcmp(&before,&c,sizeof(c)));
    }
    uint8_t aux[9]={0,0xe8,3,0,0,16,0,2,0};b[0]=(AdrenoHfiBcm){.address=0x1000,.buswidth=4};
    CHECK(AdrenoHfiBcmAux(b,aux+1,8)==AdrenoHfiStartupSuccess && b[0].unit==1000 && b[0].width==16 && b[0].vcd==2 && b[0].buswidth==4 && b[0].address==0x1000);
    AdrenoHfiBcm before=b[0];aux[8]=1;
    CHECK(AdrenoHfiBcmAux(b,aux+1,8)==AdrenoHfiStartupInvalid && !memcmp(b,&before,sizeof(before)));
    CHECK(AdrenoHfiBcmAux(b,aux+1,7)==AdrenoHfiStartupInvalid && !memcmp(b,&before,sizeof(before)));
    /* Explicit threshold and saturation/minimum cases with known wire values. */
    b[0]=(AdrenoHfiBcm){.address=0x1000,.fixed=1,.perfmode=8,.perfmode_kbps=16500000};
    uint32_t edge[]={0,16499999,16500000,16500001};
    CHECK(AdrenoHfiBuildBandwidth(edge,4,b,1,0x2000,&c)==AdrenoHfiStartupSuccess);
    CHECK(c.ddr_data[0][0]==0x40000000 && c.ddr_data[1][0]==0x60000000 && c.ddr_data[2][0]==0x60000008 && c.ddr_data[3][0]==0x60000008);
}
int main(void)
{
    arc_tests();bandwidth_tests();
    puts("PASS: 1000 ARC + 1000 BCM original-source differential cases, full bandwidth wire layout, 22 refusal controls, unaligned aux, index/threshold/clamp boundaries");
    puts("Arithmetic and software inputs only; no actual command-db, rail ownership, power or GMU execution claim");
}
