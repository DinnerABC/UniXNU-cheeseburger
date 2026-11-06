// SPDX-License-Identifier: GPL-2.0-only
// Decode real submitted PM4 + simulate the reviewed LLVM copy body using
// uploaded params/source, NEVER execute GPU ISA or seed an oracle output.
#include "../package/Userland/NativeMetal540AIRProbe.h"
#include "../tools/gpu-native540/air_fixture.h"
// Existing board encoder globals require a single host translation unit.
#include "../package/Userland/NativeMetal540AIRProbe.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace Fixture=NativeMetal540AIRFixture;
using Probe=NativeMetal540AIRProbe;
struct Model {
    alignas(4096) uint32_t ram[6][2048]={};
    uint32_t constants[512]={},widths[3]={},groups[3]={},permissions[6]={};
    uint64_t addresses[3]={};
    unsigned calls=0,maps=0,allocations=0,submissions=0,waits=0,orders=0;
    unsigned operation=0,failure=0,records=0,constCount=0,dispatchCount=0;
    enum Fault { None,NoCopy,OutputGuard,InputValue,InputGuard,ParamValue,ParamGuard,
        Alias,ShortReply,BadView,BadDevice,BadMap,ShaderUpload,IbUpload,InputUpload,
        ParamUpload,OutputUpload,DuplicateHandle,DuplicateToken,NullView,
        MisalignedView,OverflowView,BadCpuReply,ZeroFence,BadWaitReply,SmallVa };
    Fault fault=None;
    Probe::Record last{};
    int bad(){return ++operation==failure?-99:0;}
    static constexpr uint64_t va=0x100000;
    static constexpr uint64_t iovas[6]={va+0x40000,va+0x42000,va+0x43000,va+0x44000,va+0x45000,va+0x46000};
    void decode(const uint32_t *ib,unsigned n) {
        bool dims=false,addrs=false,sizes=false,consts=false,shader=false;
        uint32_t object=0,idMapping=0,grid=0,addressMode=0,control=0;
        for(unsigned i=0;i<n;) {
            const uint32_t h=ib[i++];unsigned count=0;
            if((h>>28)==4) {
                count=h&127u;const uint32_t reg=(h>>8)&0x3ffff;
                assert(count&&count<=n-i);
                for(unsigned k=0;k<count;++k) {
                    const uint32_t a=reg+k,v=ib[i+k];
                    if(a==REG_A5XX_SP_CS_OBJ_START)object=v;
                    if(a==REG_A5XX_HLSQ_CS_CNTL_0)idMapping=v;
                    if(a==REG_A5XX_HLSQ_CS_NDRANGE_1)grid=v;
                    if(a==REG_A5XX_SP_ADDR_MODE_CNTL)addressMode=v;
                    if(a==REG_A5XX_SP_CS_CTRL_REG0)control=v;
                }
            } else {
                assert((h>>28)==7);count=h&0x3fff;const unsigned op=(h>>16)&127;
                assert(count<=n-i);
                if(op==0x30) {
                    assert(count>=3);const unsigned block=(ib[i]>>18)&15,units=ib[i]>>22,type=ib[i+1];
                    if(block==15) {
                        assert(units==3&&count==9);
                        if(type==1) {
                            const uint32_t expected[]={3120,1024,48};
                            for(unsigned k=0;k<3;++k) {
                                const uint32_t low=ib[i+3+2*k],high=ib[i+4+2*k];
                                assert(!(low&A5XX_SSBO_1_0_FMT__MASK));
                                widths[k]=((low&A5XX_SSBO_1_0_WIDTH__MASK)>>A5XX_SSBO_1_0_WIDTH__SHIFT)|
                                    (((high&A5XX_SSBO_1_1_HEIGHT__MASK)>>A5XX_SSBO_1_1_HEIGHT__SHIFT)<<16);
                                assert(widths[k]==expected[k]);
                            }sizes=true;
                        }else {
                            assert(type==2);
                            for(unsigned k=0;k<3;++k)addresses[k]=uint64_t(ib[i+3+2*k])|(uint64_t(ib[i+4+2*k])<<32);
                            addrs=true;
                        }
                    }else if(block==13&&type==1) {
                        assert(units==36&&count==147);constCount=count-3;
                        std::memcpy(constants,ib+i+3,constCount*4);consts=true;
                    }else if(block==13&&type==0) {
                        assert(units==13&&count==419);
                        for(unsigned k=0;k<416;++k)assert(ib[i+3+k]==Fixture::code[k]);
                        shader=true;
                    }
                }else if(op==0x33){assert(count==4&&!ib[i]);for(unsigned k=0;k<3;++k)groups[k]=ib[i+1+k];dims=true;++dispatchCount;}
                // This probe adds no diagnostic register-to-memory DMA.
                assert(op!=0x3e);
            }
            i+=count;
        }
        assert(dims&&addrs&&sizes&&consts&&shader&&dispatchCount==1);
        assert(Fixture::program.info.bufferMask==7&&Fixture::program.info.writeMask==2);
        assert(addresses[0]==iovas[2]&&addresses[1]==iovas[3]&&addresses[2]==iovas[4]);
        assert(permissions[2]==1&&permissions[3]==3&&permissions[4]==1);
        assert(groups[0]==4&&groups[1]==1&&groups[2]==1&&grid==256);
        assert(object==iovas[1]&&addressMode==1);
        assert(control==(2|A5XX_SP_CS_CTRL_REG0_BUFFER|A5XX_SP_CS_CTRL_REG0_FULLREGFOOTPRINT(6)|
            A5XX_SP_CS_CTRL_REG0_HALFREGFOOTPRINT(4)|A5XX_SP_CS_CTRL_REG0_BRANCHSTACK(1)));
        assert(idMapping==(A5XX_HLSQ_CS_CNTL_0_WGIDCONSTID(192)|A5XX_HLSQ_CS_CNTL_0_UNK0(252)|
            A5XX_HLSQ_CS_CNTL_0_UNK1(252)|A5XX_HLSQ_CS_CNTL_0_LOCALIDREGID(0)));
        for(unsigned j=0;j<constCount;++j) {
            const uint32_t expected=j==96?3120:j==97?1024:j==98?48:j==72?256:j==76?4:j==67?64:
                (j==71||j==73||j==74||j==75||j==77||j==78)?1:
                j>=112&&j<124?Fixture::driverWords[j-112]:j>=128&&j<138?Fixture::immediates[j-128]:0;
            assert(constants[j]==expected);
        }
        assert(constCount==144);
    }
    // Exact reviewed padding_gradient LLVM body (not an IR3 interpreter):
    // resultDims@16, sourceDims@4, pad_x@28, pad_y@36. Float load/store
    // copies bits, with separate descriptor/native-size safety in this model.
    // Deliberately no Fixture::expected/sourceIndex call in execution.
    void execute(uint32_t inputBound,uint32_t outputBound,uint32_t paramBound) {
        assert(addresses[0]==iovas[2]&&addresses[1]==iovas[3]&&addresses[2]==iovas[4]);
        assert(permissions[2]==1&&permissions[3]==3&&permissions[4]==1);
        if(paramBound<40||widths[2]<40)return;
        const auto *p=ram[4];
        for(unsigned gz=0;gz<groups[2];++gz)for(unsigned gy=0;gy<groups[1];++gy)
        for(unsigned gx=0;gx<groups[0];++gx)for(unsigned lane=0;lane<constants[120];++lane) {
            const int32_t x=int32_t(gx*constants[120]+lane),y=int32_t(gy),z=int32_t(gz);
            if(x>=int32_t(p[4])||y>=int32_t(p[5])||z>=int32_t(p[6]))continue;
            const int32_t src=x+int32_t(p[7])+int32_t(p[1])*(y+int32_t(p[9])+int32_t(p[2])*z);
            const int32_t dst=x+int32_t(p[4])*(y+int32_t(p[5])*z);
            assert(src>=0&&dst>=0);
            const uint32_t readOffset=uint32_t(src)*4,writeOffset=uint32_t(dst)*4;
            if(inputBound<4||readOffset>inputBound-4||widths[0]<4||readOffset>widths[0]-4)continue;
            if(outputBound<4||writeOffset>outputBound-4||widths[1]<4||writeOffset>widths[1]-4)continue;
            ram[3][dst]=ram[2][src];
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
            d={};d.chip_id=m.fault==BadDevice?0x05030001:0x05040001;d.flags=1;d.va_start=va;d.va_size=m.fault==SmallVa?0x46000:0x100000;break;}
        case NativeAdrenoMethodBoAlloc: {
            const auto &a=*static_cast<const NativeAdrenoAbiBoAlloc *>(input);
            assert(bytes==sizeof(a)&&!a.contiguous&&a.bytes==(m.allocations?4096u:8192u));
            assert(*n==sizeof(NativeAdrenoAbiBoAllocReply));++m.allocations;
            *static_cast<NativeAdrenoAbiBoAllocReply *>(output)={m.fault==DuplicateHandle?1:m.allocations};break;}
        case NativeAdrenoMethodBoMap: {
            const auto &a=*static_cast<const NativeAdrenoAbiBoMap *>(input);assert(bytes==sizeof(a)&&!a.writeback&&a.shareability==2&&!a.reserved);
            assert(a.handle>=1&&a.handle<=6&&a.permissions==(a.handle==4||a.handle==6?3u:1u)&&a.iova==iovas[a.handle-1]);
            m.permissions[a.handle-1]=a.permissions;
            *static_cast<NativeAdrenoAbiBoMapReply *>(output)={a.iova+(m.fault==BadMap?4096u:0u)};break;}
        case NativeAdrenoMethodBoCpuMap: {
            const uint64_t h=static_cast<const NativeAdrenoAbiBoCpuMap *>(input)->handle;
            *static_cast<NativeAdrenoAbiBoCpuMapReply *>(output)={m.fault==DuplicateToken?1u:uint32_t(h),m.fault==BadCpuReply?1u:0u,h==1?8192u:4096u};break;}
        case NativeAdrenoMethodSubmit: {
            const auto &s=*static_cast<const NativeAdrenoAbiSubmit *>(input);assert(bytes==sizeof(s)&&*n==sizeof(NativeAdrenoAbiSubmitReply));
            assert(s.buffer_count==1&&s.buffers[0].handle==1&&s.buffers[0].iova==iovas[0]&&s.buffers[0].bytes<=8192&&s.buffers[0].bytes);
            assert(m.orders==1);++m.submissions;
            for(unsigned i=0;i<416;++i)assert(m.ram[1][i]==Fixture::code[i]);
            uint32_t params[12];std::memcpy(params,&Fixture::params,sizeof(params));
            for(unsigned i=0;i<1024;++i) {
                assert(m.ram[2][i]==(i<780?Fixture::inputPattern(i):Fixture::sentinel));
                assert(m.ram[3][i]==Fixture::sentinel);
                assert(m.ram[4][i]==(i<12?params[i]:Fixture::sentinel));
            }
            m.decode(m.ram[0],unsigned(s.buffers[0].bytes/4));
            *static_cast<NativeAdrenoAbiSubmitReply *>(output)={m.fault==ZeroFence?0u:29u};break;}
        case NativeAdrenoMethodWaitFence: {
            assert(!output&&!*n&&bytes==sizeof(NativeAdrenoAbiWaitFence));++m.waits;
            const auto &w=*static_cast<const NativeAdrenoAbiWaitFence *>(input);assert(w.fence==29&&w.timeout_ns==200000000);
            if(m.fault!=NoCopy)m.execute(m.constants[96],m.constants[97],m.constants[98]);
            if(m.fault==OutputGuard)m.ram[3][256]^=1;
            if(m.fault==InputValue)m.ram[2][389]^=1;
            if(m.fault==InputGuard)m.ram[2][780]^=1;
            if(m.fault==ParamValue)m.ram[4][7]^=1;
            if(m.fault==ParamGuard)m.ram[4][12]^=1;
            m.ram[5][0]=1;if(m.fault==BadWaitReply)*n=4;break;}
        default:assert(false);
        }
        if(m.fault==ShortReply&&output)*n=0;
        return 0;
    }
    static int view(void *opaque,uint32_t token,uint64_t &address,uint64_t &bytes) {
        auto &m=*static_cast<Model *>(opaque);if(int rc=m.bad())return rc;++m.maps;
        address=reinterpret_cast<uint64_t>(m.ram[m.fault==Alias?0:token-1]);
        bytes=(token==1?8192:4096)-(m.fault==BadView?4:0);
        if(m.fault==NullView)address=0;
        if(m.fault==MisalignedView)++address;
        if(m.fault==OverflowView)address=UINT64_MAX-3;
        return 0;
    }
    static void order(void *opaque,bool before) {
        auto &m=*static_cast<Model *>(opaque);assert(before==(m.orders==0));++m.orders;
        if(!before)return;
        if(m.fault==ShaderUpload)m.ram[1][0]^=1;
        if(m.fault==IbUpload)m.ram[0][0]^=1;
        if(m.fault==InputUpload)m.ram[2][0]^=1;
        if(m.fault==ParamUpload)m.ram[4][0]^=1;
        if(m.fault==OutputUpload)m.ram[3][0]^=1;
    }
    static void record(void *opaque,const Probe::Record &r) {
        auto &m=*static_cast<Model *>(opaque);++m.records;
        if(m.last.error)assert(r.error==m.last.error);
        m.last=r;
    }
    Probe::Transport transport(){return {call,view,order,record,this,Probe::InhibitCache};}
};
static void planTests() {
    NativeMetal540::Resources r;r.shader={Model::iovas[1],4096};r.scratch={Model::iovas[5],4096};
    r.buffers[0]={{Model::iovas[2],4096},0,3120,false};r.buffers[1]={{Model::iovas[3],4096},0,1024,true};r.buffers[2]={{Model::iovas[4],4096},0,48,false};
    NativeMetal540::Plan q;assert(NativeMetal540::plan(Fixture::program,Fixture::dispatch,r,q));
    assert(q.constants[96]==3120&&q.constants[97]==1024&&q.constants[98]==48);
    for(unsigned i=0;i<12;++i)assert(q.constants[112+i]==Fixture::driverWords[i]);
    r.buffers[1].writable=false;assert(!NativeMetal540::plan(Fixture::program,Fixture::dispatch,r,q));r.buffers[1].writable=true;
    r.buffers[1].allocation=r.buffers[0].allocation;assert(!NativeMetal540::plan(Fixture::program,Fixture::dispatch,r,q));
    r.buffers[1].allocation={Model::iovas[3],4096};r.buffers[2].bytes=2;assert(!NativeMetal540::plan(Fixture::program,Fixture::dispatch,r,q));
    r.buffers[2].bytes=48;auto program=Fixture::program;program.info.writeMask=8;assert(!NativeMetal540::plan(program,Fixture::dispatch,r,q));
    uint32_t shortIb[8];for(auto &x:shortIb)x=0xabcdef01;uint32_t n=55;
    assert(!NativeMetal540::encode(Fixture::program,Fixture::dispatch,r,shortIb,8,n,true)&&!n);
    for(auto x:shortIb)assert(x==0xabcdef01);
}
int main() {
    planTests();Model success;Probe probe(success.transport());assert(probe.run()==0);
    const auto &r=probe.result();assert(r.stage==Probe::Complete&&!r.error&&r.codeWords==416&&r.ibWords==868&&r.fence==29);
    assert(r.inputVerified==780&&r.inputGuardVerified==244&&r.paramVerified==12&&r.paramGuardVerified==1012&&r.verified==256&&r.guardVerified==768);
    assert(r.samples[0]==Fixture::inputPattern(262)&&r.samples[1]==Fixture::inputPattern(389)&&r.samples[2]==Fixture::inputPattern(517)&&r.samples[3]==Fixture::sentinel);
    for(unsigned i=0;i<3;++i)assert(r.samples[i]==r.inputSamples[i]);assert(r.inputSamples[3]==Fixture::sentinel&&r.scratch==1);
    assert(std::strstr(r.producer,"AIR-NIR-A540-IR3")&&!std::strstr(r.producer,"GLSL"));
    assert(success.allocations==6&&success.maps==6&&success.submissions==1&&success.waits==1&&success.orders==2&&success.records>25);
    unsigned n=success.calls;assert(probe.run()==Probe::AlreadyAttempted&&success.calls==n);
    for(unsigned i=1;i<=success.operation;++i) {
        Model m;m.failure=i;
        {Probe q(m.transport());assert(q.run()==-99&&q.result().error==-99);n=m.calls;assert(q.run()==Probe::AlreadyAttempted&&m.calls==n);
         if(q.result().stage==Probe::Fence)assert(q.result().fence==29&&!q.result().verified&&!q.result().inputVerified&&m.orders==1);}
        assert(m.calls==n&&m.submissions<=1&&m.waits<=1); // no destructor cleanup
    }
    for(unsigned i=Model::NoCopy;i<=Model::SmallVa;++i) {
        Model m;m.fault=static_cast<Model::Fault>(i);Probe q(m.transport());assert(q.run()!=0&&q.result().error);
        assert(m.submissions<=1&&m.waits<=1);
        if(i<=Model::ParamGuard)assert(q.result().stage==Probe::Readback&&q.result().error==Probe::Mismatch);
        else if(i==Model::ZeroFence)assert(m.submissions==1&&!m.waits&&!q.result().verified&&m.orders==1);
        else if(i==Model::BadWaitReply)assert(m.submissions==1&&m.waits==1&&!q.result().verified&&m.orders==1);
        else assert(!m.submissions);
    }
    for(unsigned i=0;i<4;++i) {
        Model m;auto t=m.transport();if(i==0)t.call=nullptr;if(i==1)t.mapView=nullptr;if(i==2)t.order=nullptr;if(i==3)t.policy=static_cast<Probe::CpuPolicy>(0);
        Probe q(t);assert(q.run()==Probe::Invalid&&!m.calls);
    }
    // Distinguish real uploaded params from a hardcoded x+262 oracle: pad_x=3
    // moves the source by one; narrow result/output and input bounds separate.
    for(auto &x:success.ram[3])x=Fixture::sentinel;
    success.ram[4][7]=3;success.execute(3120,1024,48);
    assert(success.ram[3][0]==success.ram[2][263]&&success.ram[3][255]==success.ram[2][518]);
    for(auto &x:success.ram[3])x=Fixture::sentinel;
    success.ram[4][7]=2;success.execute(3120,508,48);
    for(unsigned i=0;i<256;++i)assert(success.ram[3][i]==(i<127?success.ram[2][i+262]:Fixture::sentinel));
    for(auto &x:success.ram[3])x=Fixture::sentinel;
    success.execute(263*4,1024,48);assert(success.ram[3][0]==success.ram[2][262]&&success.ram[3][1]==Fixture::sentinel);
    for(auto &x:success.ram[3])x=Fixture::sentinel;
    success.execute(3120,1024,32);for(unsigned i=0;i<1024;++i)assert(success.ram[3][i]==Fixture::sentinel);
    std::printf("native540 AIR probe model PASS: fixed padding_gradient 416words, PM4 868words, 6 BOs, mask7/write2, 4x64; in780/guard244 param12/guard1012 out256/guard768; %u injected call/view failures, 25 malformed/corrupt cases; reviewed LLVM copy simulated only, NOT GPU execution\n",success.operation);
}
