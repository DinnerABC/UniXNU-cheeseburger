// SPDX-License-Identifier: MIT
#include "NativeMetal540DesktopDraw.h"
#include <A5xxRegs.h>
// Vocabulary aliases for the board's existing generated DWORD register names.
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
#define A5XX_RB_RENDER_CONTROL1_CENTERRHW (1u<<5)
#define A5XX_RB_RENDER_CONTROL1_SAMPLEMODE(v) (((v)&3u)<<3)
#define A5XX_RB_FS_OUTPUT_CNTL_FRAG_WRITES_SAMPMASK (1u<<6)
#include "AdrenoPipeline.h"
#include <memory>
#include <cstring>

namespace NativeMetal540Desktop {
int encodeDraw(NativeMetal540DesktopSession &session,uint64_t ibID,uint64_t scratchID,
    const QGPUProgramImage &vertex,const QGPUProgramImage &fragment,const Draw &draw,uint32_t &words) {
    words=0;
    if(session.lost()||!GPU::JIT::valid(vertex,vertex.registry)||!GPU::JIT::valid(fragment,vertex.registry)||
       vertex.info.stage!=GPU::Shader::Vertex||fragment.info.stage!=GPU::Shader::Fragment)return NativeMetal540DesktopSession::Invalid;
    // The first desktop path is linear single-sample2D. Do not silently fall
    // back for framebuffer fetch, depth, MSAA or shader writes.
    const auto &d=draw.state;
    if(d.version!=GPU::Shader::Version||d.rasterSamples>1||d.depth.resource||d.stencil.resource||d.indexSize||
       vertex.info.writeMask||fragment.info.writeMask||fragment.info.framebufferMask||
       vertex.info.constantWindowCount)return NativeMetal540DesktopSession::Invalid;
    NativeMetal540DesktopSession::Resource ib{},scratch{};
    if(!session.resource(ibID,ib)||!session.resource(scratchID,scratch)||ib.imported||scratch.imported||
       !ib.cpu||!scratch.cpu||ib.bytes<65536||scratch.bytes<4096||ib.id==scratch.id)return NativeMetal540DesktopSession::Invalid;
    auto vs=std::make_unique<GPU::Shader::Program>(),fs=std::make_unique<GPU::Shader::Program>();
    std::memcpy(vs.get(),&vertex.info,sizeof vertex.info);std::memcpy(fs.get(),&fragment.info,sizeof fragment.info);
    vs->code=vertex.code;vs->immediates=vertex.immediates;fs->code=fragment.code;fs->immediates=fragment.immediates;
    if(!GPU::Shader::validInterface(*vs)||!GPU::Shader::validInterface(*fs))return NativeMetal540DesktopSession::Invalid;
    Adreno::ShaderBuffers buffers{};Adreno::GraphicsTextures textures{};uint64_t colors[QGPU_COLOR_TARGET_COUNT]{};
    auto resolve=[&](uint64_t id,uint64_t offset,uint64_t bytes,uint64_t &address)->bool {
        if(!id){address=0;return !offset&&!bytes;}
        NativeMetal540DesktopSession::Resource r{};
        if(!session.resource(id,r)||id==ib.id||id==scratch.id||offset>r.bytes||bytes>r.bytes-offset)return false;
        address=r.iova+offset;return true;
    };
    for(unsigned i=0;i<QGPU_GRAPHICS_BUFFER_COUNT;++i){const auto &b=draw.buffers[i];
        const auto &binding=i<QGPU_STAGE_BUFFER_COUNT?d.buffers[i]:d.fragmentBuffers[i-QGPU_STAGE_BUFFER_COUNT];
        if(bool(binding.resource)!=bool(b.resource)||binding.offset!=b.offset||binding.bytes!=b.bytes||binding.reserved)return NativeMetal540DesktopSession::Invalid;
        if(!resolve(b.resource,b.offset,b.bytes,buffers.addresses[i]))return NativeMetal540DesktopSession::Invalid;
        buffers.bytes[i]=b.bytes;
    }
    uint32_t vertexBindings=0;
    if(!GPU::Shader::vertexFootprint(*vs,d,uint64_t(d.start)+d.count,vertexBindings))return NativeMetal540DesktopSession::Invalid;
    for(unsigned i=0;i<QGPU_COLOR_TARGET_COUNT;++i){const auto &t=GPU::Shader::colorTarget(d,i);uint64_t size=0;
        if(t.resource&&!GPU::Shader::textureBytes(t,size))return NativeMetal540DesktopSession::Invalid;
        if(!resolve(i?draw.colors[i-1]:draw.target,t.offset,size,colors[i]))return NativeMetal540DesktopSession::Invalid;
        if(bool(t.resource)!=bool(colors[i]))return NativeMetal540DesktopSession::Invalid;
    }
    const GPU::Shader::Texture *stage[]={d.vertexTextures,d.textures};uint64_t *addresses[]={textures.vertex,textures.fragment};
    for(unsigned s=0;s<2;++s)for(unsigned i=0;i<QGPU_STAGE_TEXTURE_COUNT;++i){const auto &t=stage[s][i];uint64_t size=0;
        if(t.resource&&!GPU::Shader::textureBytes(t,size))return NativeMetal540DesktopSession::Invalid;
        if(!resolve(s?draw.fragmentTextures[i]:draw.vertexTextures[i],t.offset,size,addresses[s][i]))return NativeMetal540DesktopSession::Invalid;
        if(bool(t.resource)!=bool(addresses[s][i]))return NativeMetal540DesktopSession::Invalid;
    }
    struct Range {uint64_t address,bytes;};std::vector<Range> inputs,outputs;
    for(unsigned i=0;i<QGPU_GRAPHICS_BUFFER_COUNT;++i)if(buffers.addresses[i])inputs.push_back({buffers.addresses[i],buffers.bytes[i]});
    for(unsigned i=0;i<QGPU_COLOR_TARGET_COUNT;++i)if(colors[i]){uint64_t bytes=0;GPU::Shader::textureBytes(GPU::Shader::colorTarget(d,i),bytes);outputs.push_back({colors[i],bytes});}
    for(unsigned s=0;s<2;++s)for(unsigned i=0;i<QGPU_STAGE_TEXTURE_COUNT;++i)if(addresses[s][i]){uint64_t bytes=0;GPU::Shader::textureBytes(stage[s][i],bytes);inputs.push_back({addresses[s][i],bytes});}
    auto overlap=[](const Range &a,const Range &b){return a.address<b.address+b.bytes&&b.address<a.address+a.bytes;};
    for(const auto &output:outputs)for(const auto &input:inputs)if(overlap(output,input))return NativeMetal540DesktopSession::Invalid;
    for(size_t i=0;i<outputs.size();++i)for(size_t j=0;j<i;++j)if(overlap(outputs[i],outputs[j]))return NativeMetal540DesktopSession::Invalid;
    // Each upstream packet is emitted by the pinned original encoder. No
    // alternate hardware state or A740 translation is introduced here.
    Adreno::Encoder output(static_cast<uint32_t *>(ib.cpu),uint32_t(ib.bytes/4));
    std::vector<uint64_t> storage((sizeof(Adreno::PipelineEncoder)+7)/8);
    auto *encoder=new(storage.data()) Adreno::PipelineEncoder(output,ib.iova,scratch.iova,uint32_t(scratch.bytes));
    auto rc=encoder->draw(d,*vs,*fs,buffers,colors[0],textures,0,0,0,colors+1);
    if(rc==GPU::Result::Success)rc=encoder->finishIndirectBuffer();
    if(rc!=GPU::Result::Success)return NativeMetal540DesktopSession::Invalid;
    words=output.size();return words?0:NativeMetal540DesktopSession::Invalid;
}
}
