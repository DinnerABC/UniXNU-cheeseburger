// SPDX-License-Identifier: GPL-2.0-only
// Host policy/stream model only. NEVER GPU execution or CPU output oracle.
#include "NativeMetal540Render.h"
#include "render_air_fixture.h"
#include "render_fixture.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
using namespace Native540Render;
static Resources resources(){
    Resources r{};r.ib={0x40100000,8192};r.vs={0x40102000,4096};r.fs={0x40103000,4096};
    r.target={0x40104000,16384};r.scratch={0x40108000,4096};
    r.vertexBindings[0]={{0x40109000,4096},0,48,true};r.fragmentBindings[0]={{0x4010a000,4096},0,16,true};return r;
}
static unsigned failures=0;
static void rejected(const AIRProgram &vs,const AIRProgram &fs,const Resources &r){
    uint32_t buffer[2048];for(auto &v:buffer)v=0x13579bdf;uint32_t written=77;
    assert(!encodeAIR(vs,fs,r,buffer,2048,written)&&written==0);
    for(auto v:buffer)assert(v==0x13579bdf);++failures;
}
int main(int argc,char **argv){
    assert(argc==3);const auto vs=Native540RenderAIRFixture::vs,fs=Native540RenderAIRFixture::fs;auto r=resources();Plan q{};
    assert(planAIR(vs,fs,r,q));assert(q.position==8&&q.stride==4);
    for(unsigned i=0;i<512;++i){uint32_t v=0,f=0;if(i==96)v=f=48;if(i==104)v=f=16;
        if(i>=132&&i<135)v=vs.program.immediates[i-132];if(i>=132&&i<136)f=fs.program.immediates[i-132];
        assert(q.vsConstants[i]==v&&q.fsConstants[i]==f);}
    uint32_t words[2048]={},written=0;assert(encodeAIR(vs,fs,r,words,2048,written));
    std::ofstream out(argv[1],std::ios::binary);out.write(reinterpret_cast<char*>(words),written*4);out.close();assert(out);
    for(unsigned capacity=0;capacity<written;++capacity){uint32_t n=99;std::vector<uint32_t>b(capacity+2,0x13579bdf);
        assert(!encodeAIR(vs,fs,r,b.data()+1,capacity,n)&&n==0);for(auto v:b)assert(v==0x13579bdf);++failures;}
    uint32_t n=44;assert(!encodeAIR(vs,fs,r,nullptr,2048,n)&&n==0);
    for(unsigned stage=0;stage<2;++stage){
        for(unsigned i=0;i<621;++i){auto a=vs,b=fs;uint32_t reflection[621];const auto &original=stage?fs:vs;
            std::memcpy(reflection,original.reflection,sizeof(reflection));reflection[i]^=1;
            (stage?b:a).reflection=reflection;rejected(a,b,r);}
        for(unsigned i=0;i<(stage?fs:vs).program.info.codeWords;++i){auto a=vs,b=fs;uint32_t code[160]={};
            const auto &original=stage?fs:vs;std::memcpy(code,original.program.code,original.program.info.codeWords*4);code[i]^=1;
            (stage?b:a).program.code=code;rejected(a,b,r);}
        for(unsigned i=0;i<sizeof(Info)/4;++i){auto a=vs,b=fs;auto &info=(stage?b:a).program.info;
            unsigned char *bytes=reinterpret_cast<unsigned char*>(&info);bytes[i*4]^=1;rejected(a,b,r);}
        auto a=vs,b=fs;(stage?b:a).reflectionWords=620;rejected(a,b,r);
        a=vs;b=fs;(stage?b:a).reflection=nullptr;rejected(a,b,r);
        a=vs;b=fs;(stage?b:a).program.code=nullptr;rejected(a,b,r);
        a=vs;b=fs;(stage?b:a).program.immediates=nullptr;rejected(a,b,r);
    }
    for(unsigned stage=0;stage<2;++stage){auto a=vs,b=fs;uint32_t immediates[4]={};const auto &original=stage?fs:vs;
        std::memcpy(immediates,original.program.immediates,original.program.info.immediateCount*4);immediates[0]^=1;
        (stage?b:a).program.immediates=immediates;rejected(a,b,r);}
    for(unsigned change=0;change<15;++change){auto bad=r;auto &binding=bad.vertexBindings[0];
        switch(change){case 0:binding.logicalBytes=47;break;case 1:binding.logicalBytes=49;break;
        case 2:binding.readOnly=false;break;case 3:binding.offset=16;break;case 4:binding.allocation.address=0;break;
        case 5:binding.allocation.address++;break;case 6:binding.allocation.bytes=48;break;
        case 7:binding.allocation.bytes=8192;break;case 8:binding.allocation.address=AddressLimit;break;
        case 9:binding.allocation.address=UINT64_MAX-4095;break;case 10:binding.allocation=r.target;break;
        case 11:bad.fragmentBindings[0].allocation=binding.allocation;break;case 12:bad.vertexBindings[1]=binding;break;
        case 13:bad.fragmentBindings[0].logicalBytes=15;break;case 14:bad.fragmentBindings[0].readOnly=false;break;}
        rejected(vs,fs,bad);}
    // Old zero-extension fixture remains byte-identical and cannot accept AIR.
    r.vertexBindings[0]={};r.fragmentBindings[0]={};assert(encode(Native540RenderFixture::vs,Native540RenderFixture::fs,r,words,2048,written));
    std::ofstream old(argv[2],std::ios::binary);old.write(reinterpret_cast<char*>(words),written*4);old.close();assert(old);
    assert(!encode(vs.program,fs.program,r,words,2048,written));
    std::cout<<failures<<" failures PASS; exact raw621/ISA/bounds/capacity; NOT GPU execution\n";
}
