// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540AIRProbe.h"
#include "../../tools/gpu-native540/air_fixture.h"
#include <string.h>
namespace { namespace F=NativeMetal540AIRFixture; }
void NativeMetal540AIRProbe::stage(Stage s,uint32_t i) {
    record_.stage=s;record_.resource=i;record_.handle=bos_[i].handle;
    record_.iova=bos_[i].iova;record_.cpuBytes=bos_[i].bytes;
    if(transport_.record)transport_.record(transport_.context,record_);
}
int NativeMetal540AIRProbe::fail(int rc) {
    if(!record_.error)record_.error=rc;
    if(transport_.record)transport_.record(transport_.context,record_);
    return record_.error;
}
int NativeMetal540AIRProbe::request(uint32_t method,const void *in,size_t bytes,void *out,size_t expected) {
    size_t n=expected;int rc=transport_.call(transport_.context,method,in,bytes,out,&n);
    return rc?rc:n==expected?0:Invalid;
}
int NativeMetal540AIRProbe::run() {
    if(attempted_)return AlreadyAttempted;
    attempted_=true;
    if(!transport_.call||!transport_.mapView||!transport_.order||transport_.policy!=InhibitCache)return fail(Invalid);
    stage(Begin);NativeMetal540Client client(transport_.call,transport_.context);
    stage(Query);int rc=client.query();if(rc)return fail(rc);
    NativeAdrenoAbiDeviceInfo info{};
    stage(Device);rc=request(NativeAdrenoMethodGetDeviceInfo,nullptr,0,&info,sizeof(info));if(rc)return fail(rc);
    // P1/constant/transform end below start+0x40000. Six disjoint BOs:
    // IB8192, shader4096, source4096, output4096, params4096, scratch4096.
    constexpr uint64_t offset=0x40000,extent=7*4096;
    if(info.chip_id!=0x05040001u||info.reserved||info.flags!=NativeAdrenoDeviceClientIova||
       (info.va_start&4095)||info.va_start>=NativeMetal540::AddressLimit||info.va_size<offset+extent||
       info.va_size>NativeMetal540::AddressLimit-info.va_start)return fail(Invalid);
    uint64_t cursor=info.va_start+offset;
    for(unsigned i=0;i<6;++i) {
        const uint64_t bytes=i?4096:8192;
        stage(Allocate,i);rc=client.allocate(bytes,bos_[i].handle);if(rc)return fail(rc);
        for(unsigned j=0;j<i;++j)if(bos_[i].handle==bos_[j].handle)return fail(Invalid);
        stage(MapGPU,i);rc=client.map(bos_[i].handle,cursor,i==3||i==5?3:1,bos_[i].iova);if(rc)return fail(rc);
        cursor+=bytes;
        NativeAdrenoAbiBoCpuMap in{bos_[i].handle};NativeAdrenoAbiBoCpuMapReply out{};
        stage(MapCPU,i);rc=request(NativeAdrenoMethodBoCpuMap,&in,sizeof(in),&out,sizeof(out));if(rc)return fail(rc);
        bos_[i].token=out.token;
        if(!out.token||out.reserved||out.bytes!=bytes)return fail(Invalid);
        for(unsigned j=0;j<i;++j)if(out.token==bos_[j].token)return fail(Invalid);
        rc=transport_.mapView(transport_.context,out.token,bos_[i].address,bos_[i].bytes);if(rc)return fail(rc);
        const auto &b=bos_[i];
        if(!b.address||(b.address&3)||b.bytes!=bytes||b.bytes>UINT64_MAX-b.address)return fail(Invalid);
        for(unsigned j=0;j<i;++j)if(b.address<bos_[j].address+bos_[j].bytes&&bos_[j].address<b.address+b.bytes)return fail(Invalid);
        stage(MapCPU,i);
    }
    const auto &program=F::program;
    NativeMetal540::Resources resources{};
    resources.shader={bos_[1].iova,4096};resources.scratch={bos_[5].iova,4096};
    resources.buffers[0]={{bos_[2].iova,4096},0,F::sourceBytes,false};
    resources.buffers[1]={{bos_[3].iova,4096},0,F::outputBytes,true};
    resources.buffers[2]={{bos_[4].iova,4096},0,sizeof(F::params),false};
    uint32_t words[2048]={},written=0;stage(Encode);
    if(!NativeMetal540::encode(program,F::dispatch,resources,words,2048,written,true)||
       !written||uint64_t(written)*4>bos_[0].bytes)return fail(Invalid);
    record_.codeWords=program.info.codeWords;record_.ibWords=written;stage(Upload);
    // Copy object representation without aliasing a C++ struct as uint32_t.
    uint32_t params[sizeof(F::params)/sizeof(uint32_t)]={};
    static_assert(sizeof(params)==48,"actual reviewed AIR params");
    memcpy(params,&F::params,sizeof(params));
    for(unsigned i=0;i<6;++i) {
        auto *p=reinterpret_cast<volatile uint32_t *>(bos_[i].address);
        for(unsigned j=0;j<(i?1024u:2048u);++j)
            p[j]=i==2?(j<F::sourceWords?F::inputPattern(j):F::sentinel):
                i==3?F::sentinel:i==4?(j<12?params[j]:F::sentinel):0;
    }
    auto *shader=reinterpret_cast<volatile uint32_t *>(bos_[1].address);
    auto *ib=reinterpret_cast<volatile uint32_t *>(bos_[0].address);
    auto *input=reinterpret_cast<volatile const uint32_t *>(bos_[2].address);
    auto *output=reinterpret_cast<volatile const uint32_t *>(bos_[3].address);
    auto *param=reinterpret_cast<volatile const uint32_t *>(bos_[4].address);
    for(unsigned i=0;i<program.info.codeWords;++i)shader[i]=program.code[i];
    for(unsigned i=0;i<written;++i)ib[i]=words[i];
    transport_.order(transport_.context,true);
    for(unsigned i=0;i<program.info.codeWords;++i)if(shader[i]!=program.code[i])return fail(Mismatch);
    for(unsigned i=0;i<written;++i)if(ib[i]!=words[i])return fail(Mismatch);
    for(unsigned i=0;i<1024;++i)
        if(input[i]!=(i<F::sourceWords?F::inputPattern(i):F::sentinel)||output[i]!=F::sentinel||
           param[i]!=(i<12?params[i]:F::sentinel))return fail(Mismatch);
    // IB alone is submitted. Existing connection retains ALL mapped BOs even
    // on submit/wait failure; never free/unmap or use an uncertain completion.
    const NativeAdrenoAbiSubmitBuffer buffers[]={{bos_[0].handle,bos_[0].iova,uint64_t(written)*4}};
    stage(Submit);rc=client.submit(buffers,1,record_.fence);if(rc)return fail(rc);stage(Submit);
    stage(Fence);rc=client.wait(record_.fence,200000000ull);if(rc)return fail(rc);stage(Fence);
    transport_.order(transport_.context,false);stage(Readback);
    record_.observed=output[0];record_.scratch=*reinterpret_cast<volatile const uint32_t *>(bos_[5].address);
    const unsigned positions[]={0,127,255,256},inputPositions[]={262,389,517,780};
    for(unsigned i=0;i<4;++i){record_.samples[i]=output[positions[i]];record_.inputSamples[i]=input[inputPositions[i]];}
    for(unsigned i=0;i<F::sourceWords;++i)record_.inputVerified+=input[i]==F::inputPattern(i);
    for(unsigned i=F::sourceWords;i<1024;++i)record_.inputGuardVerified+=input[i]==F::sentinel;
    for(unsigned i=0;i<12;++i)record_.paramVerified+=param[i]==params[i];
    for(unsigned i=12;i<1024;++i)record_.paramGuardVerified+=param[i]==F::sentinel;
    for(unsigned i=0;i<F::outputWords;++i)record_.verified+=output[i]==F::expected(i);
    for(unsigned i=F::outputWords;i<1024;++i)record_.guardVerified+=output[i]==F::sentinel;
    stage(Readback);
    if(record_.inputVerified!=780||record_.inputGuardVerified!=244||record_.paramVerified!=12||
       record_.paramGuardVerified!=1012||record_.verified!=256||record_.guardVerified!=768)return fail(Mismatch);
    stage(Complete);return 0;
}
