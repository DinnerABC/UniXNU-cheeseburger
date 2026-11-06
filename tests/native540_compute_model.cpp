// SPDX-License-Identifier: GPL-2.0-only
// Build with -DNATIVE540_EXPORTED_HEADER='"/absolute/export/program.h"'.
// Real compiler ISA is never submitted; PM4 is decoded on the host only.
#include NATIVE540_EXPORTED_HEADER
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using namespace NativeMetal540;
struct Packet { uint32_t op; std::vector<uint32_t> data; };
struct Stream {
    std::vector<Packet> packets;
    std::vector<std::pair<uint32_t,uint32_t>> regs;
    Stream(const uint32_t *w, uint32_t n) {
        for (uint32_t i = 0; i < n;) {
            const uint32_t h = w[i++];
            if ((h >> 28) == 4) {
                const uint32_t count=h&127,address=(h>>8)&0x3ffff;
                assert(count && count<n-i+1);
                for(uint32_t j=0;j<count;++j)regs.emplace_back(address+j,w[i++]);
            } else {
                assert((h >> 28) == 7);
                uint32_t count = h & 0x3fff, op = (h >> 16) & 127;
                assert(h == CheeseburgerGpu::packet7(op, count) && count <= n - i);
                packets.push_back({op, std::vector<uint32_t>(w+i, w+i+count)}); i += count;
            }
        }
    }
    uint32_t reg(uint32_t a) const {
        for (auto it = regs.rbegin(); it != regs.rend(); ++it) if (it->first == a) return it->second;
        assert(false); return 0;
    }
};
static Resources resources() {
    Resources r; r.shader = {0x100000000ull, 16384}; r.scratch = {0x100100000ull,4096};
    for (unsigned i=0;i<3;++i) r.buffers[i] = {{0x200000000ull+i*0x100000,131072},16,65536,i==1};
    return r;
}
static void reject(Program p, Dispatch d, Resources r) {
    uint32_t words[2048], written=55;
    for (auto &w:words)w=0xdeadbeef;
    assert(!encode(p,d,r,words,2048,written) && !written);
    for (auto w:words)assert(w==0xdeadbeef);
}
int main() {
    static_assert(A5XX_SSBO_1_0_WIDTH(256)==0x01000000u,"A540 byte width occupies bits31:16");
    static_assert(A5XX_SSBO_1_0_WIDTH(1)==0x00010000u,"64KiB exact-bounds extent low word");
    static_assert(A5XX_SSBO_1_1_HEIGHT(1)==1,"64KiB extent high word");
    Program p = Native540Export::program;
    Dispatch d; d.local[0]=64; d.groups[0]=4; d.threadgroupLengths[0]=4096;
    auto r=resources(); Plan q;
    assert(p.info.codeWords==960 && p.info.instrlen==30 && p.info.constlen==36);
    assert(p.info.flags==0x040000ec && p.info.bufferMask==7 && p.info.writeMask==2);
    assert(p.info.fullRegs==6 && p.info.halfRegs==11 && p.info.branchStack==2);
    assert(plan(p,d,r,q) && q.sharedBytes==4096 && q.constantWords==144 && q.cached==30);
    assert(q.constants[0]==0 && q.constants[1]==4096);
    for(unsigned i=2;i<64;++i)assert(!q.constants[i]);
    assert(q.constants[67]==64 && q.constants[71]==1 && q.constants[72]==256 && q.constants[75]==1);
    assert(q.constants[76]==4 && q.constants[77]==1 && q.constants[78]==1);
    for(unsigned i=80;i<96;++i)assert(!q.constants[i]);
    for(unsigned i=0;i<16;++i) {
        assert(q.constants[96+i]==(i<3?65536u:0u));
        assert(q.descriptorBytes[i]==(i<3?65537u:0u));
        assert(q.addresses[i]==(i<3?r.buffers[i].allocation.address+16:0));
    }
    const uint32_t driver[]={4,1,1,1,0,0,0,64,64,1,1,6};
    for(unsigned i=0;i<12;++i)assert(q.constants[112+i]==driver[i]);
    for(unsigned i=124;i<128;++i)assert(!q.constants[i]);
    for(unsigned i=0;i<11;++i)assert(q.constants[128+i]==p.immediates[i]);
    for(unsigned i=139;i<144;++i)assert(!q.constants[i]);
    uint32_t words[2048],n=0;
    assert(encode(p,d,r,words,2048,n)&&n>0); Stream s(words,n);
    assert(s.reg(REG_A5XX_SP_CS_CTRL_REG0)==(2|4|(6u<<10)|(11u<<4)|(2u<<24)));
    assert(s.reg(REG_A5XX_HLSQ_CS_CNTL)==61 && s.reg(REG_A5XX_HLSQ_CS_CONSTLEN)==9);
    assert(s.reg(REG_A5XX_HLSQ_CS_INSTRLEN)==30 && s.reg(REG_A5XX_HLSQ_CS_CNTL_1)==1);
    assert(s.reg(REG_A5XX_HLSQ_CS_CNTL_0)==0x00fcfcc0);
    assert(s.reg(REG_A5XX_HLSQ_CS_NDRANGE_1)==256);
    unsigned loads=0,descriptors=0,constants=0,execs=0,flushes=0;
    for(const auto &packet:s.packets) {
        const auto &v=packet.data;
        if(packet.op==0x30) {
            unsigned block=(v[0]>>18)&15;
            if(block==13 && ((v[0]>>16)&3)==2) {
                ++loads;assert(v.size()==3 && (v[0]>>22)==30 && v[1]==0 && v[2]==1);
            } else if(block==13) {
                ++constants;assert(v.size()==147 && v[1]==1 && (v[0]>>22)==36);
                for(unsigned i=0;i<144;++i)assert(v[3+i]==q.constants[i]);
            } else if(block==15) {
                ++descriptors;assert(v.size()==9 && (v[0]>>22)==3);
                for(unsigned i=0;i<3;++i) {
                    assert(v[3+i*2]==(v[1]==1?A5XX_SSBO_1_0_WIDTH(q.descriptorBytes[i]&65535):uint32_t(q.addresses[i])));
                    assert(v[4+i*2]==(v[1]==1?A5XX_SSBO_1_1_HEIGHT(q.descriptorBytes[i]>>16):uint32_t(q.addresses[i]>>32)));
                }
            } else assert(false);
        } else if(packet.op==0x33) {++execs;assert(v==std::vector<uint32_t>({0,4,1,1}));}
        else if(packet.op==0x46) {++flushes;assert(v[0]==4 && v.size()==4);}
    }
    assert(loads==1&&descriptors==2&&constants==1&&execs==1&&flushes==1);
    uint32_t small[2]={17,19},written=99;
    assert(!encode(p,d,r,small,2,written)&&!written&&small[0]==17&&small[1]==19);
    // Cache transition at 32 instruction units. Only metadata model: synthetic
    // allocations do not claim compiler validation or execute these instructions.
    auto cached=p;cached.info.codeWords=1024;cached.info.instrlen=32;
    assert(encode(cached,d,r,words,2048,n));Stream c32(words,n);
    assert(c32.reg(REG_A5XX_HLSQ_CS_INSTRLEN)==32);
    cached.info.codeWords=1056;cached.info.instrlen=33;
    assert(encode(cached,d,r,words,2048,n));Stream c33(words,n);
    assert(c33.reg(REG_A5XX_HLSQ_CS_INSTRLEN)==0 && !(c33.reg(REG_A5XX_SP_CS_CTRL_REG0)&4));
    for(const auto &packet:c33.packets)if(packet.op==0x30)assert(!(((packet.data[0]>>18)&15)==13&&((packet.data[0]>>16)&3)==2));
    for(uint32_t bytes:{65535u,65536u,65537u,131072u}) {
        auto b=r; b.buffers[0].offset=0;b.buffers[0].bytes=bytes;
        assert(plan(p,d,b,q)&&q.constants[96]==bytes&&q.descriptorBytes[0]==bytes+uint32_t(!(bytes&65535)));
    }
    auto exact=p;exact.info.flags&=~ExactBufferBytes;
    reject(exact,d,r); // low HW width zero without exact shader bounds
    auto nonzero=r;for(auto &b:nonzero.buffers)if(b.bytes)b.bytes=65535;
    assert(plan(exact,d,nonzero,q)&&memoryConstantWords(exact.info)==80);
    auto shared=d;shared.threadgroupLengths[0]=32768;assert(plan(p,shared,r,q)&&q.sharedBytes==32768);
    for(uint32_t size:{0u,1u,15u,32769u,32784u}) {shared.threadgroupLengths[0]=size;reject(p,shared,r);}
    auto staticShared=p;staticShared.info.sharedBytes=16;
    shared.threadgroupLengths[0]=32752;assert(plan(staticShared,shared,r,q));
    shared.threadgroupLengths[0]=32768;reject(staticShared,shared,r);
    staticShared.info.sharedBytes=4;reject(staticShared,d,r);
    shared=d;shared.threadgroupLengths[1]=16;reject(p,shared,r);
    auto geometry=d;geometry.groups[1]=2;geometry.local[1]=2;
    assert(plan(p,geometry,r,q)&&q.dimensions==2&&q.constants[113]==2&&q.constants[121]==2);
    for(unsigned axis=0;axis<3;++axis) {
        geometry=d;geometry.groups[axis]=0;reject(p,geometry,r);
        geometry=d;geometry.local[axis]=0;reject(p,geometry,r);
        geometry=d;geometry.groups[axis]=65536;reject(p,geometry,r);
    }
    geometry=d;geometry.local[0]=1024;geometry.local[1]=2;reject(p,geometry,r);
    geometry=d;geometry.local[2]=65;reject(p,geometry,r);
    geometry=d;geometry.groups[0]=65535;geometry.groups[1]=5;reject(p,geometry,r);
    auto fixed=p;fixed.info.flags&=~VariableLocal;
    geometry=d;geometry.local[0]=32;reject(fixed,geometry,r);
    auto bad=p;bad.info.immediateBase=112;reject(bad,d,r);
    bad=p;bad.info.driverBase=108;reject(bad,d,r);
    bad=p;bad.info.constlen=32;reject(bad,d,r);
    bad=p;bad.info.threadgroupMask=0;reject(bad,d,r);
    bad=p;bad.info.branchStack=4;reject(bad,d,r);
    bad=p;bad.info.writeMask=8;reject(bad,d,r);
    bad=p;bad.info.strides[3]=4;reject(bad,d,r);
    bad=p;bad.info.flags|=1u<<30;reject(bad,d,r);
    bad=p;bad.code=nullptr;reject(bad,d,r);
    bad=p;bad.immediates=nullptr;reject(bad,d,r);
    auto bounds=r;bounds.shader.address+=4;reject(p,d,bounds);
    bounds=r;bounds.shader.bytes=3836;reject(p,d,bounds);
    bounds=r;bounds.scratch.address=AddressLimit-2;reject(p,d,bounds);
    bounds=r;bounds.buffers[1].writable=false;reject(p,d,bounds);
    bounds=r;bounds.buffers[0].offset=131071;reject(p,d,bounds);
    bounds=r;bounds.buffers[3]=r.buffers[0];reject(p,d,bounds);
    bounds=r;bounds.buffers[0].allocation.address=r.shader.address;reject(p,d,bounds);
    bounds=r;bounds.buffers[0].allocation.address=r.buffers[1].allocation.address;reject(p,d,bounds);
    bounds=r;bounds.buffers[0].allocation={AddressLimit-65536,65536};bounds.buffers[0].offset=0;
    assert(plan(p,d,bounds,q));bounds.buffers[0].allocation.address+=4;reject(p,d,bounds);
    std::cout << "native540 real-CS 3-buffer constants/shared/cache/bounds/PM4 refusal model PASS\n";
}
