/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Software GMU peer only. Original source oracle is compiled outside Git. */
#include "AdrenoHfiStartup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t *shared;
static AdrenoHfi hfi;
static AdrenoHfiStartup state;
static AdrenoHfiStartupConfig config;
static AdrenoHfiStartupServices services;
static AdrenoHfiStartupResult result;
static struct {
    uint64_t time,jump;
    unsigned count,waits,health_calls,now_calls;
    unsigned fail_at,mode,clock_fail,clock_reverse,health_fail,wait_fail,clock_stall;
    unsigned ids[8],words[8];uint32_t messages[8][159];
} peer;
#ifdef SOURCE_ORACLE
extern int source_startup(const AdrenoHfiStartupConfig *,unsigned,unsigned *,unsigned *,unsigned *,uint32_t [8][159]);
#endif
static void response(uint32_t command)
{
    volatile uint32_t *q=shared+18;unsigned at=q[11];
    shared[2048+at]=(3u<<8)|126u;
    shared[2048+((at+1)&1023)]=command;
    shared[2048+((at+2)&1023)]=(peer.fail_at==peer.count && peer.mode==1)?37:0;
    shared[2048+((at+3)&1023)]=0xfafafafa;
    q[11]=(at+4)&1023;
}
static int notify(void *cookie)
{
    CHECK(cookie==&peer && peer.count<8);
    unsigned at=shared[16];uint32_t header=shared[1024+at];unsigned n=(header>>8)&255;
    unsigned index=peer.count++;peer.ids[index]=header&255;peer.words[index]=n-1;
    for(unsigned i=1;i<n;++i)peer.messages[index][i-1]=shared[1024+((at+i)&1023)];
    shared[16]=shared[17];
    if(peer.fail_at==peer.count && peer.mode==2)return 1;
    if(!(peer.fail_at==peer.count && peer.mode>=3))response(header);
    peer.time+=peer.jump;
    return 0;
}
static int now(void *cookie,uint64_t *value)
{
    CHECK(cookie==&peer);++peer.now_calls;
    if(peer.clock_fail && peer.now_calls==peer.clock_fail)return 1;
    if(peer.clock_reverse && peer.now_calls>=peer.clock_reverse)--peer.time;
    *value=peer.time;return 0;
}
static int wait_us(void *cookie,unsigned us)
{
    CHECK(cookie==&peer && us==10);++peer.waits;
    if(peer.wait_fail)return 1;
    if(!peer.clock_stall)peer.time+=us;
    return 0;
}
static int health(void *cookie)
{CHECK(cookie==&peer);return ++peer.health_calls==peer.health_fail;}
static void setup(unsigned acd,unsigned ifpc)
{
    memset(&peer,0,sizeof(peer));peer.time=1000;
    CHECK(AdrenoHfiInitializeStopped(&hfi,shared,16384,0x61004000,&peer,notify)==AdrenoHfiSuccess);
    state=(AdrenoHfiStartup){0};config=(AdrenoHfiStartupConfig){0};
    config.gpu_count=4;config.gmu_count=3;
    for(unsigned i=0;i<4;++i)config.gpu[i]=(AdrenoHfiPerformanceLevel){i*100000000ULL,i*3};
    for(unsigned i=0;i<3;++i)config.gmu[i]=(AdrenoHfiPerformanceLevel){i*200000000ULL,i*4};
    config.bandwidth_count=3;config.cnoc_count=2;config.ddr_count=3;
    config.cnoc_wait_mask=3;config.ddr_wait_mask=7;
    for(unsigned i=0;i<2;++i) {
        config.cnoc_addresses[i]=0x1000+4*i; // deliberate software fixture addresses, not board facts
        config.cnoc_data[0][i]=0x40000000;config.cnoc_data[1][i]=0x60000001;
    }
    for(unsigned i=0;i<3;++i) {
        config.ddr_addresses[i]=0x2000+4*i;
        for(unsigned j=0;j<3;++j)config.ddr_data[j][i]=0x40000000+i+j;
    }
    if(acd) {config.acd_version=1;config.acd_enable_mask=10;config.acd_stride=1;config.acd_count=2;config.acd_data[0]=41;config.acd_data[1]=99;}
    config.enable_ifpc=ifpc;
    services=(AdrenoHfiStartupServices){&peer,now,wait_us,health,5,10,100,500};
}
static AdrenoHfiStartupStatus run(void)
{return AdrenoHfiStartupRun(&state,&hfi,&config,&services,&result);}
static void refused(void)
{
    uint32_t before[4096];memcpy(before,shared,sizeof(before));
    CHECK(run()==AdrenoHfiStartupInvalid && !state.attempted && !peer.count);
    CHECK(!memcmp(before,shared,sizeof(before)) && result.resources_must_retain);
}
int main(void)
{
    CHECK(!posix_memalign((void **)&shared,4096,16384));
    unsigned cases=0;
    for(unsigned acd=0;acd<2;++acd)for(unsigned ifpc=0;ifpc<2;++ifpc) {
        setup(acd,ifpc);CHECK(run()==AdrenoHfiStartupSuccess);
        unsigned count=4+2*acd+ifpc;CHECK(peer.count==count && state.completed && !state.poisoned);
        CHECK(result.stage==AdrenoHfiStageComplete && result.published_commands==count && result.resources_must_retain);
#ifdef SOURCE_ORACLE
        unsigned expected_count=0,ids[8]={0},words[8]={0};uint32_t messages[8][159]={{0}};
        CHECK(!source_startup(&config,0,&expected_count,ids,words,messages));
        CHECK(expected_count==count);
        for(unsigned i=0;i<count;++i)CHECK(ids[i]==peer.ids[i] && words[i]==peer.words[i] && !memcmp(messages[i],peer.messages[i],words[i]*4));
#endif
        CHECK(run()==AdrenoHfiStartupBusy && peer.count==count);
        for(unsigned fail=1;fail<=count;++fail)for(unsigned mode=1;mode<=3;++mode) {
            setup(acd,ifpc);peer.fail_at=fail;peer.mode=mode;
            CHECK(run()==(mode==3?AdrenoHfiStartupTimeout:AdrenoHfiStartupTransportError));
            CHECK(peer.count==fail && state.poisoned && hfi.poisoned && !state.completed && result.resources_must_retain);
            CHECK(result.published_commands==fail && run()==AdrenoHfiStartupPoisoned && peer.count==fail);
#ifdef SOURCE_ORACLE
            expected_count=0;
            CHECK(source_startup(&config,fail,&expected_count,ids,words,messages)!=0 && expected_count==fail);
            for(unsigned i=0;i<fail;++i)CHECK(ids[i]==peer.ids[i] && words[i]==peer.words[i] && !memcmp(messages[i],peer.messages[i],words[i]*4));
#endif
            ++cases;
        }
    }
    setup(1,1);peer.fail_at=1;peer.mode=3;peer.clock_stall=1;
    CHECK(run()==AdrenoHfiStartupTimeout && result.poll_reads==5 && peer.waits==4);
    setup(1,1);peer.fail_at=1;peer.mode=3;services.command_timeout_us=20;
    CHECK(run()==AdrenoHfiStartupTimeout && result.elapsed_us==20 && peer.count==1);
    setup(1,1);peer.jump=100;
    CHECK(run()==AdrenoHfiStartupTimeout && peer.count==1 && !result.acknowledged_stages);
    setup(1,1);peer.jump=30;services.command_timeout_us=40;services.total_timeout_us=80;
    CHECK(run()==AdrenoHfiStartupTimeout && peer.count==3 && result.elapsed_us==90);
    setup(1,1);peer.clock_reverse=3;CHECK(run()==AdrenoHfiStartupClockError && state.poisoned);
    setup(1,1);peer.clock_fail=1;CHECK(run()==AdrenoHfiStartupClockError && !state.attempted && !peer.count);
    setup(1,1);peer.clock_fail=3;CHECK(run()==AdrenoHfiStartupClockError && state.poisoned);
    setup(1,1);peer.fail_at=1;peer.mode=3;peer.wait_fail=1;CHECK(run()==AdrenoHfiStartupWaitError && state.poisoned);
    setup(1,1);peer.health_fail=1;CHECK(run()==AdrenoHfiStartupPlatformFault && !state.attempted && !peer.count);
    for(unsigned i=2;i<=29;++i) {setup(1,1);peer.health_fail=i;AdrenoHfiStartupStatus s=run();CHECK(s==AdrenoHfiStartupPlatformFault || s==AdrenoHfiStartupSuccess);if(s)CHECK(state.poisoned && !state.completed);}
    setup(1,1);config.gpu_count=17;refused();
    setup(1,1);config.gmu_count=1;refused();
    setup(1,1);config.gpu[0].hz=1000;refused();
    setup(1,1);config.gpu[1].hz=1;refused();
    setup(1,1);config.gpu[1].hz=config.gpu[2].hz;refused();
    setup(1,1);config.gpu[3].hz=((uint64_t)UINT32_MAX+1)*1000;refused();
    setup(1,1);config.gpu[15].vote=1;refused();
    setup(1,1);config.ddr_count=9;refused();
    setup(1,1);config.bandwidth_count=17;refused();
    setup(1,1);config.cnoc_wait_mask=4;refused();
    setup(1,1);config.ddr_addresses[0]=0;refused();
    setup(1,1);config.ddr_addresses[1]=config.ddr_addresses[0];refused();
    setup(1,1);config.ddr_addresses[1]=config.cnoc_addresses[0];refused();
    setup(1,1);config.ddr_addresses[1]|=1;refused();
    setup(1,1);config.ddr_data[15][0]=1;refused();
    setup(1,1);config.cnoc_data[1][5]=1;refused();
    setup(1,1);config.acd_enable_mask|=1;refused();
    setup(1,1);config.acd_enable_mask|=16;refused();
    setup(1,1);config.acd_count=3;refused();
    setup(1,1);config.acd_stride=2;refused();
    setup(1,1);config.acd_data[31]=1;refused();
    setup(0,1);config.acd_version=1;refused();
    setup(1,1);config.enable_ifpc=2;refused();
    setup(1,1);services.polls_per_command=0;refused();
    setup(1,1);services.total_timeout_us=99;refused();
    setup(1,1);uint32_t p[159],copy[159],id=99;size_t n=9;memset(p,0xa5,sizeof(p));memcpy(copy,p,sizeof(p));
    CHECK(AdrenoHfiStartupEncode(&config,AdrenoHfiStageComplete,&id,p,159,&n)==AdrenoHfiStartupInvalid && !n && id==99 && !memcmp(p,copy,sizeof(p)));
    free(shared);printf("PASS: four startup profiles, %u per-stage failures, real transport software ACKs, deadlines/clock/fault quarantine and pre-publication refusal\n",cases);
    return 0;
}
