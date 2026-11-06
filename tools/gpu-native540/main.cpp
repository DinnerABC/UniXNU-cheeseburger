// SPDX-License-Identifier: MIT
// Offline CLI only. No Metal device factory, IOKit open or GPU submission.
#include "AIRNIR.h"
#include "LibraryArchive.h"
#include <CommonCrypto/CommonDigest.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <iomanip>
static std::string quote(const std::string &s) {
    std::ostringstream o; o << '"';
    for (unsigned char c:s) { if(c=='"'||c=='\\') o << '\\' << c;
        else if(c<32) o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else o << c; }
    o << '"'; return o.str();
}
static void description(std::ostream &o,const QGPUProgramDescription &p) {
    o << '{'; bool first=true;
    auto scalar=[&](const char *n,uint32_t v){if(!first)o<<',';first=false;o<<quote(n)<<':'<<v;};
#define FIELD(n) scalar(#n,p.n)
    FIELD(id); FIELD(stage); FIELD(codeWords); FIELD(instrlen); FIELD(constlen); FIELD(fullRegs); FIELD(halfRegs);
    FIELD(branchStack); FIELD(threadSize); FIELD(subgroupSize); FIELD(immediateBase); FIELD(immediateCount);
    FIELD(driverBase); FIELD(driverCount); FIELD(localID); FIELD(groupID); FIELD(vertexID); FIELD(position);
    FIELD(color); FIELD(fragCoord); FIELD(fragMask); FIELD(bufferMask); FIELD(writeMask); FIELD(textureCount); FIELD(flags);
    FIELD(instanceID); FIELD(instanceMask); FIELD(varyingCount); FIELD(totalIn); FIELD(perspective); FIELD(linear);
    FIELD(centerRHW); FIELD(sharedBytes); FIELD(threadgroupMask); FIELD(imageMask); FIELD(imageReadMask); FIELD(imageWriteMask);
    FIELD(imageUintMask); FIELD(sampleMask); FIELD(samplerMask); FIELD(explicitSamplerMask); FIELD(cubeSamplerMask);
    FIELD(fetchMask); FIELD(queryMask); FIELD(attributeMask); FIELD(implicitSamplerMask); FIELD(constantSamplerMask);
    FIELD(constantImplicitSamplerMask); FIELD(constantExplicitSamplerMask); FIELD(constantCubeSamplerMask); FIELD(framebufferMask);
    FIELD(colorMask); FIELD(depthReg); FIELD(multisampleMask); FIELD(depthTextureMask); FIELD(volumeMask);
    FIELD(imageblockMask); FIELD(imageblockBindings); FIELD(imageblockSampleBytes); FIELD(sampleFlags); FIELD(sampleID);
    FIELD(sampleMaskIn); FIELD(sampleMaskOut); FIELD(perspectiveCentroid); FIELD(linearCentroid); FIELD(perspectiveSample);
    FIELD(linearSample); FIELD(textureBindings); FIELD(constantWindowCount);
#undef FIELD
    // The complete pointer-free description is also serialized losslessly.
    // Schema and source hashes pin the words; consumers must not infer offsets
    // from another sagit revision or treat it as NativeAdreno wire ABI.
    o << ",\"descriptionWords\":[";
    const auto *w=reinterpret_cast<const uint32_t *>(&p);
    for(size_t i=0;i<sizeof(p)/4;++i){if(i)o<<',';o<<w[i];}o<<"]}";
}
int main(int argc,char **argv) {
    try {
        if(argc!=3 && argc!=4)throw std::runtime_error("usage: native540-compiler --list INPUT | INPUT ENTRY OUTPUT_PREFIX");
        const bool list=std::string(argv[1])=="--list";
        std::ifstream in(list?argv[2]:argv[1],std::ios::binary|std::ios::ate);
        if(!in)throw std::runtime_error("input not readable");
        auto size=in.tellg(); if(size<=0||size>64*1024*1024)throw std::runtime_error("input size exceeds bound");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));in.seekg(0);in.read(reinterpret_cast<char *>(bytes.data()),bytes.size());
        if(!in)throw std::runtime_error("short input read");
        std::vector<GPU::Compiler::LibraryArchive::Function> functions;std::string error;
        if(!GPU::Compiler::LibraryArchive::parse(bytes.data(),bytes.size(),functions,error))throw std::runtime_error(error);
        if(list){std::cout<<'[';bool first=true;for(const auto &f:functions){if(!first)std::cout<<',';first=false;
            std::cout<<"{\"name\":"<<quote(f.name)<<",\"metalType\":"<<f.type<<",\"offset\":"<<f.offset<<",\"bytes\":"<<f.size<<'}';}std::cout<<"]\n";return 0;}
        if(argc!=4)throw std::runtime_error("compile requires entry and output prefix");
        const GPU::Compiler::LibraryArchive::Function *chosen=nullptr;
        for(const auto &f:functions)if(f.name==argv[2]){if(chosen)throw std::runtime_error("ambiguous entry");chosen=&f;}
        if(!chosen)throw std::runtime_error("entry absent");
        unsigned char hash[CC_SHA256_DIGEST_LENGTH];CC_SHA256(bytes.data()+chosen->offset,static_cast<CC_LONG>(chosen->size),hash);
        if(memcmp(hash,chosen->hash.data(),sizeof(hash)))throw std::runtime_error("AIR archive hash mismatch");
        auto image=std::make_unique<QGPUProgramImage>();QGPUAIRNIRTiming timing{};char diagnostic[4096]{};
        if(!qgpu_compile_air_nir(bytes.data()+chosen->offset,chosen->size,argv[2],nullptr,0,0,image.get(),&timing,diagnostic,sizeof(diagnostic)))
            throw std::runtime_error(diagnostic);
        if(!GPU::JIT::valid(*image,0))throw std::runtime_error("compiler output validation failed");
        const unsigned expected=chosen->type==2?0:chosen->type==0?1:2;
        if(image->info.stage!=expected)throw std::runtime_error("archive stage mismatch");
        std::ofstream manifest(std::string(argv[3])+".json");if(!manifest)throw std::runtime_error("output not writable");
        manifest<<"{\"schema\":1,\"gpu\":540,\"jitVersion\":"<<GPU::JIT::Version<<",\"route\":\"AIR-NIR-IR3\",\"entry\":"<<quote(argv[2])
          <<",\"runtimeVerified\":false,\"variants\":[";
        const char *names[]={"base","msaa","constantWindow","msaaConstantWindow"};
        const QGPUProgramDescription *infos[]={&image->info,&image->msaa.info,&image->constantWindow.info,&image->msaaConstantWindow.info};
        const uint32_t *codes[]={image->code,image->msaa.code,image->constantWindow.code,image->msaaConstantWindow.code};
        const uint32_t *imms[]={image->immediates,image->msaa.immediates,image->constantWindow.immediates,image->msaaConstantWindow.immediates};
        bool first=true;
        for(unsigned i=0;i<4;++i){const auto &p=*infos[i];if(!p.codeWords)continue;if(!first)manifest<<',';first=false;
            std::string path=std::string(argv[3])+"."+names[i]+".ir3";std::ofstream code(path,std::ios::binary);
            code.write(reinterpret_cast<const char *>(codes[i]),p.codeWords*4);code.close();if(!code)throw std::runtime_error("shader write failed");
            manifest<<"{\"name\":"<<quote(names[i])<<",\"info\":";description(manifest,p);manifest<<",\"immediates\":[";
            for(unsigned j=0;j<p.immediateCount;++j){if(j)manifest<<',';manifest<<imms[i][j];}manifest<<"]}";}
        manifest<<"],\"lowerNS\":"<<timing.lowerNS<<",\"backendNS\":"<<timing.backendNS<<"}\n";
        manifest.close();if(!manifest)throw std::runtime_error("manifest write failed");return 0;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
