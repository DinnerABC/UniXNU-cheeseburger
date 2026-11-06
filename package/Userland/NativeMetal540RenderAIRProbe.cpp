// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540RenderAIRProbe.h"
#include "../../tools/gpu-native540/render_air_fixture.h"
#include <string.h>
namespace {namespace F=Native540RenderAIRFixture;
const uint32_t vertices[]={0xbf400000,0xbf400000,0,0x3f800000,0x3f400000,0xbf400000,0,0x3f800000,0,0x3f400000,0,0x3f800000};
const uint32_t color[]={0x3f800000,0,0,0x3f800000};}
void NativeMetal540RenderAIRProbe::stage(Stage s,uint32_t i){record_.stage=s;record_.resource=i;record_.handle=bos_[i].handle;record_.iova=bos_[i].iova;}
int NativeMetal540RenderAIRProbe::fail(int rc){if(!record_.error)record_.error=rc;if(transport_.record)transport_.record(transport_.context,record_);return record_.error;}
int NativeMetal540RenderAIRProbe::request(uint32_t selector,const void *in,size_t n,void *out,size_t expected){size_t bytes=expected;
    const int rc=transport_.call(transport_.context,selector,in,n,out,&bytes);return rc?rc:bytes==expected?0:Invalid;}
int NativeMetal540RenderAIRProbe::run(bool prerequisites){
    if(attempted_)return AlreadyAttempted;attempted_=true;if(!prerequisites)return fail(Prerequisite);
    if(!transport_.call||!transport_.mapView||!transport_.order)return fail(Invalid);
    NativeMetal540Client client(transport_.call,transport_.context);stage(Query);int rc=client.query();if(rc)return fail(rc);
    NativeAdrenoAbiDeviceInfo info{};stage(Device);rc=request(NativeAdrenoMethodGetDeviceInfo,nullptr,0,&info,sizeof(info));if(rc)return fail(rc);
    constexpr uint64_t offset=0x200000,extent=11*4096;
    if(info.chip_id!=0x05040001||info.flags!=NativeAdrenoDeviceClientIova||info.reserved||!info.va_start||(info.va_start&4095)||
       info.va_start>=Native540Render::AddressLimit||info.va_size<offset+extent||info.va_size>Native540Render::AddressLimit-info.va_start)return fail(Invalid);
    uint64_t cursor=info.va_start+offset;const uint64_t sizes[]={8192,4096,4096,16384,4096,4096,4096};
    for(unsigned i=0;i<7;++i){
        auto &b=bos_[i];stage(Allocate,i);rc=client.allocate(sizes[i],b.handle);if(rc)return fail(rc);
        for(unsigned j=0;j<i;++j)if(b.handle==bos_[j].handle)return fail(Invalid);
        stage(MapGPU,i);rc=client.map(b.handle,cursor,i==3||i==4?3:1,b.iova);if(rc)return fail(rc);
        if(b.iova!=cursor)return fail(Invalid);cursor+=sizes[i];
        NativeAdrenoAbiBoCpuMap in{b.handle};NativeAdrenoAbiBoCpuMapReply out{};stage(MapCPU,i);
        rc=request(NativeAdrenoMethodBoCpuMap,&in,sizeof(in),&out,sizeof(out));if(rc)return fail(rc);
        if(!out.token||out.reserved||out.bytes!=sizes[i])return fail(Invalid);b.token=out.token;
        for(unsigned j=0;j<i;++j)if(b.token==bos_[j].token)return fail(Invalid);
        rc=transport_.mapView(transport_.context,b.token,b.address,b.bytes);if(rc)return fail(rc);
        if(!b.address||(b.address&3)||b.bytes!=sizes[i]||b.address>UINTPTR_MAX||b.bytes>UINTPTR_MAX-b.address)return fail(Invalid);
        for(unsigned j=0;j<i;++j)if(b.address<bos_[j].address+bos_[j].bytes&&bos_[j].address<b.address+b.bytes)return fail(Invalid);
    }
    Native540Render::Resources r{{bos_[0].iova,bos_[0].bytes},{bos_[1].iova,bos_[1].bytes},
        {bos_[2].iova,bos_[2].bytes},{bos_[3].iova,bos_[3].bytes},{bos_[4].iova,bos_[4].bytes}};
    r.vertexBindings[0]={{bos_[5].iova,bos_[5].bytes},0,48,true};
    r.fragmentBindings[0]={{bos_[6].iova,bos_[6].bytes},0,16,true};
    uint32_t words[2048]={},written=0;stage(Encode);
    if(!Native540Render::encodeAIR(F::vs,F::fs,r,words,2048,written)||!written)return fail(Invalid);
    record_.ibWords=written;stage(Upload);
    for(unsigned i=0;i<7;++i){auto *p=reinterpret_cast<volatile uint32_t *>(bos_[i].address);
        for(unsigned j=0;j<bos_[i].bytes/4;++j)p[j]=i==3||i==5||i==6?Native540Render::Sentinel:0;}
    const Native540Render::Program *programs[]={&F::vs.program,&F::fs.program};
    for(unsigned k=0;k<2;++k){auto *p=reinterpret_cast<volatile uint32_t *>(bos_[k+1].address);
        for(unsigned i=0;i<programs[k]->info.codeWords;++i)p[i]=programs[k]->code[i];}
    auto *ib=reinterpret_cast<volatile uint32_t *>(bos_[0].address);
    auto *vtx=reinterpret_cast<volatile uint32_t *>(bos_[5].address);auto *rgba=reinterpret_cast<volatile uint32_t *>(bos_[6].address);
    for(unsigned i=0;i<written;++i)ib[i]=words[i];for(unsigned i=0;i<12;++i)vtx[i]=vertices[i];for(unsigned i=0;i<4;++i)rgba[i]=color[i];
    transport_.order(transport_.context,true);
    for(unsigned i=0;i<written;++i)if(ib[i]!=words[i])return fail(Mismatch);
    for(unsigned k=0;k<2;++k){auto *p=reinterpret_cast<volatile uint32_t *>(bos_[k+1].address);
        for(unsigned i=0;i<programs[k]->info.codeWords;++i)if(p[i]!=programs[k]->code[i])return fail(Mismatch);}
    const NativeAdrenoAbiSubmitBuffer submitted[]={{bos_[0].handle,bos_[0].iova,uint64_t(written)*4}};
    stage(Submit);rc=client.submit(submitted,1,record_.fence);if(rc)return fail(rc);
    stage(Fence);rc=client.wait(record_.fence,200000000ull);if(rc)return fail(rc);
    transport_.order(transport_.context,false);stage(Readback);
    auto *scratch=reinterpret_cast<volatile const uint32_t *>(bos_[4].address);
    record_.clearTimestamp=scratch[0];record_.colorTimestamp=scratch[2];record_.ucheTimestamp=scratch[4];
    const bool verified=Native540Render::verify(reinterpret_cast<volatile const uint32_t *>(bos_[3].address),bos_[3].bytes,record_.pixels);
    for(unsigned i=0;i<12;++i)record_.inputVerified+=vtx[i]==vertices[i];
    for(unsigned i=12;i<1024;++i)record_.inputGuardVerified+=vtx[i]==Native540Render::Sentinel;
    for(unsigned i=0;i<4;++i)record_.colorVerified+=rgba[i]==color[i];
    for(unsigned i=4;i<1024;++i)record_.colorGuardVerified+=rgba[i]==Native540Render::Sentinel;
    if(!verified||record_.clearTimestamp!=1||record_.colorTimestamp!=2||record_.ucheTimestamp!=3||record_.inputVerified!=12||
       record_.inputGuardVerified!=1012||record_.colorVerified!=4||record_.colorGuardVerified!=1020)return fail(Mismatch);
    stage(Complete);if(transport_.record)transport_.record(transport_.context,record_);return 0;
}
