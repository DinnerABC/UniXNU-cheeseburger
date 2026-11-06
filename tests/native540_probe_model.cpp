// SPDX-License-Identifier: GPL-2.0-only
// CPU transport/ownership model only. Simulated DMA is NEVER hardware proof.
#include "../package/Userland/NativeMetal540Probe.h"
#ifdef NATIVE540_PROBE_GLOBAL
#include "../tools/gpu-native540/probe_global_fixture.h"
#else
#include "../tools/gpu-native540/probe_fixture.h"
#endif
// One host TU: board ring-log globals in existing encoder headers are not inline.
#include "../package/Userland/NativeMetal540Probe.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
struct Model {
    alignas(4096) uint32_t ram[4][2048]{};
    unsigned calls=0, maps=0, allocations=0, submissions=0, waits=0, orders=0;
    unsigned failure=0, operation=0;bool noDMA=false, overflow=false, duplicate=false, shortReply=false;
    int bad(){return ++operation==failure?-99:0;}
    static int call(void *opaque,uint32_t method,const void *input,size_t bytes,void *output,size_t *n) {
        auto &m=*static_cast<Model *>(opaque);++m.calls;
        assert(method!=NativeAdrenoMethodBoFree&&method!=NativeAdrenoMethodBoCpuUnmap&&method!=NativeAdrenoMethodBoImport);
        if(int rc=m.bad())return rc;
        switch(method) {
        case NativeAdrenoMethodQuery: {
            assert(!input&&!bytes&&*n==sizeof(NativeAdrenoAbiParams));
            *static_cast<NativeAdrenoAbiParams *>(output)={1,36,36,0,1u<<28};break;}
        case NativeAdrenoMethodGetDeviceInfo: {
            assert(*n==sizeof(NativeAdrenoAbiDeviceInfo));auto &d=*static_cast<NativeAdrenoAbiDeviceInfo *>(output);
            d={};d.chip_id=0x05040001;d.flags=1;d.va_start=0x100000;d.va_size=0x100000;break;}
        case NativeAdrenoMethodBoAlloc: {
            auto &a=*static_cast<const NativeAdrenoAbiBoAlloc *>(input);assert(bytes==sizeof(a)&&!a.contiguous);
            assert(a.bytes==(m.allocations?4096u:8192u));assert(*n==sizeof(NativeAdrenoAbiBoAllocReply));
            *static_cast<NativeAdrenoAbiBoAllocReply *>(output)={++m.allocations};break;}
        case NativeAdrenoMethodBoMap: {
            auto &a=*static_cast<const NativeAdrenoAbiBoMap *>(input);
            assert(!a.writeback&&a.shareability==2&&!a.reserved&&a.permissions==(a.handle<=2?1u:3u));
            *static_cast<NativeAdrenoAbiBoMapReply *>(output)={a.iova};break;}
        case NativeAdrenoMethodBoCpuMap: {
            auto h=static_cast<const NativeAdrenoAbiBoCpuMap *>(input)->handle;
            *static_cast<NativeAdrenoAbiBoCpuMapReply *>(output)={uint32_t(h),0,h==1?8192u:4096u};break;}
        case NativeAdrenoMethodSubmit: {
            const auto &s=*static_cast<const NativeAdrenoAbiSubmit *>(input);assert(bytes==sizeof(s));
            assert(s.buffer_count==1&&s.buffers[0].handle==1&&s.buffers[0].bytes>0&&s.buffers[0].bytes<=8192);
            assert(m.orders==1);++m.submissions;
            for(unsigned i=0;i<Native540ProbeFixture::program.info.codeWords;++i)assert(m.ram[1][i]==Native540ProbeFixture::code[i]);
            bool constants=false, ids=false, descriptor=false;
            for(unsigned i=0;i+5<s.buffers[0].bytes/4;++i){
                if(m.ram[0][i]==((15u<<18)|(1u<<22))&&m.ram[0][i+1]==1&&m.ram[0][i+2]==0){
                    const uint32_t width=(m.ram[0][i+3]&A5XX_SSBO_1_0_WIDTH__MASK)>>A5XX_SSBO_1_0_WIDTH__SHIFT;
                    const uint32_t height=(m.ram[0][i+4]&A5XX_SSBO_1_1_HEIGHT__MASK)>>A5XX_SSBO_1_1_HEIGHT__SHIFT;
                    assert(m.ram[0][i+3]==((Native540ProbeFixture::outputWords*4u)<<16)&&!(m.ram[0][i+3]&A5XX_SSBO_1_0_FMT__MASK));
                    assert((width|(height<<16))==Native540ProbeFixture::outputWords*4u);descriptor=true;
                }
            }
            for(unsigned i=0;i+114<s.buffers[0].bytes/4;++i) {
                if(m.ram[0][i]==((13u<<18)|(28u<<22)) && m.ram[0][i+1]==1 && m.ram[0][i+2]==0) {
                    for(unsigned j=0;j<112;++j) {
                        const uint32_t expected=j==96?Native540ProbeFixture::outputWords*4u:
                            j==72?Native540ProbeFixture::outputWords:j==76?Native540ProbeFixture::outputWords/64:j==67?64u:
                            (j==71||j==73||j==74||j==75||j==77||j==78)?1u:0u;
                        assert(m.ram[0][i+3+j]==expected);
                    }
                    constants=true;
                }
            }
            for(unsigned i=0;i+1<s.buffers[0].bytes/4;++i)
                if(m.ram[0][i+1]==(A5XX_HLSQ_CS_CNTL_0_WGIDCONSTID(Native540ProbeFixture::program.info.groupID)|A5XX_HLSQ_CS_CNTL_0_UNK0(252)|A5XX_HLSQ_CS_CNTL_0_UNK1(252)|A5XX_HLSQ_CS_CNTL_0_LOCALIDREGID(0))) ids=true;
            assert(constants&&ids&&descriptor);
            for(unsigned i=0;i<1024;++i)assert(m.ram[2][i]==Native540ProbeFixture::sentinel);
            *static_cast<NativeAdrenoAbiSubmitReply *>(output)={1};break;}
        case NativeAdrenoMethodWaitFence: {
            assert(*n==0&&output==nullptr);++m.waits;
            auto &w=*static_cast<const NativeAdrenoAbiWaitFence *>(input);assert(w.fence==1&&w.timeout_ns==200000000);
            // Finite model of THIS disassembled shader, not a general IR3 emulator.
            // Decode the actual mov literal/A5xx cat6 store and use the encoded
            // constant c24.x (byte bound), localID r0.x; no groupID/r48 input.
            // Pin the actual compiler literal and cat6 operands, not a new ISA.
#ifdef NATIVE540_PROBE_GLOBAL
            assert(m.ram[1][31]==0x204cc007u && m.ram[1][36]==0x0400060fu && m.ram[1][37]==0xc7260005u);
            const uint32_t literal=m.ram[1][30];
#else
            assert(m.ram[1][23]==0x204cc006u && m.ram[1][28]==0x0000060du && m.ram[1][29]==0xc7260004u);
            const uint32_t literal=m.ram[1][22];
#endif
            if(!m.noDMA)for(unsigned id=0;id<Native540ProbeFixture::outputWords;++id) {
                const uint32_t offset=id<<2,bound=Native540ProbeFixture::outputWords*4u;
                if(bound>=4 && bound-4>=offset)m.ram[2][id]=literal;
            }
            if(m.overflow)m.ram[2][Native540ProbeFixture::outputWords]=literal;
            m.ram[3][0]=1;break;}
        default:assert(false);
        }
        if(m.shortReply&&output)*n=0;
        return 0;
    }
    static int view(void *opaque,uint32_t token,uint64_t &address,uint64_t &bytes) {
        auto &m=*static_cast<Model *>(opaque);if(int rc=m.bad())return rc;
        ++m.maps;address=reinterpret_cast<uint64_t>(m.ram[m.duplicate?0:token-1]);bytes=token==1?8192:4096;return 0;
    }
    static void order(void *opaque,bool before) {auto &m=*static_cast<Model *>(opaque);assert(before==(m.orders==0));++m.orders;}
    NativeMetal540Probe::Transport transport(){return {call,view,order,nullptr,this,NativeMetal540Probe::InhibitCache};}
};
int main() {
    Model success;NativeMetal540Probe p(success.transport());assert(p.run()==0);
    assert(p.result().stage==NativeMetal540Probe::Complete&&p.result().verified==Native540ProbeFixture::outputWords&&p.result().guardVerified==1024-Native540ProbeFixture::outputWords);
    assert(success.allocations==4&&success.maps==4&&success.submissions==1&&success.waits==1&&success.orders==2);
    auto calls=success.calls;assert(p.run()==NativeMetal540Probe::AlreadyAttempted&&success.calls==calls);
    for(unsigned i=1;i<=success.operation;++i) {
        Model m;m.failure=i;NativeMetal540Probe q(m.transport());assert(q.run()==-99);
        assert(q.result().error==-99&&m.submissions<=1&&m.waits<=1);
        unsigned n=m.calls;assert(q.run()==NativeMetal540Probe::AlreadyAttempted&&m.calls==n);
        // Destruction has no transport/cleanup calls; outstanding maps stay held.
    }
    for(unsigned i=0;i<4;++i) {
        Model m;m.noDMA=i==0;m.overflow=i==1;m.duplicate=i==2;m.shortReply=i==3;
        NativeMetal540Probe q(m.transport());assert(q.run()!=0);
        assert(m.submissions<=1);if(i<2)assert(q.result().stage==NativeMetal540Probe::Readback);
    }
    std::puts("native540 probe model PASS: staged dispatch, 20 injected failures, single submit, readback/guard, no auto-free; simulated DMA only");
}
