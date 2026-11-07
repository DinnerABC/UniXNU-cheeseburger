// SPDX-License-Identifier: MIT
#include "NativeMetal540DesktopDraw.h"
#include "NativeMetal540DesktopLibrary.h"
#include "AIRNIR.h"
#include "LibraryArchive.h"
#include <CommonCrypto/CommonDigest.h>
#include <memory>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <map>
struct Model {uint64_t next=0;std::map<uint64_t,std::vector<uint32_t>> buffers;};
static int call(void *p,uint32_t sel,const void *in,size_t,void *out,size_t *n){auto &m=*static_cast<Model *>(p);
    if(sel==NativeAdrenoMethodQuery){*static_cast<NativeAdrenoAbiParams *>(out)={1,36,36,1,1ull<<28};*n=sizeof(NativeAdrenoAbiParams);return 0;}
    if(sel==NativeAdrenoMethodGetDeviceInfo){NativeAdrenoAbiDeviceInfo d{};d.chip_id=0x05040001;d.flags=1;d.va_start=0x40000000;d.va_size=1ull<<28;*static_cast<NativeAdrenoAbiDeviceInfo *>(out)=d;*n=sizeof d;return 0;}
    if(sel==NativeAdrenoMethodBoAlloc){auto b=static_cast<const NativeAdrenoAbiBoAlloc *>(in);uint64_t h=++m.next;m.buffers[h].resize(b->bytes/4);*static_cast<NativeAdrenoAbiBoAllocReply *>(out)={h};*n=sizeof(NativeAdrenoAbiBoAllocReply);return 0;}
    if(sel==NativeAdrenoMethodBoMap){*static_cast<NativeAdrenoAbiBoMapReply *>(out)={static_cast<const NativeAdrenoAbiBoMap *>(in)->iova};*n=sizeof(NativeAdrenoAbiBoMapReply);return 0;}
    if(sel==NativeAdrenoMethodBoCpuMap){auto h=static_cast<const NativeAdrenoAbiBoCpuMap *>(in)->handle;*static_cast<NativeAdrenoAbiBoCpuMapReply *>(out)={uint32_t(h),0,m.buffers[h].size()*4};*n=sizeof(NativeAdrenoAbiBoCpuMapReply);return 0;}
    assert(false);return -1;
}
static std::unique_ptr<QGPUProgramImage> compile(const std::vector<uint8_t> &bytes,const char *name){
    std::vector<GPU::Compiler::LibraryArchive::Function> functions;std::string error;
    assert(GPU::Compiler::LibraryArchive::parse(bytes.data(),bytes.size(),functions,error));
    auto out=std::make_unique<QGPUProgramImage>();bool found=false;
    for(const auto &f:functions)if(f.name==name){assert(!found);found=true;unsigned char hash[32];CC_SHA256(bytes.data()+f.offset,CC_LONG(f.size),hash);assert(!memcmp(hash,f.hash.data(),32));
        QGPUAIRNIRTiming timing{};char text[4096]{};assert(qgpu_compile_air_nir(bytes.data()+f.offset,f.size,name,nullptr,0,0,out.get(),&timing,text,sizeof text));}
    assert(found&&GPU::JIT::valid(*out,0));return out;
}
int main(int argc,char **argv){assert(argc==2);std::ifstream f(argv[1],std::ios::binary|std::ios::ate);assert(f);std::vector<uint8_t> bytes(size_t(f.tellg()));f.seekg(0);f.read(reinterpret_cast<char *>(bytes.data()),bytes.size());assert(f);
    // Real original QuartzCore AIR, not a hand-written GLSL approximation.
    auto vs=compile(bytes,"image_rect_blit_vert_lpf"),fs=compile(bytes,"image_rect_blit_frag_lpf");
    std::string error;auto library=NativeMetal540DesktopLibrary::load(bytes.data(),bytes.size(),qgpu_compile_air_nir,0,error);
    assert(library&&error.empty()&&library->names().size()>100&&library->identity().size()==64);
    auto owned=library->function("image_rect_blit_frag_lpf",error);assert(owned&&error.empty());
    assert(library->function("image_rect_blit_frag_lpf",error)==owned);
    assert(!library->function("unknown-desktop-entry",error)&&!error.empty());
    auto corrupted=bytes;std::vector<GPU::Compiler::LibraryArchive::Function> entries;assert(GPU::Compiler::LibraryArchive::parse(bytes.data(),bytes.size(),entries,error));corrupted[entries[0].offset]^=1;
    assert(!NativeMetal540DesktopLibrary::load(corrupted.data(),corrupted.size(),qgpu_compile_air_nir,0,error)&&!error.empty());
    Model m;NativeMetal540DesktopSession::Transport t{};t.call=call;t.context=&m;
    t.mapView=[](void *p,uint32_t h,void **cpu,size_t *bytes){auto &b=static_cast<Model *>(p)->buffers[h];*cpu=b.data();*bytes=b.size()*4;return 0;};
    t.unmapView=[](void *,uint32_t,void *,size_t){return 0;};t.flush=[](void *,void *,size_t,bool){return 0;};
    NativeMetal540DesktopSession s(t);assert(!s.initialize());using Resource=NativeMetal540DesktopSession::Resource;Resource ib{},scratch{},target{},vertex{};
    assert(!s.allocate(65536,ib)&&!s.allocate(4096,scratch)&&!s.allocate(8294400,target)&&!s.allocate(4096,vertex));
    NativeMetal540Desktop::Draw d{};auto &state=d.state;state.version=GPU::Shader::Version;state.primitive=3;state.count=3;state.instances=1;
    state.target={1,0,1080,1920,4352,GPU::Shader::BGRA8};
    // Allocate real padded row extent; reject the too-small1080*1920*4 backing.
    uint32_t words=0;assert(NativeMetal540Desktop::encodeDraw(s,ib.id,scratch.id,*vs,*fs,d,words));
    Resource color{};assert(!s.allocate(4352ull*1920,target)&&!s.allocate(4352ull*1920,color));
    state.scissor[2]=1080;state.scissor[3]=1920;state.state=GPU::Shader::defaultRenderState(1080,1920);
    state.state.blend=1;state.state.destinationRGB=state.state.destinationAlpha=GPU::Shader::OneMinusSourceAlpha;
    state.depthStencil=GPU::Shader::defaultDepthStencilState();d.target=target.id;
    state.colorTargets[0]=state.target;state.colorTargets[0].resource=2;d.colors[0]=color.id;
    state.colorStates[0]=GPU::Shader::copyColorState(state.state);
    for(unsigned i=0;i<QGPU_STAGE_BUFFER_COUNT;++i){if(vs->info.bufferMask&(1u<<i)){state.buffers[i]={uint32_t(i+3),0,4096,0};d.buffers[i]={vertex.id,0,4096};}
        if(fs->info.bufferMask&(1u<<i)){state.fragmentBuffers[i]={uint32_t(i+40),0,4096,0};d.buffers[QGPU_STAGE_BUFFER_COUNT+i]={vertex.id,0,4096};}}
    state.textures[3]={7,0,64,64,256,GPU::Shader::RGBA8};d.fragmentTextures[3]=vertex.id;
    Resource sampled{};assert(!s.allocate(16384,sampled));d.fragmentTextures[3]=sampled.id;
    state.samplers[0].filters=3; // linear min/mag, clamp, normalized coordinates
    GPU::Shader::Program pv{},pf{};memcpy(&pv,&vs->info,sizeof vs->info);memcpy(&pf,&fs->info,sizeof fs->info);pv.code=vs->code;pv.immediates=vs->immediates;pf.code=fs->code;pf.immediates=fs->immediates;
    GPU::Shader::Linkage link{};uint32_t width=0,height=0,vertexBindings=0;
    std::fprintf(stderr,"gates interface=%u/%u draw=%u link=%u attach=%u layout=%u tex=%u/%u window=%u/%u\n",unsigned(GPU::Shader::validInterface(pv)),unsigned(GPU::Shader::validInterface(pf)),unsigned(GPU::Shader::validDraw(state)),unsigned(GPU::Shader::link(pv,pf,link)),unsigned(GPU::Shader::validAttachments(state,width,height)),unsigned(GPU::Shader::vertexLayout(pv,state,vertexBindings)),unsigned(GPU::Shader::graphicsTextures(pv,state.vertexTextures,state.vertexSamplers)),unsigned(GPU::Shader::graphicsTextures(pf,state.textures,state.samplers)),pv.constantWindowCount,pf.constantWindowCount);
    int rc=NativeMetal540Desktop::encodeDraw(s,ib.id,scratch.id,*vs,*fs,d,words);
    if(rc)std::fprintf(stderr,"encode rc=%d vsbuf=%x fsbuf=%x colors=%x words=%u\n",rc,vs->info.bufferMask,fs->info.bufferMask,fs->info.colorMask,words);
    assert(!rc&&words>1000);
    auto good=d;d.fragmentTextures[3]=target.id;assert(NativeMetal540Desktop::encodeDraw(s,ib.id,scratch.id,*vs,*fs,d,words));d=good;
    d.colors[0]=target.id;assert(NativeMetal540Desktop::encodeDraw(s,ib.id,scratch.id,*vs,*fs,d,words));d=good;
    d.buffers[1].bytes=8192;assert(NativeMetal540Desktop::encodeDraw(s,ib.id,scratch.id,*vs,*fs,d,words));
    std::puts("PASS original QuartzCore AIR image sampling/premultiplied blend/MRT full-HD PM4 and extent/alias/binding refusal; no device execution");
}
