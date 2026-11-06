// SPDX-License-Identifier: GPL-2.0-only
// Finite FD540 instruction/PM4/transport host model, NOT hardware proof.
#include "../package/Userland/NativeMetal540Transform.h"
#include "../tools/gpu-native540/transform_fixture.h"
// Existing board encoder globals require a single host TU.
#include "../package/Userland/NativeMetal540Transform.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace F=Native540TransformFixture;
struct Model {
    alignas(4096) uint32_t ram[5][2048]{};
    uint32_t constants[512]={}, widths[2]={}, state[8]={}, groups[3]={};
    uint64_t addresses[2]={};
    unsigned calls=0,maps=0,allocations=0,submissions=0,waits=0,orders=0;
    unsigned operation=0,failure=0,records=0;uint32_t constCount=0;
    bool noDMA=false,overflow=false,inputWrite=false,alias=false,duplicateHandle=false;
    bool duplicateToken=false,shortReply=false,badView=false,badDevice=false,badMap=false,corruptUpload=false;
    int bad(){return ++operation==failure?-99:0;}
    static constexpr uint64_t va=0x100000;
    static constexpr uint64_t iovas[5]={va+0x20000,va+0x22000,va+0x23000,va+0x24000,va+0x25000};
    void decode(const uint32_t *ib,unsigned n) {
        bool dims=false, addrs=false, sizes=false, consts=false, shader=false;
        for(unsigned i=0;i<n;) {
            const uint32_t h=ib[i++];unsigned count=0;
            if((h>>28)==4) {
                count=h&127u;const uint32_t reg=(h>>8)&0x3ffff;
                assert(count&&count<=n-i);
                for(unsigned k=0;k<count;++k) {
                    const uint32_t a=reg+k,v=ib[i+k];
                    const uint32_t tracked[]={REG_A5XX_SP_CS_CTRL_REG0,REG_A5XX_SP_CS_CONFIG,REG_A5XX_HLSQ_CS_CONFIG,
                        REG_A5XX_HLSQ_CS_CNTL,REG_A5XX_SP_CS_OBJ_START,REG_A5XX_HLSQ_CS_CNTL_0,
                        REG_A5XX_HLSQ_CS_NDRANGE_1,REG_A5XX_SP_ADDR_MODE_CNTL};
                    for(unsigned j=0;j<8;++j)if(a==tracked[j])state[j]=v;
                }
            } else {
                assert((h>>28)==7);count=h&0x3fff;const unsigned op=(h>>16)&127;
                assert(count<=n-i);
                if(op==0x30) {
                    assert(count>=3);const unsigned block=(ib[i]>>18)&15,units=ib[i]>>22,type=ib[i+1];
                    if(block==15) {
                        assert(units==2&&count==7);
                        if(type==1) {
                            for(unsigned k=0;k<2;++k) {
                                uint32_t low=ib[i+3+2*k],high=ib[i+4+2*k];
                                assert(!(low&A5XX_SSBO_1_0_FMT__MASK));
                                widths[k]=((low&A5XX_SSBO_1_0_WIDTH__MASK)>>A5XX_SSBO_1_0_WIDTH__SHIFT)|
                                    (((high&A5XX_SSBO_1_1_HEIGHT__MASK)>>A5XX_SSBO_1_1_HEIGHT__SHIFT)<<16);
                                assert(widths[k]==1024);
                            }sizes=true;
                        }else{assert(type==2);for(unsigned k=0;k<2;++k)addresses[k]=uint64_t(ib[i+3+2*k])|(uint64_t(ib[i+4+2*k])<<32);addrs=true;}
                    }else if(block==13&&type==1) {
                        assert(units==F::program.info.constlen&&count==units*4+3);
                        constCount=count-3;std::memcpy(constants,ib+i+3,constCount*4);consts=true;
                    }else if(block==13&&type==0) {
                        assert(units==F::program.info.instrlen&&count==F::program.info.codeWords+3);
                        for(unsigned k=0;k<F::program.info.codeWords;++k)assert(ib[i+3+k]==F::code[k]);shader=true;
                    }
                }else if(op==0x33){assert(count==4&&!ib[i]);for(unsigned k=0;k<3;++k)groups[k]=ib[i+1+k];dims=true;}
                else if(op==0x3e) {
                    assert(count==3);const uint64_t dest=uint64_t(ib[i+1])|(uint64_t(ib[i+2])<<32);
                    assert(dest>=iovas[4]+64&&dest<iovas[4]+96);
                }
            }
            i+=count;
        }
        assert(dims&&addrs&&sizes&&consts&&shader);
        assert(addresses[0]==iovas[2]&&addresses[1]==iovas[3]&&addresses[0]!=addresses[1]);
        assert(groups[0]==4&&groups[1]==1&&groups[2]==1&&state[6]==256);
        assert(state[4]==iovas[1]&&state[7]==1);
        assert(state[5]==(A5XX_HLSQ_CS_CNTL_0_WGIDCONSTID(F::program.info.groupID)|
            A5XX_HLSQ_CS_CNTL_0_UNK0(252)|A5XX_HLSQ_CS_CNTL_0_UNK1(252)|A5XX_HLSQ_CS_CNTL_0_LOCALIDREGID(0)));
        for(unsigned j=0;j<constCount;++j) {
            const uint32_t expected=j==96||j==97?1024:j==72?256:j==76?4:j==67?64:
                (j==71||j==73||j==74||j==75||j==77||j==78)?1:
                j>=112&&j<114?F::immediates[j-112]:0;
            assert(constants[j]==expected);
        }
        assert(constCount==128&&constants[112]==0&&constants[113]==0x5a5a1234u);
    }
    // Execute THIS disassembled ldgb g0 / xor c28.y / add globalID / stgb
    // g1 sequence, using uploaded constants/descriptors (including dynamic
    // bounds), not fixture.expected() or a pre-filled expected output array.
    void execute(uint32_t inputBound,uint32_t outputBound) {
        const auto *isa=ram[1];
        assert(isa[0]==0x200600c0&&isa[1]==0x46d00005); // r48.x<<6
        assert(isa[6]==0x00000005&&isa[7]==0x42100005); // group+local
        assert(isa[38]==0x0b024600&&isa[39]==0xc6e6000c); // ldgb g0
        assert(isa[44]==0x1071000c&&isa[45]==0x43f0000c); // xor c28.y
        assert(isa[48]==0x0005000c&&isa[49]==0x4210000c); // add globalID
        assert(isa[56]==0x05000619&&isa[57]==0xc7260206); // stgb g1
        const uint32_t local=constants[67];assert(local==64);
        for(unsigned group=0;group<groups[0];++group)for(unsigned lane=0;lane<local;++lane) {
            const uint32_t id=(group<<6)+lane,offset=id<<2;
            bool read=inputBound>=4&&inputBound-4>=offset&&widths[0]>=4&&widths[0]-4>=offset;
            const uint32_t value=read?ram[2][id]:constants[112];
            const uint32_t transformed=(value^constants[113])+id;
            if(outputBound>=4&&outputBound-4>=offset&&widths[1]>=4&&widths[1]-4>=offset)
                ram[3][id]=transformed;
        }
    }
    static int call(void *opaque,uint32_t method,const void *input,size_t bytes,void *output,size_t *n) {
        auto &m=*static_cast<Model *>(opaque);++m.calls;
        assert(method!=NativeAdrenoMethodBoFree&&method!=NativeAdrenoMethodBoCpuUnmap&&method!=NativeAdrenoMethodBoImport);
        if(int rc=m.bad())return rc;
        switch(method) {
        case NativeAdrenoMethodQuery:assert(!input&&!bytes&&*n==sizeof(NativeAdrenoAbiParams));*static_cast<NativeAdrenoAbiParams *>(output)={1,36,36,0,1u<<28};break;
        case NativeAdrenoMethodGetDeviceInfo: {
            assert(!input&&!bytes&&*n==sizeof(NativeAdrenoAbiDeviceInfo));auto &d=*static_cast<NativeAdrenoAbiDeviceInfo *>(output);
            d={};d.chip_id=m.badDevice?0x05030001:0x05040001;d.flags=1;d.va_start=va;d.va_size=0x100000;break;}
        case NativeAdrenoMethodBoAlloc: {
            const auto &a=*static_cast<const NativeAdrenoAbiBoAlloc *>(input);assert(bytes==sizeof(a)&&!a.contiguous&&a.bytes==(m.allocations?4096u:8192u));
            assert(*n==sizeof(NativeAdrenoAbiBoAllocReply));++m.allocations;
            *static_cast<NativeAdrenoAbiBoAllocReply *>(output)={m.duplicateHandle?1:m.allocations};break;}
        case NativeAdrenoMethodBoMap: {
            const auto &a=*static_cast<const NativeAdrenoAbiBoMap *>(input);assert(!a.writeback&&a.shareability==2&&!a.reserved);
            assert(a.permissions==(a.handle<=3?1u:3u)&&a.iova==iovas[a.handle-1]);
            *static_cast<NativeAdrenoAbiBoMapReply *>(output)={a.iova+(m.badMap?4096u:0u)};break;}
        case NativeAdrenoMethodBoCpuMap: {
            const uint64_t h=static_cast<const NativeAdrenoAbiBoCpuMap *>(input)->handle;
            *static_cast<NativeAdrenoAbiBoCpuMapReply *>(output)={m.duplicateToken?1u:uint32_t(h),0,h==1?8192u:4096u};break;}
        case NativeAdrenoMethodSubmit: {
            const auto &s=*static_cast<const NativeAdrenoAbiSubmit *>(input);assert(bytes==sizeof(s)&&*n==sizeof(NativeAdrenoAbiSubmitReply));
            assert(s.buffer_count==1&&s.buffers[0].handle==1&&s.buffers[0].iova==iovas[0]&&s.buffers[0].bytes<=8192&&s.buffers[0].bytes);
            assert(m.orders==1);++m.submissions;
            for(unsigned i=0;i<F::program.info.codeWords;++i)assert(m.ram[1][i]==F::code[i]);
            for(unsigned i=0;i<1024;++i){assert(m.ram[2][i]==(i<256?F::inputPattern(i):F::sentinel));assert(m.ram[3][i]==F::sentinel);}
            m.decode(m.ram[0],s.buffers[0].bytes/4);*static_cast<NativeAdrenoAbiSubmitReply *>(output)={19};break;}
        case NativeAdrenoMethodWaitFence: {
            assert(!output&&!*n&&bytes==sizeof(NativeAdrenoAbiWaitFence));++m.waits;
            const auto &w=*static_cast<const NativeAdrenoAbiWaitFence *>(input);assert(w.fence==19&&w.timeout_ns==200000000);
            if(!m.noDMA)m.execute(m.constants[96],m.constants[97]);
            if(m.overflow)m.ram[3][256]=0xabcdef01;
            if(m.inputWrite)m.ram[2][127]^=1;
            m.ram[4][0]=1;for(unsigned i=0;i<8;++i)m.ram[4][16+i]=m.state[i];break;}
        default:assert(false);
        }
        if(m.shortReply&&output)*n=0;
        return 0;
    }
    static int view(void *opaque,uint32_t token,uint64_t &address,uint64_t &bytes) {
        auto &m=*static_cast<Model *>(opaque);if(int rc=m.bad())return rc;++m.maps;
        address=reinterpret_cast<uint64_t>(m.ram[m.alias?0:token-1]);bytes=(token==1?8192:4096)-(m.badView?4:0);return 0;
    }
    static void order(void *opaque,bool before) {
        auto &m=*static_cast<Model *>(opaque);assert(before==(m.orders==0));++m.orders;
        if(before&&m.corruptUpload)m.ram[1][0]^=1;
    }
    static void record(void *opaque,const NativeMetal540Transform::Record &){++static_cast<Model *>(opaque)->records;}
    NativeMetal540Transform::Transport transport(){return {call,view,order,record,this,NativeMetal540Transform::InhibitCache};}
};
static void planTests() {
    NativeMetal540::Dispatch d;d.groups[0]=4;
    NativeMetal540::Resources r;r.shader={0x122000,4096};r.scratch={0x125000,4096};
    r.buffers[0]={{0x123000,4096},0,1024,false};r.buffers[1]={{0x124000,4096},0,1024,true};
    NativeMetal540::Plan p;assert(NativeMetal540::plan(F::program,d,r,p));
    assert(p.descriptorBytes[0]==1024&&p.descriptorBytes[1]==1024&&p.constants[96]==1024&&p.constants[97]==1024);
    r.buffers[0].bytes=508;r.buffers[1].bytes=516;assert(NativeMetal540::plan(F::program,d,r,p));
    assert(p.constants[96]==508&&p.constants[97]==516&&p.descriptorBytes[0]==508&&p.descriptorBytes[1]==516);
    r.buffers[1].writable=false;assert(!NativeMetal540::plan(F::program,d,r,p));r.buffers[1].writable=true;
    r.buffers[1].allocation=r.buffers[0].allocation;assert(!NativeMetal540::plan(F::program,d,r,p));
    r.buffers[1].allocation={0x124000,4096};r.buffers[0].bytes=2;assert(!NativeMetal540::plan(F::program,d,r,p));
    r.buffers[0].bytes=4100;assert(!NativeMetal540::plan(F::program,d,r,p));r.buffers[0].bytes=1024;
    auto program=F::program;program.info.writeMask=4;assert(!NativeMetal540::plan(program,d,r,p));
    program=F::program;program.info.strides[1]=0;assert(!NativeMetal540::plan(program,d,r,p));
    program=F::program;program.info.constlen=28;assert(!NativeMetal540::plan(program,d,r,p));
    uint32_t stream[8];for(auto &v:stream)v=0xabcdef01;uint32_t written=55;
    assert(!NativeMetal540::encode(F::program,d,r,stream,8,written,true)&&!written);for(auto v:stream)assert(v==0xabcdef01);
}
int main() {
    planTests();Model success;NativeMetal540Transform t(success.transport());assert(t.run()==0);
    const auto &v=t.result();assert(v.stage==NativeMetal540Transform::Complete&&v.verified==256&&v.inputVerified==256&&v.guardVerified==768&&v.inputGuardVerified==768);
    assert(v.fence==19&&v.scratch==1&&v.samples[1]==F::expected(F::inputPattern(127),127)&&v.samples[3]==F::sentinel);
    assert(v.inputSamples[2]==F::inputPattern(255)&&v.state[6]==256&&success.records>20);
    assert(success.allocations==5&&success.maps==5&&success.submissions==1&&success.waits==1&&success.orders==2);
    unsigned n=success.calls;assert(t.run()==NativeMetal540Transform::AlreadyAttempted&&success.calls==n);
    for(unsigned i=1;i<=success.operation;++i) {
        Model m;m.failure=i;
        {NativeMetal540Transform q(m.transport());assert(q.run()==-99&&q.result().error==-99);n=m.calls;assert(q.run()==NativeMetal540Transform::AlreadyAttempted&&m.calls==n);}
        assert(m.calls==n&&m.submissions<=1&&m.waits<=1); // destructor must NOT free/unmap
    }
    for(unsigned i=0;i<12;++i) {
        Model m;m.noDMA=i==0;m.overflow=i==1;m.inputWrite=i==2;m.alias=i==3;m.shortReply=i==4;m.badView=i==5;
        m.badDevice=i==6;m.badMap=i==7;m.corruptUpload=i==8;m.duplicateHandle=i==9;m.duplicateToken=i==10;
        auto tr=m.transport();if(i==11)tr.policy=static_cast<NativeMetal540Transform::CpuPolicy>(0);
        NativeMetal540Transform q(tr);assert(q.run()!=0&&m.submissions<=1&&m.waits<=1);
        if(i<3)assert(q.result().stage==NativeMetal540Transform::Readback);else assert(m.submissions==0);
    }
    // Dynamic input and output bounds are independent: 127 readable uints,
    // 129 writable uints. Out-of-input reads become zero; stores stop at129.
    for(unsigned i=0;i<1024;++i)success.ram[3][i]=F::sentinel;
    success.execute(508,516);
    for(unsigned i=0;i<256;++i)assert(success.ram[3][i]==(i<129?((i<127?F::inputPattern(i):0)^0x5a5a1234u)+i:F::sentinel));
    for(unsigned i=256;i<1024;++i)assert(success.ram[3][i]==F::sentinel);
    std::printf("native540 transform model PASS: 5 BOs, ldgb/xor/add/stgb finite ISA model, 4x64, input256/output256/guard768, independent bounds, %u injected failures, no free/retry; simulated DMA only\n",success.operation);
}
