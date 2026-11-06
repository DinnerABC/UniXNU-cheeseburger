// SPDX-License-Identifier: MIT
// Executes the pinned ORIGINAL full draw encoder as an independent PM4 oracle.
#include <A5xxRegs.h>
// Header vocabulary aliases only: existing board regdefs retain reg64 base
// names, pinned Mesa XML names low/high. No copied Apple or board addresses.
#define REG_A5XX_RB_BLIT_FLAG_DST_LO REG_A5XX_RB_BLIT_FLAG_DST
#define REG_A5XX_TPL1_TP_BORDER_COLOR_BASE_ADDR_LO REG_A5XX_TPL1_TP_BORDER_COLOR_BASE_ADDR
#define REG_A5XX_TPL1_TP_BORDER_COLOR_BASE_ADDR_HI (REG_A5XX_TPL1_TP_BORDER_COLOR_BASE_ADDR+1)
#define REG_A5XX_VFD_FETCH_BASE_LO(n) REG_A5XX_VFD_FETCH_BASE(n)
#define REG_A5XX_VFD_FETCH_BASE_HI(n) (REG_A5XX_VFD_FETCH_BASE(n)+1)
#define REG_A5XX_RB_DEPTH_BUFFER_BASE_LO REG_A5XX_RB_DEPTH_BUFFER_BASE
#define REG_A5XX_RB_DEPTH_BUFFER_BASE_HI (REG_A5XX_RB_DEPTH_BUFFER_BASE+1)
#define REG_A5XX_RB_STENCIL_BASE_LO REG_A5XX_RB_STENCIL_BASE
#define REG_A5XX_RB_STENCIL_BASE_HI (REG_A5XX_RB_STENCIL_BASE+1)
#define REG_A5XX_RB_DEPTH_FLAG_BUFFER_BASE_LO REG_A5XX_RB_DEPTH_FLAG_BUFFER_BASE
#define REG_A5XX_GRAS_LRZ_BUFFER_BASE_LO REG_A5XX_GRAS_LRZ_BUFFER_BASE
#define REG_A5XX_SP_CS_OBJ_START_LO REG_A5XX_SP_CS_OBJ_START
#define REG_A5XX_SP_VS_OBJ_START_LO REG_A5XX_SP_VS_OBJ_START
#define REG_A5XX_SP_FS_OBJ_START_LO REG_A5XX_SP_FS_OBJ_START
#define REG_A5XX_RB_MRT_BASE_LO(n) REG_A5XX_RB_MRT_BASE(n)
// Pinned Mesa24.3.4 a5xx.xml RB_RENDER_CONTROL1/FS_OUTPUT_CNTL;
// these are unused (zero) for the admitted single-sample constant-color draw.
#define A5XX_RB_RENDER_CONTROL1_CENTERRHW (1u<<5)
#define A5XX_RB_RENDER_CONTROL1_SAMPLEMODE(v) (((v)&3u)<<3)
#define A5XX_RB_FS_OUTPUT_CNTL_FRAG_WRITES_SAMPMASK (1u<<6)
#include "AdrenoPipeline.h"
#include <fstream>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    auto vs=std::make_unique<GPU::Shader::Program>(),fs=std::make_unique<GPU::Shader::Program>();
    static_assert(offsetof(GPU::Shader::Program,constantWindows)+sizeof(vs->constantWindows)==sizeof(QGPUProgramDescription),"same pinned CPU fields, before pointer-alignment padding");
    auto load=[](const char *path,GPU::Shader::Program &p){std::ifstream f(path,std::ios::binary);auto image=std::make_unique<QGPUProgramImage>();f.read(reinterpret_cast<char *>(image.get()),sizeof(*image));if(!f)throw std::runtime_error("jit read");std::memcpy(&p,&image->info,sizeof(image->info));p.code=image->code;p.immediates=image->immediates;return image;};
    auto vi=load(argv[1],*vs),fi=load(argv[2],*fs);
    GPU::Shader::Draw d{};d.version=GPU::Shader::Version;d.primitive=3;d.count=3;d.instances=1;
    d.target={1,0,64,64,256,GPU::Shader::RGBA8};d.scissor[2]=d.scissor[3]=64;d.state=GPU::Shader::defaultRenderState(64,64);d.depthStencil=GPU::Shader::defaultDepthStencilState();
    GPU::Shader::Linkage linkage{};if(!GPU::Shader::link(*vs,*fs,linkage)||linkage.count!=1||linkage.stride!=4)return 3;
    uint32_t words[16384]={};Adreno::Encoder e(words,16384);std::vector<uint64_t> storage((sizeof(Adreno::PipelineEncoder)+7)/8);auto *encoder=new(storage.data()) Adreno::PipelineEncoder(e,0x40100000,0x40108000,4096);
    Adreno::ShaderBuffers bindings{};Adreno::GraphicsTextures sampled{};
    auto rc=encoder->draw(d,*vs,*fs,bindings,0x40104000,sampled);if(rc!=GPU::Result::Success){std::cerr<<"upstream draw rc="<<unsigned(rc)<<'\n';return 4;}
    std::ofstream out(argv[3],std::ios::binary);out.write(reinterpret_cast<const char *>(words),e.size()*4);std::cout<<"original pinned draw words="<<e.size()<<" linkCount="<<linkage.count<<" stride="<<linkage.stride<<'\n';return out?0:5;
}
