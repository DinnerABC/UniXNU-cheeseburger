// SPDX-License-Identifier: MIT
// Standalone offline GLSL -> NIR -> A540 IR3 probe. Never AIR/Metal or device work.
#include "Compiler.h"
#include <CommonCrypto/CommonDigest.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
static std::string sha(const void *data, size_t bytes) {
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data, static_cast<CC_LONG>(bytes), hash);
    std::ostringstream s; s << std::hex << std::setfill('0');
    for (auto c : hash) s << std::setw(2) << unsigned(c);
    return s.str();
}
static void description(std::ostream &o, const QGPUProgramDescription &p) {
    o << '{';
#define FIELD(n) o << "\"" #n "\":" << p.n << ','
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
    static_assert(sizeof(p) == 621 * 4, "pinned reflection size");
    uint32_t words[621]; std::memcpy(words, &p, sizeof(p));
    o << "\"descriptionWords\":[";
    for (unsigned i=0;i<621;++i) { if(i) o << ','; o << words[i]; }
    o << "]}";
}
int main(int argc, char **argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: native540-compile-probe INPUT.comp NEW_OUTPUT_DIRECTORY");
        std::ifstream in(argv[1], std::ios::binary | std::ios::ate);
        if (!in) throw std::runtime_error("input unreadable");
        const auto length=in.tellg();
        if (length<=0 || length>65536) throw std::runtime_error("GLSL size out of bounds");
        std::string source(static_cast<size_t>(length), '\0'); in.seekg(0); in.read(source.data(),length);
        if (!in || source.find('\0')!=std::string::npos) throw std::runtime_error("invalid GLSL read/NUL");
        const std::string admittedGlobal="#version 430\nlayout(local_size_x=64) in;\nlayout(std430,binding=0) buffer B { uint v[]; };\nvoid main() { v[gl_GlobalInvocationID.x] = 0xdeadbeefu; }\n";
        const std::string admittedLocal="#version 430\nlayout(local_size_x=64) in;\nlayout(std430,binding=0) buffer B { uint v[]; };\nvoid main() { v[gl_LocalInvocationID.x] = 0xdeadbeefu; }\n";
        if(source!=admittedGlobal && source!=admittedLocal)
            throw std::runtime_error("only the two exact pinned GLSL430 probes are admitted; no source rewriting");
        auto image=std::make_unique<QGPUProgramImage>();
        // qgpu_compile_glsl owns reflection/code; these are only JIT envelope fields.
        image->version=GPU::JIT::Version; image->gpu=540; std::strcpy(image->name,"Probe540");
        char diagnostic[4096]{};
        if (!qgpu_compile_glsl(source.c_str(),0,image.get(),diagnostic,sizeof(diagnostic)))
            throw std::runtime_error(diagnostic[0]?diagnostic:"GLSL compiler failed");
        // The fixed backend's standalone frontend supports 430, not 450.
        // This deliberately finite GLSL frontend admits ONLY the declared
        // uint[] SSBO0 store below, then supplies its actual source reflection
        // exactly as sagit Compiler.cpp::compileEntries does at lines 53/77.
        // Backend allocation, flags, sysvals, constants and ISA are never edited.
        const auto backendInfo=image->info;
        if(backendInfo.bufferMask || backendInfo.writeMask || backendInfo.strides[0])
            throw std::runtime_error("backend reflection contract changed; do not overwrite compiler fields");
        image->info.bufferMask=1; image->info.writeMask=1; image->info.strides[0]=sizeof(uint32_t);
        // compileEntries line71 discards undeclared varying tails. The exact
        // compute source declares no varyings; retain backendInfo unchanged.
        for(auto &varying:image->info.varyings) varying={};
        if (!GPU::JIT::valid(*image,0)) {
            description(std::cerr,image->info); std::cerr << '\n';
            throw std::runtime_error("actual GLSL output rejected by QGPUJIT::valid");
        }
        if (image->info.stage!=0 || image->info.bufferMask!=1 || image->info.writeMask!=1 || image->info.strides[0]!=4)
            throw std::runtime_error("actual reflection is not SSBO0 uint write stride4; no metadata repair allowed");
        const std::filesystem::path dir(argv[2]);
        if (!std::filesystem::create_directory(dir)) throw std::runtime_error("refusing to replace output evidence");
        std::ofstream code(dir/"probe.ir3", std::ios::binary), glsl(dir/"probe.comp", std::ios::binary), manifest(dir/"manifest.json");
        std::ofstream backend(dir/"backend-reflection.bin", std::ios::binary), final(dir/"final-reflection.bin", std::ios::binary), jit(dir/"probe.jit", std::ios::binary);
        backend.write(reinterpret_cast<const char *>(&backendInfo),sizeof(backendInfo));
        final.write(reinterpret_cast<const char *>(&image->info),sizeof(image->info));
        jit.write(reinterpret_cast<const char *>(image.get()),sizeof(*image));
        backend.close(); final.close(); jit.close();
        if(!backend||!final||!jit)throw std::runtime_error("reflection/JIT write failure");
        code.write(reinterpret_cast<const char *>(image->code),image->info.codeWords*4);
        glsl.write(source.data(),source.size());
        manifest << "{\"finalJIT\":{\"file\":\"probe.jit\",\"sha256\":\"" << sha(image.get(),sizeof(*image))
          << "\",\"bytes\":" << sizeof(*image) << ",\"valid\":true},\"schema\":1,\"gpu\":540,\"jitVersion\":" << GPU::JIT::Version
          << ",\"producer\":\"qgpu_compile_glsl\",\"route\":\"GLSL-NIR-A540-IR3\",\"entry\":\"Probe540\","
          "\"jitValid\":true,\"metalCompiler\":false,\"runtimeVerified\":false,\"input\":{\"sha256\":\"" << sha(source.data(),source.size())
          << "\",\"shader\":\"probe.comp\"},\"dispatch\":{\"groups\":[1,1,1],\"local\":[64,1,1]},"
          "\"outputBytes\":256,\"expected\":3735928559,\"initial\":324508639,\"variants\":[{\"name\":\"base\",\"shader\":\"probe.ir3\",\"sha256\":\""
          << sha(image->code,image->info.codeWords*4) << "\",\"info\":";
        description(manifest,image->info); manifest << ",\"immediates\":[";
        for(unsigned i=0;i<image->info.immediateCount;++i) { if(i)manifest<<',';manifest<<image->immediates[i]; }
        manifest << "]}],\"backendReflection\":"; description(manifest,backendInfo);
        manifest << ",\"reflectionComposition\":\"exact admitted GLSL declaration/store + unmodified backend; sagit Compiler.cpp::compileEntries\",\"requestedGLSLVersion\":450,\"compiledGLSLVersion\":430}\n"; code.close();glsl.close();manifest.close();
        if(!code||!glsl||!manifest)throw std::runtime_error("output write failure");
        std::cout << "QGPUJIT valid: actual A540 GLSL output, codeWords=" << image->info.codeWords << " flags=" << image->info.flags << '\n';
        return 0;
    } catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
