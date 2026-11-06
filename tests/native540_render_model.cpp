// SPDX-License-Identifier: MIT
#include "NativeMetal540Render.h"
#include "render_fixture.h"
#include "../package/BoardRuntime/A5xxRegs.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
using namespace Native540Render;
struct Model {
    struct BO {uint64_t iova=0;uint32_t perms=0;std::vector<uint32_t> memory;};
    std::array<BO,5> bos{};unsigned allocated=0,calls=0,views=0,orders=0,submits=0,waits=0,failAt=0,failView=0;
    bool render=false,uploadUntouched=true,readbackOrdered=false;std::vector<uint32_t> stream;
    static int call(void *ctx,uint32_t m,const void *in,size_t n,void *out,size_t *bytes) {
        auto &s=*static_cast<Model *>(ctx);if(++s.calls==s.failAt)return -77;
        assert(m!=NativeAdrenoMethodBoFree && m!=NativeAdrenoMethodBoCpuUnmap && m!=NativeAdrenoMethodBoImport);
        auto reply=[&](const auto &v){assert(*bytes==sizeof(v));std::memcpy(out,&v,sizeof(v));return 0;};
        if(m==NativeAdrenoMethodQuery){assert(!in&&!n);return reply(NativeAdrenoAbiParams{1,36,36,0,1u<<28});}
        if(m==NativeAdrenoMethodGetDeviceInfo){NativeAdrenoAbiDeviceInfo d{};d.chip_id=0x05040001;d.va_start=0x40000000;d.va_size=0x1000000;d.flags=1;return reply(d);}
        if(m==NativeAdrenoMethodBoAlloc){assert(n==sizeof(NativeAdrenoAbiBoAlloc));const auto &r=*static_cast<const NativeAdrenoAbiBoAlloc *>(in);assert(s.allocated<5);const uint64_t sizes[]={8192,4096,4096,16384,4096};assert(r.bytes==sizes[s.allocated] && r.align_log2==12 && !r.contiguous);s.bos[s.allocated].memory.resize(r.bytes/4);return reply(NativeAdrenoAbiBoAllocReply{++s.allocated});}
        if(m==NativeAdrenoMethodBoMap){const auto &r=*static_cast<const NativeAdrenoAbiBoMap *>(in);assert(r.handle&&r.handle<=s.allocated);auto &b=s.bos[r.handle-1];assert(!r.writeback&&r.shareability==2&&!r.reserved);b.iova=r.iova;b.perms=r.permissions;return reply(NativeAdrenoAbiBoMapReply{r.iova});}
        if(m==NativeAdrenoMethodBoCpuMap){const auto &r=*static_cast<const NativeAdrenoAbiBoCpuMap *>(in);return reply(NativeAdrenoAbiBoCpuMapReply{uint32_t(r.handle),0,s.bos[r.handle-1].memory.size()*4});}
        if(m==NativeAdrenoMethodSubmit){const auto &r=*static_cast<const NativeAdrenoAbiSubmit *>(in);assert(n==sizeof(r)&&r.buffer_count==1&&!r.queue&&!r.wait_fence&&!r.reserved);assert(r.buffers[0].handle==1 && r.buffers[0].iova==s.bos[0].iova);assert(s.orders==1);++s.submits;
            for(auto w:s.bos[3].memory)if(w!=Sentinel)s.uploadUntouched=false;
            s.stream.assign(s.bos[0].memory.begin(),s.bos[0].memory.begin()+r.buffers[0].bytes/4);
            return reply(NativeAdrenoAbiSubmitReply{17});}
        if(m==NativeAdrenoMethodWaitFence){const auto &r=*static_cast<const NativeAdrenoAbiWaitFence *>(in);assert(r.fence==17&&r.timeout_ns==200000000&&*bytes==0);++s.waits;
            // Explicit software raster model, never device evidence. With render
            // disabled, a successful fence MUST still fail pixel validation.
            if(s.render){for(int y=0;y<64;++y)for(int x=0;x<64;++x){int X=x*2+1,Y=y*2+1;s.bos[3].memory[y*64+x]=(Y<112 && 2*X+Y>144 && 112-2*X+Y>0)?Center:Background;}s.bos[4].memory[0]=1;s.bos[4].memory[2]=2;s.bos[4].memory[4]=3;}return 0;}
        assert(false);return 1;
    }
    static int view(void *ctx,uint32_t token,uint64_t &address,uint64_t &bytes){auto &s=*static_cast<Model *>(ctx);if(++s.views==s.failView)return -78;assert(token&&token<=s.allocated);auto &b=s.bos[token-1];address=reinterpret_cast<uintptr_t>(b.memory.data());bytes=b.memory.size()*4;return 0;}
    static void order(void *ctx,bool before){auto &s=*static_cast<Model *>(ctx);++s.orders;assert(before?s.orders==1:s.waits==1);if(!before)s.readbackOrdered=true;}
    NativeMetal540Render::Transport transport(){return {call,view,order,nullptr,this,NativeMetal540Render::InhibitCache};}
};
int main(int argc,char **argv){
    const Resources r{{0x40100000,8192},{0x40102000,4096},{0x40103000,4096},{0x40104000,16384},{0x40108000,4096}};
    Plan q{};assert(plan(Native540RenderFixture::vs,Native540RenderFixture::fs,r,q));assert(q.position==4&&q.stride==4);
    for(unsigned i=0;i<4;++i)assert(!q.vsConstants[i]);assert(q.vsConstants[20]==Native540RenderFixture::vs.immediates[0]);
    uint32_t words[2048];std::fill(words,words+2048,0xa5a5a5a5);uint32_t written=100;
    assert(encode(Native540RenderFixture::vs,Native540RenderFixture::fs,r,words,2048,written)&&written<2048);
    if(argc==2){std::ofstream out(argv[1],std::ios::binary);out.write(reinterpret_cast<const char *>(words),written*4);assert(out);}
    unsigned draws=0,clears=0,loads=0,flushes=0;
    for(unsigned i=0;i<written;){uint32_t h=words[i++],count=0;if((h>>28)==4){count=h&127;assert(count&&i+count<=written);}
        else{assert((h>>28)==7);count=h&0x3fff;unsigned op=(h>>16)&127;assert(i+count<=written);assert(op!=0x33 && op!=0x3d && op!=0x73);
            if(op==0x38){assert(count==3 && words[i]==(4u|(2u<<10)|(2u<<6))&&words[i+1]==1&&words[i+2]==3);++draws;}
            if(op==0x2c){assert(count==5&&words[i]==0);++clears;}if(op==0x30)++loads;if(op==0x46&&count==4)++flushes;}
        i+=count;}
    assert(draws==1&&clears==1&&loads==3&&flushes==3);
    auto bad=r;bad.target.bytes=4096;assert(!plan(Native540RenderFixture::vs,Native540RenderFixture::fs,bad,q));bad=r;bad.fs.address=bad.vs.address;assert(!plan(Native540RenderFixture::vs,Native540RenderFixture::fs,bad,q));bad=r;bad.target.address=AddressLimit-4096;assert(!plan(Native540RenderFixture::vs,Native540RenderFixture::fs,bad,q));
    auto changed=Native540RenderFixture::vs;changed.info.driverCount=41;assert(!plan(changed,Native540RenderFixture::fs,r,q));changed=Native540RenderFixture::fs;changed.info.flags|=2;assert(!plan(Native540RenderFixture::vs,changed,r,q));
    uint32_t dest[8];std::fill(dest,dest+8,0x12345678);written=99;assert(!encode(Native540RenderFixture::vs,Native540RenderFixture::fs,r,dest,8,written)&&!written);for(auto w:dest)assert(w==0x12345678);
    {Model m;NativeMetal540Render client(m.transport());assert(client.run(false)==NativeMetal540Render::Prerequisite && !m.calls);}
    {Model m;NativeMetal540Render client(m.transport());assert(client.run(true)==NativeMetal540Render::Mismatch);assert(!client.result().readbackVerified&&m.uploadUntouched&&m.submits==1&&m.waits==1);}
    {Model m;m.render=true;NativeMetal540Render client(m.transport());assert(client.run(true)==0);assert(client.result().readbackVerified&&client.result().pixels.center==Center&&client.result().pixels.corner==Background&&m.readbackOrdered&&m.uploadUntouched);assert(m.allocated==5&&m.submits==1&&m.waits==1);assert(m.bos[0].iova==0x40100000&&m.bos[4].iova==0x40108000);for(unsigned i=0;i<5;++i)assert(m.bos[i].perms==(i<3?1u:3u));unsigned calls=m.calls;assert(client.run(true)==NativeMetal540Render::AlreadyAttempted&&m.calls==calls);
        m.bos[3].memory[32*64+32]=Background;Pixels p;assert(!verify(m.bos[3].memory.data(),TargetBytes,p));assert(!verify(m.bos[3].memory.data(),TargetBytes-4,p));}
    for(unsigned point=1;point<=19;++point){Model m;m.failAt=point;NativeMetal540Render client(m.transport());assert(client.run(true)<0);assert(!client.result().readbackVerified);unsigned c=m.calls;assert(client.run(true)==NativeMetal540Render::AlreadyAttempted&&m.calls==c);}
    for(unsigned point=1;point<=5;++point){Model m;m.failView=point;NativeMetal540Render client(m.transport());assert(client.run(true)==-78);assert(!m.submits&&!client.result().readbackVerified);}
    std::cout<<"host render: actual fixture/PM4/bounds/oracle/19 failures PASS; 5 CPU map failures PASS; software model only, NOT GPU execution\n";
}
