// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540Probe.h"
#ifdef NATIVE540_PROBE_GLOBAL
#include "../../tools/gpu-native540/probe_global_fixture.h"
#else
#include "../../tools/gpu-native540/probe_fixture.h"
#endif

void NativeMetal540Probe::stage(Stage s,uint32_t i) {
    record_.stage=s;record_.resource=i;record_.handle=bos_[i].handle;
    record_.iova=bos_[i].iova;record_.cpuBytes=bos_[i].bytes;
    if(transport_.record)transport_.record(transport_.context,record_);
}
int NativeMetal540Probe::fail(int rc) {
    if(!record_.error)record_.error=rc;
    if(transport_.record)transport_.record(transport_.context,record_);
    return record_.error;
}
int NativeMetal540Probe::request(uint32_t method,const void *in,size_t bytes,void *out,size_t expected) {
    size_t size=expected;
    int rc=transport_.call(transport_.context,method,in,bytes,out,&size);
    return rc?rc:size==expected?0:Invalid;
}
int NativeMetal540Probe::run() {
    if(attempted_)return AlreadyAttempted;
    attempted_=true;
    if(!transport_.call||!transport_.mapView||!transport_.order||transport_.policy!=InhibitCache)return fail(Invalid);
    stage(Begin);
    NativeMetal540Client client(transport_.call,transport_.context);
    stage(Query);int rc=client.query();if(rc)return fail(rc);
    NativeAdrenoAbiDeviceInfo info{};
    stage(Device);rc=request(NativeAdrenoMethodGetDeviceInfo,nullptr,0,&info,sizeof(info));if(rc)return fail(rc);
    // Same-board identity, bounded scratch aperture separated from original P1's
    // first two pages. All four BOs are new allocations, NEVER imported aliases.
    const uint64_t offset=0x10000,extent=5*4096;
    if(info.chip_id!=0x05040001u||info.reserved||info.flags!=NativeAdrenoDeviceClientIova||
       (info.va_start&4095)||info.va_start>=NativeMetal540::AddressLimit||info.va_size<offset+extent||
       info.va_size>NativeMetal540::AddressLimit-info.va_start)return fail(Invalid);
    uint64_t cursor=info.va_start+offset;
    for(unsigned i=0;i<4;++i) {
        const uint64_t bytes=i?4096:8192;
        stage(Allocate,i);rc=client.allocate(bytes,bos_[i].handle);if(rc)return fail(rc);
        for(unsigned j=0;j<i;++j)if(bos_[i].handle==bos_[j].handle)return fail(Invalid);
        stage(MapGPU,i);rc=client.map(bos_[i].handle,cursor,i<2?1:3,bos_[i].iova);if(rc)return fail(rc);
        cursor+=bytes;
        NativeAdrenoAbiBoCpuMap in{bos_[i].handle};NativeAdrenoAbiBoCpuMapReply out{};
        stage(MapCPU,i);rc=request(NativeAdrenoMethodBoCpuMap,&in,sizeof(in),&out,sizeof(out));if(rc)return fail(rc);
        bos_[i].token=out.token;
        if(!out.token||out.reserved||out.bytes<bytes)return fail(Invalid);
        for(unsigned j=0;j<i;++j)if(out.token==bos_[j].token)return fail(Invalid);
        rc=transport_.mapView(transport_.context,out.token,bos_[i].address,bos_[i].bytes);if(rc)return fail(rc);
        auto &b=bos_[i];
        if(!b.address||(b.address&3)||b.bytes<bytes||b.bytes>out.bytes||b.bytes>UINT64_MAX-b.address)return fail(Invalid);
        for(unsigned j=0;j<i;++j)if(b.address<bos_[j].address+bos_[j].bytes&&bos_[j].address<b.address+b.bytes)return fail(Invalid);
        stage(MapCPU,i);
    }
    const auto &program=Native540ProbeFixture::program;
    NativeMetal540::Resources resources{};
    resources.shader={bos_[1].iova,4096};resources.scratch={bos_[3].iova,4096};
    resources.buffers[0]={{bos_[2].iova,4096},0,Native540ProbeFixture::outputWords*4,true};
    NativeMetal540::Dispatch dispatch{};
#ifdef NATIVE540_PROBE_GLOBAL
    dispatch.groups[0]=4;
#endif
    uint32_t words[2048]={},written=0;
    stage(Encode);
    if(!NativeMetal540::encode(program,dispatch,resources,words,2048,written,true)||!written||written*4>bos_[0].bytes)return fail(Invalid);
    const uint32_t stateRegs[]={REG_A5XX_SP_CS_CTRL_REG0,REG_A5XX_SP_CS_CONFIG,REG_A5XX_HLSQ_CS_CONFIG,
        REG_A5XX_HLSQ_CS_CNTL,REG_A5XX_SP_CS_OBJ_START,REG_A5XX_HLSQ_CS_CNTL_0,
        REG_A5XX_HLSQ_CS_NDRANGE_1,REG_A5XX_SP_ADDR_MODE_CNTL};
    CheeseburgerGpu::Encoder540 stateEncoder(words+written,2048-written);
    for(unsigned i=0;i<8;++i){
        const uint64_t dest=bos_[3].iova+64+i*4;
        const uint32_t data[]={stateRegs[i],uint32_t(dest),uint32_t(dest>>32)};
        if(!stateEncoder.packet(0x3e,data,3))return fail(Invalid);
    }
    if(!stateEncoder.barrier())return fail(Invalid);
    written+=stateEncoder.size();
    record_.codeWords=program.info.codeWords;record_.ibWords=written;
    stage(Upload);
    for(unsigned i=0;i<4;++i) {
        auto *p=reinterpret_cast<volatile uint32_t *>(bos_[i].address);
        const unsigned n=i?1024:2048;
        for(unsigned j=0;j<n;++j)p[j]=i==2?Native540ProbeFixture::sentinel:0;
    }
    auto *shader=reinterpret_cast<volatile uint32_t *>(bos_[1].address);
    auto *ib=reinterpret_cast<volatile uint32_t *>(bos_[0].address);
    for(unsigned i=0;i<program.info.codeWords;++i)shader[i]=program.code[i];
    for(unsigned i=0;i<written;++i)ib[i]=words[i];
    transport_.order(transport_.context,true);
    for(unsigned i=0;i<program.info.codeWords;++i)if(shader[i]!=program.code[i])return fail(Mismatch);
    for(unsigned i=0;i<written;++i)if(ib[i]!=words[i])return fail(Mismatch);
    // Only IBs belong in Submit.buffers; data/shader/scratch are mapped BOs held
    // by existing AGXDeviceUserClient::submit (all mapped objects, lines866..874).
    // Adding them as IBs would execute data as PM4. No second submission/retry.
    const NativeAdrenoAbiSubmitBuffer buffers[]={{bos_[0].handle,bos_[0].iova,uint64_t(written)*4}};
    stage(Submit);rc=client.submit(buffers,1,record_.fence);if(rc)return fail(rc);stage(Submit);
    stage(Fence);rc=client.wait(record_.fence,200000000ull);if(rc)return fail(rc);stage(Fence);
    transport_.order(transport_.context,false);
    stage(Readback);
    auto *out=reinterpret_cast<volatile const uint32_t *>(bos_[2].address);
    record_.observed=out[0];record_.scratch=*reinterpret_cast<volatile const uint32_t *>(bos_[3].address);
    auto *state=reinterpret_cast<volatile const uint32_t *>(bos_[3].address+64);
    for(unsigned i=0;i<8;++i)record_.state[i]=state[i];
    const unsigned positions[]={0,Native540ProbeFixture::outputWords/2-1,Native540ProbeFixture::outputWords-1,Native540ProbeFixture::outputWords};
    for(unsigned i=0;i<4;++i)record_.samples[i]=out[positions[i]];
    for(unsigned i=0;i<Native540ProbeFixture::outputWords;++i)record_.verified+=out[i]==Native540ProbeFixture::expected;
    for(unsigned i=Native540ProbeFixture::outputWords;i<1024;++i)record_.guardVerified+=out[i]==Native540ProbeFixture::sentinel;
    stage(Readback);
    if(record_.verified!=Native540ProbeFixture::outputWords||record_.guardVerified!=1024-Native540ProbeFixture::outputWords)return fail(Mismatch);
    stage(Complete);return 0;
}
