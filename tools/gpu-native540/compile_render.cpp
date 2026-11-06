// SPDX-License-Identifier: MIT
// Actual fixed qgpu_compile_glsl -> Mesa24.3.4 A540 backend; no Metal claim.
#include "Compiler.h"
#include "render_fixture_source.h"
#include <CommonCrypto/CommonDigest.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <cstring>
static constexpr char sagit[]="673f85070fe9f7872793015eb230a5c3987d4a8c";
static constexpr char mesa[]="769e51468b49b2a42f0a0eaf71cf9eed5ff4e5de";
static std::string sha(const void *p,size_t n) {
    unsigned char h[32];CC_SHA256(p,static_cast<CC_LONG>(n),h);
    std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto b:h)s<<std::setw(2)<<unsigned(b);return s.str();
}
static void blob(const std::filesystem::path &p,const void *v,size_t n) {
    std::ofstream f(p,std::ios::binary);f.write(static_cast<const char *>(v),n);f.close();if(!f)throw std::runtime_error("write failed");
}
static void array(std::ostream &o,const uint32_t *p,size_t n) {
    o<<'{';for(size_t i=0;i<n;++i){if(i)o<<',';o<<"0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<p[i]<<'u';}o<<'}'<<std::dec;
}
static void description(std::ostream &o,const QGPUProgramDescription &p) {
    static_assert(sizeof(p)==621*4,"fixed full metadata");uint32_t w[621];std::memcpy(w,&p,sizeof(w));
    o<<'[';for(unsigned i=0;i<621;++i){if(i)o<<',';o<<w[i];}o<<']';
}
static void compose(QGPUProgramDescription &p,unsigned stage) {
    // Exact-source declaration map: VS only built-in gl_Position output,
    // FS one location0 color output; neither has user varyings/SSBOs/instances.
    // Compiler.cpp::compileEntries lines53-77 composes these source-owned
    // fields; output/sysval IDs/color masks/driver constants stay backend-owned.
    if(p.stage!=stage || p.bufferMask || p.writeMask || p.instanceMask || p.textureBindings ||
       p.varyingCount || p.framebufferMask || p.depthTextureMask)throw std::runtime_error("source/backend interface disagreement");
    for(auto s:p.strides)if(s)throw std::runtime_error("unexpected source stride");
    p.bufferMask=0;p.writeMask=0;p.instanceMask=0;p.textureBindings=0;
    p.imageUintMask=0;p.depthTextureMask=0;p.varyingCount=0;
    for(auto &v:p.varyings)v={};for(auto &s:p.strides)s=0;
    // MSAA siblings retain their own sample/sysval metadata; only the base
    // variant is exported for this single-sample draw. Full JIT keeps siblings.
}
int main(int argc,char **argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: compile-render NEW_OUTPUT_DIRECTORY");
        const std::filesystem::path dir(argv[1]);if(!std::filesystem::create_directory(dir))throw std::runtime_error("refusing existing output");
        std::ofstream header(dir/"render_fixture.h"),manifest(dir/"manifest.json");
        header<<"// SPDX-License-Identifier: MIT\n#pragma once\n#include \"NativeMetal540Render.h\"\nnamespace Native540RenderFixture {\n";
        manifest<<"{\"gpu\":540,\"jitVersion\":"<<GPU::JIT::Version<<",\"sagit\":\""<<sagit<<"\",\"mesa\":\""<<mesa
          <<"\",\"producer\":\"qgpu_compile_glsl\",\"metalCompiler\":false,\"runtimeVerified\":false,\"composition\":\"Compiler.cpp compileEntries: exact zero user-stage declarations; backend owns position/color/sysval/constants\",\"variants\":[";
        for(unsigned stage=1;stage<=2;++stage) {
            const char *name=stage==1?"vs":"fs",*source=stage==1?Native540RenderSource::vertex:Native540RenderSource::fragment;
            auto image=std::make_unique<QGPUProgramImage>();image->version=GPU::JIT::Version;image->gpu=540;std::strcpy(image->name,name);
            char diag[4096]{};if(!qgpu_compile_glsl(source,stage,image.get(),diag,sizeof(diag)))throw std::runtime_error(diag);
            const auto raw=image->info;compose(image->info,stage);
            if(stage==2){compose(image->msaa.info,stage);if(image->constantWindow.info.codeWords)compose(image->constantWindow.info,stage);if(image->msaaConstantWindow.info.codeWords)compose(image->msaaConstantWindow.info,stage);}
            if(!GPU::JIT::valid(*image,0))throw std::runtime_error("JIT valid rejected actual image");
            const auto &p=image->info;
            if(p.textureCount || p.sharedBytes || p.threadgroupMask || p.imageMask || p.imageReadMask || p.imageWriteMask ||
               p.sampleMask || p.samplerMask || p.explicitSamplerMask || p.cubeSamplerMask || p.fetchMask || p.queryMask ||
               p.attributeMask || p.implicitSamplerMask || p.constantSamplerMask || p.constantWindowCount ||
               p.multisampleMask || p.volumeMask || p.imageblockMask || p.imageblockBindings || p.imageblockSampleBytes ||
               p.sampleFlags || p.totalIn || p.fragMask || p.flags!=(stage==1?8u:24u) ||
               p.localID!=252 || p.groupID!=252 || p.instanceID!=252 || p.fragCoord!=252 ||
               p.perspective!=252 || p.linear!=252 || p.centerRHW!=252 || p.depthReg ||
               (stage==1?(p.color!=252 || p.colorMask || p.vertexID>=252 || p.position>=252):
                         (p.vertexID!=252 || p.position!=252 || p.colorMask!=1 || p.color!=p.colorRegisters[0])))
                throw std::runtime_error("base variant exceeds exact single-sample VS/FS subset");
            blob(dir/(std::string(name)+".glsl"),source,std::strlen(source));blob(dir/(std::string(name)+".ir3"),image->code,p.codeWords*4);
            blob(dir/(std::string(name)+"-backend-reflection.bin"),&raw,sizeof(raw));blob(dir/(std::string(name)+"-final-reflection.bin"),&p,sizeof(p));blob(dir/(std::string(name)+".jit"),image.get(),sizeof(*image));
            header<<"inline constexpr uint32_t "<<name<<"Code[]=";array(header,image->code,p.codeWords);header<<";\n";
            const uint32_t zero=0;header<<"inline constexpr uint32_t "<<name<<"Immediates[]=";array(header,p.immediateCount?image->immediates:&zero,p.immediateCount?p.immediateCount:1);header<<";\n";
            uint32_t w[621];std::memcpy(w,&p,sizeof(w));header<<"inline constexpr uint32_t "<<name<<"Reflection[]=";array(header,w,621);header<<";\n";
            const uint32_t fields[]={p.stage,p.codeWords,p.instrlen,p.constlen,p.fullRegs,p.halfRegs,p.branchStack,p.threadSize,p.subgroupSize,p.immediateBase,p.immediateCount,p.driverBase,p.driverCount,p.vertexID,p.instanceID,p.position,p.color,p.colorMask,p.colorRegisters[0],p.fragCoord,p.perspective,p.linear,p.centerRHW,p.flags};
            header<<"inline constexpr Native540Render::Program "<<name<<"={";array(header,fields,24);header<<','<<name<<"Code,"<<name<<"Immediates};\n";
            header<<"inline constexpr char "<<name<<"CodeSHA256[]=\""<<sha(image->code,p.codeWords*4)<<"\";\n";
            if(stage==2)manifest<<',';
            manifest<<"{\"name\":\""<<name<<"\",\"sourceSHA256\":\""<<sha(source,std::strlen(source))<<"\",\"codeSHA256\":\""<<sha(image->code,p.codeWords*4)<<"\",\"jitSHA256\":\""<<sha(image.get(),sizeof(*image))<<"\",\"backendSHA256\":\""<<sha(&raw,sizeof(raw))<<"\",\"finalSHA256\":\""<<sha(&p,sizeof(p))<<"\",\"backendWords\":";description(manifest,raw);manifest<<",\"finalWords\":";description(manifest,p);manifest<<",\"immediates\":[";
            for(unsigned i=0;i<p.immediateCount;++i){if(i)manifest<<',';manifest<<image->immediates[i];}manifest<<"]}";
            std::cout<<name<<" words="<<p.codeWords<<" instrlen="<<p.instrlen<<" constlen="<<p.constlen<<" regs="<<p.fullRegs<<'/'<<p.halfRegs<<" driver="<<p.driverBase<<'+'<<p.driverCount<<" immediate="<<p.immediateBase<<'+'<<p.immediateCount<<" IDs="<<p.vertexID<<'/'<<p.position<<'/'<<p.color<<" flags="<<p.flags<<'\n';
        }
        header<<"}\n";manifest<<"]}\n";header.close();manifest.close();if(!header||!manifest)throw std::runtime_error("output write failed");return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
