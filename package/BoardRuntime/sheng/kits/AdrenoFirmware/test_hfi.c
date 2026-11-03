/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* All peers below are software fixtures; no firmware/device is executed. */
#include "AdrenoHfi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static _Alignas(4096) uint32_t shared[4096],before[4096];
static AdrenoHfi h;static unsigned notified,notify_fail,auto_reply,waits,reply_at;
static uint32_t input[254],output[16];
#ifdef SOURCE_ORACLE
static _Alignas(4096) uint32_t reference[4096];
extern void source_initialize(uint32_t *,uint32_t);
extern int source_write(uint32_t *,uint32_t,uint32_t *,uint32_t);
#endif
static void response(unsigned count,unsigned id,uint32_t ret,uint32_t error)
{
    uint32_t *v=shared+18;unsigned at=v[11];
    CHECK(count && count<=255);
    for(unsigned i=0;i<count;++i)shared[2048+((at+i)&1023)]=i<3?0:0xfeed0000+i;
    shared[2048+at]=(1u<<16)|(count<<8)|id;
    if(count>1)shared[2048+((at+1)&1023)]=ret;
    if(count>2)shared[2048+((at+2)&1023)]=error;
    v[11]=(at+((count+3)&~3u))&1023;
}
static int notify(void *cookie)
{
    CHECK(cookie==&h);++notified;
    if(auto_reply)response(4,126,h.pending_header,0);
    return notify_fail;
}
static int wait_once(void *cookie)
{CHECK(cookie==&h);if(++waits==reply_at)response(4,126,h.pending_header,0);return 0;}
static int failed_wait(void *cookie)
{CHECK(cookie==&h);++waits;return 1;}
static void reset(void)
{
    notified=notify_fail=auto_reply=waits=reply_at=0;
    memset(&h,0,sizeof(h));memset(shared,0xa5,sizeof(shared));
    CHECK(AdrenoHfiInitializeStopped(&h,shared,sizeof(shared),0x60000000,&h,notify)==AdrenoHfiSuccess);
    CHECK(shared[1]==168 && shared[2]==6 && shared[3]==12 && shared[4]==3 && shared[5]==3);
    for(unsigned q=0;q<3;++q) {
        uint32_t *v=shared+6+12*q;
        CHECK(v[0]==1 && v[1]==0x60000000+(q+1)*4096 && v[2]==(0xa00|q) && v[3]==1024);
        CHECK(v[4]==0 && v[5]==0 && v[6]==1 && v[7]==1 && v[8]==1 && v[9]==0 && !v[10] && !v[11]);
    }
#ifdef SOURCE_ORACLE
    memset(reference,0,sizeof(reference));source_initialize(reference,h.iova);
    CHECK(!memcmp(shared,reference,sizeof(shared)));
#endif
}
static void untouched_output(void)
{for(unsigned i=0;i<16;++i)CHECK(output[i]==0x5a5a5a5a);}
int main(void)
{
    for(unsigned i=0;i<254;++i)input[i]=0xabcdef00+i;
    size_t count;uint32_t error,header;unsigned cases=0;
    for(unsigned wrap=0;wrap<2;++wrap)for(unsigned words=0;words<255;++words) {
        reset();unsigned at=wrap?1020:0;shared[16]=shared[17]=at;
        CHECK(AdrenoHfiSend(&h,14,input,words,&header)==AdrenoHfiSuccess && notified==1);
        CHECK(header==((1u<<20)|((words+1)<<8)|14) && h.pending_header==header);
        CHECK(shared[1024+at]==header);
        for(unsigned i=0;i<words;++i)CHECK(shared[1024+((at+1+i)&1023)]==input[i]);
        unsigned padded=(words+4)&~3u;
        for(unsigned i=words+1;i<padded;++i)CHECK(shared[1024+((at+i)&1023)]==0xfafafafa);
        CHECK(shared[17]==((at+padded)&1023));
#ifdef SOURCE_ORACLE
        reference[16]=reference[17]=at;
        uint32_t message[255];message[0]=header;for(unsigned i=0;i<words;++i)message[i+1]=input[i];
        CHECK(!source_write(reference,h.iova,message,words+1));
        CHECK(!memcmp(shared,reference,sizeof(shared)));
#endif
        CHECK(AdrenoHfiSend(&h,10,NULL,0,&header)==AdrenoHfiBusy && notified==1);
        shared[28]=shared[29]=at;unsigned returned=words%17;
        response(3+returned,126,h.pending_header,0);memset(output,0x5a,sizeof(output));
        CHECK(AdrenoHfiPoll(&h,output,16,&count,&error)==AdrenoHfiSuccess);
        CHECK(count==returned && !error && !h.pending_header && !h.poisoned);
        for(unsigned i=0;i<returned;++i)CHECK(output[i]==0xfeed0003+i);
        for(unsigned i=returned;i<16;++i)CHECK(output[i]==0x5a5a5a5a);
        CHECK(shared[28]==shared[29]);++cases;
    }
    reset();shared[16]=4;memcpy(before,shared,sizeof(shared));
    /* Reserve the complete four-word packet including padding and sentinel. */
    CHECK(AdrenoHfiSend(&h,10,NULL,0,&header)==AdrenoHfiNoSpace);
    CHECK(!memcmp(before,shared,sizeof(shared)) && !notified && !h.pending_header);
    reset();notify_fail=1;
    CHECK(AdrenoHfiSend(&h,14,NULL,0,&header)==AdrenoHfiNotifyError);
    CHECK(h.poisoned && h.pending_header && shared[17]==4);
    CHECK(AdrenoHfiSend(&h,14,NULL,0,&header)==AdrenoHfiPoisoned && notified==1);
    for(unsigned fault=0;fault<11;++fault) {
        reset();CHECK(AdrenoHfiSend(&h,14,NULL,0,&header)==AdrenoHfiSuccess);
        response(4,126,header,0);memset(output,0x5a,sizeof(output));
        AdrenoHfiStatus expected=AdrenoHfiCorrupt;
        switch(fault) {
            case 0:shared[2048]&=~0xff00u;break; /* zero length */
            case 1:shared[2048]=(20u<<8)|126;break;
            case 2:shared[29]=2;break; /* unaligned producer */
            case 3:shared[28]=1024;break;
            case 4:shared[21]=2048;break; /* untrusted ring size */
            case 5:shared[2048]=(1u<<8)|100;break; /* error message without a code */
            case 6:shared[2048]=(2u<<8)|126;expected=AdrenoHfiEmpty;break; /* too short to match: skipped */
            case 7:shared[2050]=7;expected=AdrenoHfiFirmwareError;break;
            case 8:shared[2048]=(20u<<8)|100;break;
            case 9:shared[2048]=(5u<<8)|126;break; /* incomplete packet */
            case 10:shared[1]=42;break;
        }
        uint32_t read=shared[28];
        CHECK(AdrenoHfiPoll(&h,output,16,&count,&error)==expected);
        if(fault==6) {CHECK(!count && !h.poisoned && h.pending_header && h.skipped==1);untouched_output();continue;}
        CHECK(!count && h.poisoned && h.pending_header && shared[28]==read);untouched_output();
    }
    /* a6xx_hfi_wait_for_ack: a firmware error message and a reply to another sequence number are consumed and
     * skipped, the first one recorded; the matching reply then completes. The GMU may change the echoed header's
     * other fields: only the sequence number is compared. */
    reset();CHECK(AdrenoHfiSend(&h,4,input,58,&header)==AdrenoHfiSuccess && header==0x103b04);
    response(2,100,77,0);
    response(4,126,(header&~0xfffff000u)|(9u<<20),0);
    memset(output,0x5a,sizeof(output));
    CHECK(AdrenoHfiPoll(&h,output,16,&count,&error)==AdrenoHfiEmpty && !h.poisoned && h.pending_header==header);
    CHECK(h.skipped==2 && h.skipped_header==((1u<<16)|(2u<<8)|100) && h.skipped_ret==77 && shared[28]==shared[29]);
    untouched_output();
    response(4,126,(header&0xfff00000u)|(1u<<16)|0x3b05,0);
    CHECK(AdrenoHfiPoll(&h,output,16,&count,&error)==AdrenoHfiSuccess && count==1 && output[0]==0xfeed0003);
    CHECK(!h.pending_header && !h.poisoned && h.skipped==2);
    /* a reply that matches by sequence number but reports an error fails as before */
    reset();CHECK(AdrenoHfiSend(&h,4,NULL,0,&header)==AdrenoHfiSuccess);
    response(4,126,header,5);
    CHECK(AdrenoHfiPoll(&h,output,16,&count,&error)==AdrenoHfiFirmwareError && error==5 && h.poisoned);
    reset();memset(output,0x5a,sizeof(output));
    CHECK(AdrenoHfiExchange(&h,14,NULL,0,3,wait_once,output,16,&count,&error)==AdrenoHfiTimeout);
    CHECK(waits==2 && h.poisoned && h.pending_header && !count);untouched_output();
    reset();
    CHECK(AdrenoHfiExchange(&h,14,NULL,0,3,failed_wait,output,16,&count,&error)==AdrenoHfiWaitError);
    CHECK(waits==1 && notified==1 && h.poisoned && h.pending_header && !count);untouched_output();
    reset();reply_at=2;
    CHECK(AdrenoHfiExchange(&h,14,NULL,0,3,wait_once,output,16,&count,&error)==AdrenoHfiSuccess);
    CHECK(waits==2 && count==1 && output[0]==0xfeed0003 && !h.pending_header);
    reset();auto_reply=1;
    for(unsigned i=0;i<4100;++i) {
        /* Software peer consumes the outgoing packet, then produces its ACK. */
        shared[16]=shared[17];
        CHECK(AdrenoHfiExchange(&h,10,NULL,0,1,NULL,output,16,&count,&error)==AdrenoHfiSuccess);
        CHECK(h.sequence==(i+1)%4095);
    }
    reset();memcpy(before,shared,sizeof(shared));
    CHECK(AdrenoHfiInitializeStopped(&h,shared,16383,0x60000000,&h,notify)==AdrenoHfiInvalid);
    CHECK(AdrenoHfiInitializeStopped(&h,shared,16384,0xffffd000,&h,notify)==AdrenoHfiInvalid);
    CHECK(AdrenoHfiInitializeStopped(&h,shared+1,16384,0x60000000,&h,notify)==AdrenoHfiInvalid);
    CHECK(!memcmp(before,shared,sizeof(shared)));
    CHECK(AdrenoHfiSend(&h,256,NULL,0,&header)==AdrenoHfiInvalid);
    CHECK(AdrenoHfiSend(&h,14,input,255,&header)==AdrenoHfiInvalid && !notified);
    CHECK(AdrenoHfiExchange(&h,14,NULL,0,0,NULL,output,16,&count,&error)==AdrenoHfiInvalid && !notified);
    printf("PASS: %u modern HFI wrapped command/response cases, 4100 sequence exchanges, padded-space guard, malformed/stale/error/timeout quarantine\n",cases);
    puts("Software peer only; no actual GMU ACK, firmware startup, MMIO or GPU execution");
}
