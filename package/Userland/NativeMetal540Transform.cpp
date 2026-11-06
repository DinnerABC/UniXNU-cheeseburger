// SPDX-License-Identifier: GPL-2.0-only
#include "NativeMetal540Transform.h"
#include "../../tools/gpu-native540/transform_fixture.h"
void NativeMetal540Transform::stage(Stage s,uint32_t i) {
    record_.stage=s;record_.resource=i;record_.handle=bos_[i].handle;
    record_.iova=bos_[i].iova;record_.cpuBytes=bos_[i].bytes;
    if(transport_.record)transport_.record(transport_.context,record_);
}
int NativeMetal540Transform::fail(int rc) {
    if(!record_.error)record_.error=rc;
    if(transport_.record)transport_.record(transport_.context,record_);
    return record_.error;
}
int NativeMetal540Transform::request(uint32_t method,const void *in,size_t bytes,void *out,size_t expected) {
    size_t n=expected;int rc=transport_.call(transport_.context,method,in,bytes,out,&n);
    return rc?rc:n==expected?0:Invalid;
}
int NativeMetal540Transform::run() {
    if(attempted_)return AlreadyAttempted;
    attempted_=true;
    if(!transport_.call||!transport_.mapView||!transport_.order||transport_.policy!=InhibitCache)return fail(Invalid);
    stage(Begin);NativeMetal540Client client(transport_.call,transport_.context);
    stage(Query);int rc=client.query();if(rc)return fail(rc);
    NativeAdrenoAbiDeviceInfo info{};
    stage(Device);rc=request(NativeAdrenoMethodGetDeviceInfo,nullptr,0,&info,sizeof(info));if(rc)return fail(rc);
    // P1 and constant-probe retained mappings end below start+0x20000.
    // IB 8192, shader/input/output/scratch 4096 = FIVE disjoint new BOs.
    constexpr uint64_t offset=0x20000,extent=6*4096;
    if(info.chip_id!=0x05040001u||info.reserved||info.flags!=NativeAdrenoDeviceClientIova||
       (info.va_start&4095)||info.va_start>=NativeMetal540::AddressLimit||info.va_size<offset+extent||
       info.va_size>NativeMetal540::AddressLimit-info.va_start)return fail(Invalid);
    uint64_t cursor=info.va_start+offset;
    for(unsigned i=0;i<5;++i) {
        const uint64_t bytes=i?4096:8192;
        stage(Allocate,i);rc=client.allocate(bytes,bos_[i].handle);if(rc)return fail(rc);
        for(unsigned j=0;j<i;++j)if(bos_[i].handle==bos_[j].handle)return fail(Invalid);
        stage(MapGPU,i);rc=client.map(bos_[i].handle,cursor,i<3?1:3,bos_[i].iova);if(rc)return fail(rc);
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
    const auto &program=Native540TransformFixture::program;
    NativeMetal540::Resources resources{};
    resources.shader={bos_[1].iova,4096};resources.scratch={bos_[4].iova,4096};
    resources.buffers[0]={{bos_[2].iova,4096},0,1024,false};
    resources.buffers[1]={{bos_[3].iova,4096},0,1024,true};
    NativeMetal540::Dispatch dispatch{};dispatch.groups[0]=4;
    uint32_t words[2048]={},written=0;stage(Encode);
    if(!NativeMetal540::encode(program,dispatch,resources,words,2048,written,true)||!written)return fail(Invalid);
    const uint32_t regs[]={REG_A5XX_SP_CS_CTRL_REG0,REG_A5XX_SP_CS_CONFIG,REG_A5XX_HLSQ_CS_CONFIG,
        REG_A5XX_HLSQ_CS_CNTL,REG_A5XX_SP_CS_OBJ_START,REG_A5XX_HLSQ_CS_CNTL_0,
        REG_A5XX_HLSQ_CS_NDRANGE_1,REG_A5XX_SP_ADDR_MODE_CNTL};
    CheeseburgerGpu::Encoder540 stateEncoder(words+written,2048-written);
    for(unsigned i=0;i<8;++i) {
        const uint64_t dest=bos_[4].iova+64+i*4;
        const uint32_t data[]={regs[i],uint32_t(dest),uint32_t(dest>>32)};
        if(!stateEncoder.packet(0x3e,data,3))return fail(Invalid);
    }
    if(!stateEncoder.barrier())return fail(Invalid);
    written+=stateEncoder.size();if(uint64_t(written)*4>bos_[0].bytes)return fail(Invalid);
    record_.codeWords=program.info.codeWords;record_.ibWords=written;stage(Upload);
    for(unsigned i=0;i<5;++i) {
        auto *p=reinterpret_cast<volatile uint32_t *>(bos_[i].address);
        for(unsigned j=0;j<(i?1024u:2048u);++j)
            p[j]=i==3?Native540TransformFixture::sentinel:
                i==2?(j<256?Native540TransformFixture::inputPattern(j):Native540TransformFixture::sentinel):0;
    }
    auto *shader=reinterpret_cast<volatile uint32_t *>(bos_[1].address);
    auto *ib=reinterpret_cast<volatile uint32_t *>(bos_[0].address);
    for(unsigned i=0;i<program.info.codeWords;++i)shader[i]=program.code[i];
    for(unsigned i=0;i<written;++i)ib[i]=words[i];
    transport_.order(transport_.context,true);
    for(unsigned i=0;i<program.info.codeWords;++i)if(shader[i]!=program.code[i])return fail(Mismatch);
    for(unsigned i=0;i<written;++i)if(ib[i]!=words[i])return fail(Mismatch);
    // Only IB is in submit.buffers, never shader/input/output/scratch as PM4.
    // Existing userclient retains ALL mapped BOs. No second submission/retry.
    const NativeAdrenoAbiSubmitBuffer buffers[]={{bos_[0].handle,bos_[0].iova,uint64_t(written)*4}};
    stage(Submit);rc=client.submit(buffers,1,record_.fence);if(rc)return fail(rc);stage(Submit);
    stage(Fence);rc=client.wait(record_.fence,200000000ull);if(rc)return fail(rc);stage(Fence);
    transport_.order(transport_.context,false);stage(Readback);
    auto *in=reinterpret_cast<volatile const uint32_t *>(bos_[2].address);
    auto *out=reinterpret_cast<volatile const uint32_t *>(bos_[3].address);
    record_.observed=out[0];record_.scratch=*reinterpret_cast<volatile const uint32_t *>(bos_[4].address);
    auto *state=reinterpret_cast<volatile const uint32_t *>(bos_[4].address+64);
    for(unsigned i=0;i<8;++i)record_.state[i]=state[i];
    const unsigned positions[]={0,127,255,256};
    for(unsigned i=0;i<4;++i){record_.samples[i]=out[positions[i]];record_.inputSamples[i]=in[positions[i]];}
    for(unsigned i=0;i<256;++i) {
        const uint32_t src=Native540TransformFixture::inputPattern(i);
        record_.inputVerified+=in[i]==src;
        record_.verified+=out[i]==Native540TransformFixture::expected(src,i);
    }
    for(unsigned i=256;i<1024;++i) {
        record_.inputGuardVerified+=in[i]==Native540TransformFixture::sentinel;
        record_.guardVerified+=out[i]==Native540TransformFixture::sentinel;
    }
    stage(Readback);
    if(record_.inputVerified!=256||record_.verified!=256||record_.inputGuardVerified!=768||record_.guardVerified!=768)return fail(Mismatch);
    stage(Complete);return 0;
}
