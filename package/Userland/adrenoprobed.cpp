// SPDX-License-Identifier: GPL-2.0-only
// Userland proof of the A540 submission path, written against sheng's
// NativeAdrenoABI.h unchanged. It allocates two buffer objects, maps them into
// the GPU address window, writes one indirect buffer from userspace, submits it
// and checks that the CP wrote the expected dwords through the SMMU.
//
// It runs as an owner launchd job (no sandbox) from the GPU addon, so it opens
// the private NativeAdrenoGpu service directly. The line it reports reaches
// the persistent log through SetMetadata and the kernel's BootLog.
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <atomic>
#include "NativeAdrenoABI.h"

namespace {
constexpr uint32_t kOpenType=0x4147;

// Independent progress/error channel: a probe that has not opened the GPU
// service yet cannot use SetMetadata, so each stage goes to /dev/console (the
// kernel message buffer) as well as stderr. Lines are bounded.
void line(const char *fmt,...){
    char text[160];
    va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof text,fmt,ap);va_end(ap);
    int fd=open("/dev/console",O_WRONLY|O_NOCTTY|O_CLOEXEC);
    if(fd>=0){write(fd,text,strlen(text));write(fd,"\n",1);close(fd);}
    fprintf(stderr,"%s\n",text);
}

uint32_t parity(uint32_t v){v^=v>>16;v^=v>>8;v^=v>>4;v^=v>>2;v^=v>>1;return (~v)&1;}
uint32_t packet7(uint32_t op,uint32_t n){
    return 0x70000000u|n|(parity(n)<<15)|(op<<16)|(parity(op)<<23);
}

struct Ctx {
    io_connect_t conn=0;
    int step=0,error=0;
    uint64_t ticks=0;
    char detail[40]={};
};

void report(Ctx &c,const char *why,int rc){
    if(!c.conn)return;
    NativeAdrenoAbiMetadata meta={};
    meta.handle=1;
    char text[64]={};
    snprintf(text,sizeof text,"P21 %s s%d e%x v%d t%llx",why,c.step,c.error,0,
        (unsigned long long)c.ticks);
    if(rc){char more[20];snprintf(more,sizeof more," r%x",(unsigned)rc);strncat(text,more,sizeof text-strlen(text)-1);}
    meta.bytes=(uint32_t)strlen(text);
    memcpy(meta.text,text,meta.bytes);
    IOConnectCallMethod(c.conn,NativeAdrenoMethodSetMetadata,nullptr,0,&meta,sizeof meta,nullptr,nullptr,nullptr,nullptr);
}

template <class In,class Out>
kern_return_t call(Ctx &c,uint32_t method,const In *in,Out *out){
    size_t outSize=out?sizeof(Out):0;
    return IOConnectCallMethod(c.conn,method,nullptr,0,in,sizeof(In),
        nullptr,nullptr,out,out?&outSize:nullptr);
}
template <class Out>
kern_return_t callOut(Ctx &c,uint32_t method,Out *out){
    size_t outSize=out?sizeof(Out):0;
    return IOConnectCallMethod(c.conn,method,nullptr,0,nullptr,0,
        nullptr,nullptr,out,out?&outSize:nullptr);
}

bool checked(Ctx &c,kern_return_t kr,const char *why){
    if(kr==KERN_SUCCESS){++c.step;return true;}
    if(!c.error)c.error=(int)kr;
    report(c,why,(int)kr);
    return false;
}
}

int main(){
    Ctx c;
    line("P21PROBE stage=start pid=%d",(int)getpid());
    // The GPU probe publishes the service after CP_ME_INIT, GPMU and ZAP
    // (about 100 s into boot), while launchd starts this job much earlier.
    io_service_t service=0;
    for(unsigned i=0;i<600 && !service;++i){
        service=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("NativeAdrenoGpuService"));
        if(!service)service=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceNameMatching("NativeAdrenoGpu"));
        if(!service)usleep(500000);
    }
    if(!service){line("P21PROBE stage=lookup found=0");report(c,"nosvc",0);return 3;}
    line("P21PROBE stage=lookup found=1");
    // Marker open: proves the service was found and the open path reached the
    // driver even if the real connection is refused. The driver counts the
    // type and returns unsupported.
    {io_connect_t marker=0;if(IOServiceOpen(service,mach_task_self(),0x41470001,&marker)==KERN_SUCCESS&&marker){IOServiceClose(marker);line("P21PROBE stage=marker ok=1");}else line("P21PROBE stage=marker ok=0");}
    kern_return_t opened=IOServiceOpen(service,mach_task_self(),kOpenType,&c.conn);
    line("P21PROBE stage=open rc=%d conn=%u",(int)opened,c.conn);
    if(opened!=KERN_SUCCESS||!c.conn){
        IOObjectRelease(service);report(c,"noopen",(int)opened);return 4;
    }
    IOObjectRelease(service);

    NativeAdrenoAbiParams params={};
    if(!checked(c,callOut(c,NativeAdrenoMethodQuery,&params),"query"))return 5;
    line("P21PROBE stage=query abi=%u in=%u out=%u max=%llu",params.abi_version,params.input_bits,params.output_bits,(unsigned long long)params.max_bo_bytes);
    if(params.abi_version!=NativeAdrenoAbiVersion){c.error=params.abi_version;report(c,"abiver",0);return 6;}

    NativeAdrenoAbiDeviceInfo info={};
    if(!checked(c,callOut(c,NativeAdrenoMethodGetDeviceInfo,&info),"devinfo"))return 7;
    line("P21PROBE stage=devinfo chip=%x va=%llx size=%llu",info.chip_id,(unsigned long long)info.va_start,(unsigned long long)info.va_size);
    if(!info.va_start||info.va_size<0x2000){c.error=0xbad0;report(c,"vawin",0);return 8;}

    NativeAdrenoAbiBoAlloc alloc={4096,12,1},allocReplyStart={4096,12,1};
    NativeAdrenoAbiBoAllocReply ib={},tgt={};
    if(!checked(c,call(c,NativeAdrenoMethodBoAlloc,&alloc,&ib),"alloc-ib"))return 9;
    if(!checked(c,call(c,NativeAdrenoMethodBoAlloc,&allocReplyStart,&tgt),"alloc-tgt"))return 10;
    line("P21PROBE stage=bo ib=%llx tgt=%llx",(unsigned long long)ib.handle,(unsigned long long)tgt.handle);

    const uint64_t ibIova=info.va_start,tgtIova=info.va_start+0x1000;
    NativeAdrenoAbiBoMap mapIb={ib.handle,ibIova,1,0,2,0},mapTgt={tgt.handle,tgtIova,3,0,2,0};
    NativeAdrenoAbiBoMapReply mr={};
    if(!checked(c,call(c,NativeAdrenoMethodBoMap,&mapIb,&mr),"map-ib"))return 11;
    if(!checked(c,call(c,NativeAdrenoMethodBoMap,&mapTgt,&mr),"map-tgt"))return 12;
    line("P21PROBE stage=map ib=%llx tgt=%llx",(unsigned long long)mr.iova,(unsigned long long)mr.iova);

    NativeAdrenoAbiBoCpuMap cmIb={ib.handle},cmTgt={tgt.handle};
    NativeAdrenoAbiBoCpuMapReply crIb={},crTgt={};
    if(!checked(c,call(c,NativeAdrenoMethodBoCpuMap,&cmIb,&crIb),"cpumap-ib"))return 13;
    if(!checked(c,call(c,NativeAdrenoMethodBoCpuMap,&cmTgt,&crTgt),"cpumap-tgt"))return 14;
    line("P21PROBE stage=cpumap ib=%u tgt=%u bytes=%llu/%llu",crIb.token,crTgt.token,(unsigned long long)crIb.bytes,(unsigned long long)crTgt.bytes);

    mach_vm_address_t ibAddr=0,tgtAddr=0;
    mach_vm_size_t ibLen=0,tgtLen=0;
    if(!checked(c,IOConnectMapMemory64(c.conn,crIb.token,mach_task_self(),&ibAddr,&ibLen,1),"mapmem-ib"))return 15;
    if(!checked(c,IOConnectMapMemory64(c.conn,crTgt.token,mach_task_self(),&tgtAddr,&tgtLen,1),"mapmem-tgt"))return 16;
    line("P21PROBE stage=memmap ib=%llx/%llu tgt=%llx/%llu",(unsigned long long)ibAddr,(unsigned long long)ibLen,(unsigned long long)tgtAddr,(unsigned long long)tgtLen);
    if(ibLen<4096||tgtLen<4096){c.error=0xbad1;report(c,"maplen",0);return 17;}

    volatile uint32_t *iw=reinterpret_cast<volatile uint32_t *>(ibAddr);
    volatile uint32_t *tw=reinterpret_cast<volatile uint32_t *>(tgtAddr);
    for(unsigned i=0;i<1024;++i)tw[i]=0xdeadbeef;
    const uint32_t values[3]={0x0adc0de1u,0xfeed0002u,0x600d0003u};
    uint32_t words[12];
    for(unsigned i=0;i<3;++i){
        words[i*4]=packet7(0x3d,3); // CP_MEM_WRITE
        words[i*4+1]=uint32_t(tgtIova+i*4);
        words[i*4+2]=uint32_t(tgtIova>>32);
        words[i*4+3]=values[i];
    }
    for(unsigned i=0;i<12;++i)iw[i]=words[i];
    __asm__ volatile("dsb sy":::"memory");

    NativeAdrenoAbiSubmit submit={};
    submit.buffer_count=1;
    submit.buffers[0]={ib.handle,ibIova,sizeof words};
    NativeAdrenoAbiSubmitReply sr={};
    if(!checked(c,call(c,NativeAdrenoMethodSubmit,&submit,&sr),"submit"))return 18;
    line("P21PROBE stage=submit fence=%llu",(unsigned long long)sr.fence);
    NativeAdrenoAbiWaitFence wait={sr.fence,200000000ull};
    if(!checked(c,call(c,NativeAdrenoMethodWaitFence,&wait,(NativeAdrenoAbiTimestamp*)nullptr),"wait"))return 19;
    line("P21PROBE stage=wait ok=1");
    __asm__ volatile("dsb sy":::"memory");

    const int verified=(tw[0]==values[0])+(tw[1]==values[1])+(tw[2]==values[2])+(tw[3]==0xdeadbeef);
    line("P21PROBE stage=values %08x/%08x/%08x/%08x verified=%d",
        (unsigned)tw[0],(unsigned)tw[1],(unsigned)tw[2],(unsigned)tw[3],verified);
    NativeAdrenoAbiTimestamp ts={};
    callOut(c,NativeAdrenoMethodGetTimestamp,&ts);
    c.ticks=ts.ticks;
    if(verified!=4){c.error=0xbadd;snprintf(c.detail,sizeof c.detail,"v%d",verified);}

    // Report the verified dword count in the line itself.
    {
        NativeAdrenoAbiMetadata meta={};
        char text[64]={};
        snprintf(text,sizeof text,"P21 done s%d e%x v%d t%llx",c.step,c.error,verified,
            (unsigned long long)c.ticks);
        meta.bytes=(uint32_t)strlen(text);
        memcpy(meta.text,text,meta.bytes);
        IOConnectCallMethod(c.conn,NativeAdrenoMethodSetMetadata,nullptr,0,&meta,sizeof meta,nullptr,nullptr,nullptr,nullptr);
    }

    IOConnectUnmapMemory64(c.conn,crIb.token,mach_task_self(),ibAddr);
    IOConnectUnmapMemory64(c.conn,crTgt.token,mach_task_self(),tgtAddr);
    NativeAdrenoAbiBoCpuUnmap ui={ib.handle,crIb.token,0},ut={tgt.handle,crTgt.token,0};
    call(c,NativeAdrenoMethodBoCpuUnmap,&ui,(NativeAdrenoAbiTimestamp*)nullptr);
    call(c,NativeAdrenoMethodBoCpuUnmap,&ut,(NativeAdrenoAbiTimestamp*)nullptr);
    NativeAdrenoAbiBoFree fi={ib.handle},ft={tgt.handle};
    call(c,NativeAdrenoMethodBoFree,&fi,(NativeAdrenoAbiTimestamp*)nullptr);
    call(c,NativeAdrenoMethodBoFree,&ft,(NativeAdrenoAbiTimestamp*)nullptr);
    line("P21PROBE stage=cleanup done verified=%d",verified);
    IOServiceClose(c.conn);
    line("P21PROBE stage=exit code=%d",verified==4?0:20);
    return verified==4?0:20;
}
