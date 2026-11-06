// SPDX-License-Identifier: GPL-2.0-only
// Minimal sysmem raster subset ported from pinned sagit AdrenoPipeline::raster,
// link, constants, shaderObject/loadShader, restore and fd5_program.c. A540
// existing regdefs only. No raw user state, private iOS addresses or compute.
#include "NativeMetal540Render.h"
#include "../BoardRuntime/CheeseburgerGpuBlit540.h"
#include "../../tools/gpu-native540/render_fixture.h"
#include "../../tools/gpu-native540/render_air_fixture.h"
namespace Native540Render {
static bool region(const Region &r) {return r.address && !(r.address&4095) && r.bytes && !(r.bytes&4095) && r.address<AddressLimit && r.bytes<=AddressLimit-r.address;}
static bool legacyInfo(const Info &i) {
    if(i.bufferMask||i.writeMask||i.constantPrefixWords||i.constantWindowCount||i.variant)return false;
    for(unsigned k=0;k<8;++k)if(i.strides[k]||i.logicalBytes[k])return false;
    return true;
}
static bool valid(const Program &p,unsigned stage) {
    if(!legacyInfo(p.info))return false;
    const auto &i=p.info;const uint32_t n=i.constlen*4;
    if(i.stage!=stage || !p.code || !i.codeWords || i.codeWords>1024 || (i.codeWords&31) ||
       !i.instrlen || i.instrlen>32 || i.codeWords<i.instrlen*32 || i.codeWords>(i.instrlen+1)*32 ||
       i.constlen>128 || (i.constlen&3) || !i.fullRegs || i.fullRegs>63 || i.halfRegs>63 ||
       i.branchStack>32 || i.threadSize>1 || i.subgroupSize!=64 || i.immediateBase>n ||
       i.immediateCount>n-i.immediateBase || (i.immediateCount&&!p.immediates) || i.driverBase>n ||
       i.driverCount>n-i.driverBase || i.driverCount>(stage==1?40u:4u) ||
       i.fragCoord!=252 || i.perspective!=252 || i.linear!=252 || i.centerRHW!=252 || i.instanceID!=252 ||
       i.flags!=(stage==1?8u:24u))return false;
    if(i.immediateCount && i.driverCount && i.immediateBase<i.driverBase+i.driverCount && i.driverBase<i.immediateBase+i.immediateCount)return false;
    if(stage==1)return i.vertexID<i.fullRegs*4 && i.position<252 && i.position+4<=i.fullRegs*4 && i.color==252 && !i.colorMask && !i.colorRegister;
    return i.vertexID==252 && i.position==252 && i.colorMask==1 && i.color==i.colorRegister && i.color<252 && i.color+4<=i.fullRegs*4;
}
static bool emptyBinding(const Binding &b) {return !b.allocation.address&&!b.allocation.bytes&&!b.offset&&!b.logicalBytes&&b.readOnly;}
bool plan(const Program &vs,const Program &fs,const Resources &r,Plan &q) {
    q={};if(!valid(vs,1)||!valid(fs,2))return false;
    for(unsigned k=0;k<8;++k)if(!emptyBinding(r.vertexBindings[k])||!emptyBinding(r.fragmentBindings[k]))return false;
    const Region all[]={r.ib,r.vs,r.fs,r.target,r.scratch};
    const uint64_t sizes[]={8192,4096,4096,TargetBytes,4096};
    for(unsigned i=0;i<5;++i){if(!region(all[i])||all[i].bytes!=sizes[i])return false;
        for(unsigned j=0;j<i;++j)if(all[i].address<all[j].address+all[j].bytes && all[j].address<all[i].address+all[i].bytes)return false;}
    if(vs.info.codeWords*4>r.vs.bytes || fs.info.codeWords*4>r.fs.bytes)return false;
    // GPUShaderProtocol::link, zero user varyings: append gl_Position (mask15)
    // at location0, stride4, zero FS interpolation/disable-mask complement.
    q.position=vs.info.position;q.stride=4;
    // AdrenoPipeline::raster vertexParams[1]=start=0, [2]=baseInstance=0,
    // [4]=nonindexed=0. All40 driver words are zero for this exact draw.
    // constants() order is memory prefix(empty) -> immediates -> driver words.
    const Program *programs[]={&vs,&fs};uint32_t *constants[]={q.vsConstants,q.fsConstants};
    for(unsigned k=0;k<2;++k){const auto &p=*programs[k];
        for(unsigned i=0;i<p.info.immediateCount;++i)constants[k][p.info.immediateBase+i]=p.immediates[i];
        for(unsigned i=0;i<p.info.driverCount;++i)constants[k][p.info.driverBase+i]=0;}
    return true;
}
static bool sameAIR(const AIRProgram &a,const AIRProgram &fixed) {
    if(!a.reflection||a.reflectionWords!=621||!a.program.code||!a.program.immediates)return false;
    // Fixed typed/full metadata and actual ISA identities, not generic external
    // IR3 admission. Info contains only uint32 fields, no padding/pointers.
    static_assert(sizeof(Info)==45*sizeof(uint32_t),"host semantic Info words");
    const auto *x=reinterpret_cast<const unsigned char *>(&a.program.info);
    const auto *y=reinterpret_cast<const unsigned char *>(&fixed.program.info);
    for(unsigned i=0;i<sizeof(Info);++i)if(x[i]!=y[i])return false;
    for(unsigned i=0;i<621;++i)if(a.reflection[i]!=fixed.reflection[i])return false;
    for(unsigned i=0;i<fixed.program.info.codeWords;++i)if(a.program.code[i]!=fixed.program.code[i])return false;
    for(unsigned i=0;i<fixed.program.info.immediateCount;++i)if(a.program.immediates[i]!=fixed.program.immediates[i])return false;
    return true;
}
bool planAIR(const AIRProgram &vs,const AIRProgram &fs,const Resources &r,Plan &q) {
    q={};
    if(!sameAIR(vs,Native540RenderAIRFixture::vs)||!sameAIR(fs,Native540RenderAIRFixture::fs))return false;
    const Region all[]={r.ib,r.vs,r.fs,r.target,r.scratch,r.vertexBindings[0].allocation,r.fragmentBindings[0].allocation};
    const uint64_t sizes[]={8192,4096,4096,TargetBytes,4096,4096,4096};
    for(unsigned i=0;i<7;++i){if(!region(all[i])||all[i].bytes!=sizes[i])return false;
        for(unsigned j=0;j<i;++j)if(all[i].address<all[j].address+all[j].bytes&&all[j].address<all[i].address+all[i].bytes)return false;}
    const Binding *bindings[]={r.vertexBindings,r.fragmentBindings};
    const Program *programs[]={&vs.program,&fs.program};
    for(unsigned stage=0;stage<2;++stage){const auto &binding=bindings[stage][0];const auto &i=programs[stage]->info;
        if(!binding.readOnly||binding.offset||binding.logicalBytes!=i.logicalBytes[0]||
           binding.logicalBytes>binding.allocation.bytes)return false;
        for(unsigned k=1;k<8;++k)if(!emptyBinding(bindings[stage][k]))return false;}
    q.position=vs.program.info.position;q.stride=4;
    // sagit graphicsConstants for empty texture/sampler sets is zero. Exact
    // bounds cover hardware bank0 and bank8, BOTH stages' memory prefixes.
    // FS raw metadata retains windowCount1/prefix132, but ordinary base takes
    // guarded SSBO fallback with flag128=0; no optional CP window producer.
    q.vsConstants[96]=q.fsConstants[96]=r.vertexBindings[0].logicalBytes;
    q.vsConstants[104]=q.fsConstants[104]=r.fragmentBindings[0].logicalBytes;
    uint32_t *constants[]={q.vsConstants,q.fsConstants};
    for(unsigned k=0;k<2;++k){const auto &p=*programs[k];
        for(unsigned i=0;i<p.info.immediateCount;++i)constants[k][p.info.immediateBase+i]=p.immediates[i];
        for(unsigned i=0;i<p.info.driverCount;++i)constants[k][p.info.driverBase+i]=0;}
    return true;
}
static bool encodePlanned(const Program &vs,const Program &fs,const Resources &resources,const Plan &q,
                          bool air,uint32_t *words,uint32_t capacity,uint32_t &written) {
    written=0;if(!words)return false;
    uint32_t staged[2048]={};CheeseburgerGpu::Encoder540 e(staged,2048);CheeseburgerGpu::Blit540 b(e);
    if(!b.restore() || !b.begin2D() || !b.solidFill(resources.target.address,64,64,256,Background,resources.scratch.address) || !b.restore())return false;
    bool ok=true;
    auto r=[&](uint32_t a,uint32_t v){if(ok&&!e.reg(a,v))ok=false;};
    auto rr=[&](uint32_t a,const uint32_t *v,uint32_t n){if(ok&&!e.regs(a,v,n))ok=false;};
    auto p=[&](uint32_t op,const uint32_t *v,uint32_t n){if(ok&&!e.packet(op,v,n))ok=false;};
    const uint32_t colorInvalidate=25,depthInvalidate=24;p(0x46,&colorInvalidate,1);p(0x46,&depthInvalidate,1);
    r(REG_A5XX_SP_CS_CONFIG,0);r(REG_A5XX_HLSQ_CS_CONFIG,0);r(REG_A5XX_HLSQ_CS_CNTL,0);r(REG_A5XX_HLSQ_CS_CONSTLEN,0);r(REG_A5XX_HLSQ_CS_INSTRLEN,0);
    const auto &v=vs.info,&f=fs.info;const uint32_t vc=v.instrlen,fc=f.instrlen;
    r(REG_A5XX_HLSQ_VS_CONFIG,A5XX_HLSQ_VS_CONFIG_ENABLED);r(REG_A5XX_SP_VS_CONFIG,A5XX_SP_VS_CONFIG_ENABLED);
    r(REG_A5XX_HLSQ_FS_CONFIG,A5XX_HLSQ_FS_CONFIG_ENABLED|A5XX_HLSQ_FS_CONFIG_CONSTOBJECTOFFSET(v.constlen/4)|A5XX_HLSQ_FS_CONFIG_SHADEROBJOFFSET(64-fc));
    r(REG_A5XX_SP_FS_CONFIG,A5XX_SP_FS_CONFIG_ENABLED|A5XX_SP_FS_CONFIG_CONSTOBJECTOFFSET(v.constlen/4)|A5XX_SP_FS_CONFIG_SHADEROBJOFFSET(64-fc));
    r(REG_A5XX_HLSQ_VS_CNTL,A5XX_HLSQ_VS_CNTL_INSTRLEN(vc)|(air?A5XX_HLSQ_VS_CNTL_SSBO_ENABLE:0));r(REG_A5XX_HLSQ_FS_CNTL,A5XX_HLSQ_FS_CNTL_INSTRLEN(fc)|(air?A5XX_HLSQ_FS_CNTL_SSBO_ENABLE:0));
    r(REG_A5XX_HLSQ_VS_CONSTLEN,v.constlen/4);r(REG_A5XX_HLSQ_VS_INSTRLEN,vc);r(REG_A5XX_HLSQ_FS_CONSTLEN,f.constlen/4);r(REG_A5XX_HLSQ_FS_INSTRLEN,fc);
    r(REG_A5XX_SP_VS_CTRL_REG0,2|A5XX_SP_VS_CTRL_REG0_BUFFER|A5XX_SP_VS_CTRL_REG0_HALFREGFOOTPRINT(v.halfRegs)|A5XX_SP_VS_CTRL_REG0_FULLREGFOOTPRINT(v.fullRegs)|A5XX_SP_VS_CTRL_REG0_BRANCHSTACK(v.branchStack));
    r(REG_A5XX_SP_FS_CTRL_REG0,0x40002|A5XX_SP_FS_CTRL_REG0_BUFFER|A5XX_SP_FS_CTRL_REG0_THREADSIZE(f.threadSize)|A5XX_SP_FS_CTRL_REG0_HALFREGFOOTPRINT(f.halfRegs)|A5XX_SP_FS_CTRL_REG0_FULLREGFOOTPRINT(f.fullRegs)|A5XX_SP_FS_CTRL_REG0_BRANCHSTACK(f.branchStack));
    auto object=[&](uint32_t reg,const Region &s){const uint32_t d[]={uint32_t(s.address),uint32_t(s.address>>32),0,0,0,0};rr(reg,d,6);};
    auto load=[&](const Region &s,unsigned block,unsigned cached){const uint32_t d[]={(2u<<16)|(block<<18)|(cached<<22),uint32_t(s.address),uint32_t(s.address>>32)};p(0x30,d,3);};
    object(REG_A5XX_SP_VS_OBJ_START,resources.vs);load(resources.vs,8,vc);object(REG_A5XX_SP_FS_OBJ_START,resources.fs);
    uint32_t outputs[16]={};outputs[0]=A5XX_SP_VS_OUT_REG_A_REGID(q.position)|A5XX_SP_VS_OUT_REG_A_COMPMASK(15);rr(REG_A5XX_SP_VS_OUT_REG(0),outputs,16);
    uint32_t dst[8]={};rr(REG_A5XX_SP_VS_VPC_DST_REG(0),dst,8);r(REG_A5XX_SP_PRIMITIVE_CNTL,A5XX_SP_PRIMITIVE_CNTL_VSOUT(1));
    const uint32_t disable[]={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};rr(REG_A5XX_VPC_VAR_DISABLE(0),disable,4);
    r(REG_A5XX_VPC_CNTL_0,0x10000|A5XX_VPC_CNTL_0_STRIDE_IN_VPC(4));r(REG_A5XX_VPC_PACK,A5XX_VPC_PACK_PSIZELOC(255));
    r(REG_A5XX_VPC_CLIP_CNTL,A5XX_VPC_CLIP_CNTL_CLIP_DIST_03_LOC(255)|A5XX_VPC_CLIP_CNTL_CLIP_DIST_47_LOC(255));
    r(REG_A5XX_PC_CLIP_CNTL,0);r(REG_A5XX_GRAS_VS_CL_CNTL,0);r(REG_A5XX_PC_PRIM_VTX_CNTL,0);r(REG_A5XX_VPC_GS_SIV_CNTL,0xffff);
    for(unsigned i=0;i<8;++i){r(REG_A5XX_VPC_VARYING_INTERP_MODE(i),0);r(REG_A5XX_VPC_VARYING_PS_REPL_MODE(i),0);}
    r(REG_A5XX_HLSQ_CONTROL_0_REG,0x880|A5XX_HLSQ_CONTROL_0_REG_FSTHREADSIZE(f.threadSize));
    r(REG_A5XX_HLSQ_CONTROL_1_REG,A5XX_HLSQ_CONTROL_1_REG_PRIMALLOCTHRESHOLD(63));
    r(REG_A5XX_HLSQ_CONTROL_2_REG,0xfcfcfcfc);r(REG_A5XX_HLSQ_CONTROL_3_REG,0xfcfcfcfc);r(REG_A5XX_HLSQ_CONTROL_4_REG,0xfcfcfcfc);
    r(REG_A5XX_HLSQ_UPDATE_CNTL,0x020fffff);load(resources.fs,12,fc);
    auto constants=[&](unsigned block,unsigned constlen,const uint32_t *src){if(!constlen)return;uint32_t data[515]={};data[0]=(block<<18)|(constlen<<22);data[1]=1;
        for(unsigned i=0;i<constlen*4;++i)data[3+i]=src[i];p(0x30,data,3+constlen*4);};
    constants(8,v.constlen,q.vsConstants);constants(12,f.constlen,q.fsConstants);
    if(air){
        // Original sagit buffers(false): graphics block14, ALL16 dimensions,
        // then type2 64-bit addresses. Fragment logical0 is hardware bank8.
        uint32_t dimensions[35]={},addresses[35]={};
        dimensions[0]=addresses[0]=(14u<<18)|(16u<<22);dimensions[1]=1;addresses[1]=2;
        const Binding *bindings[]={&resources.vertexBindings[0],&resources.fragmentBindings[0]};
        for(unsigned k=0;k<2;++k){const auto &binding=*bindings[k];const unsigned slot=k?8:0;
            dimensions[3+slot*2]=A5XX_SSBO_1_0_WIDTH(binding.logicalBytes&0xffff);
            dimensions[4+slot*2]=A5XX_SSBO_1_1_HEIGHT(binding.logicalBytes>>16);
            const uint64_t address=binding.allocation.address+binding.offset;
            addresses[3+slot*2]=uint32_t(address);addresses[4+slot*2]=uint32_t(address>>32);}
        p(0x30,dimensions,35);p(0x30,addresses,35);
    }
    r(REG_A5XX_SP_SP_CNTL,0x10);r(REG_A5XX_GRAS_CNTL,0);r(REG_A5XX_RB_RENDER_CONTROL0,0);r(REG_A5XX_RB_RENDER_CONTROL1,0);
    r(REG_A5XX_RB_FS_OUTPUT_CNTL,A5XX_RB_FS_OUTPUT_CNTL_MRT(1));
    r(REG_A5XX_SP_FS_OUTPUT_CNTL,A5XX_SP_FS_OUTPUT_CNTL_MRT(1)|A5XX_SP_FS_OUTPUT_CNTL_DEPTH_REGID(252)|A5XX_SP_FS_OUTPUT_CNTL_SAMPLEMASK_REGID(252));
    uint32_t fsOutputs[8];for(unsigned i=0;i<8;++i)fsOutputs[i]=A5XX_SP_FS_OUTPUT_REG_REGID(i?252:f.colorRegister);rr(REG_A5XX_SP_FS_OUTPUT_REG(0),fsOutputs,8);
    r(REG_A5XX_VFD_CONTROL_0,A5XX_VFD_CONTROL_0_VTXCNT(0)); // gl_VertexID, no vertex BO/fetch
    r(REG_A5XX_VFD_CONTROL_1,0xfc0000|A5XX_VFD_CONTROL_1_REGID4VTX(v.vertexID)|A5XX_VFD_CONTROL_1_REGID4INST(252));
    const uint32_t vertexControl[]={0xfcfc,0xfcfc,252,0};rr(REG_A5XX_VFD_CONTROL_2,vertexControl,4);
    r(REG_A5XX_PC_RESTART_INDEX,UINT32_MAX);r(REG_A5XX_PC_PRIMITIVE_CNTL,A5XX_PC_PRIMITIVE_CNTL_STRIDE_IN_VPC(4));
    r(REG_A5XX_GRAS_CL_CNTL,A5XX_GRAS_CL_CNTL_ZERO_GB_SCALE_Z);
    const uint32_t viewport[]={0x42000000,0x42000000,0x42000000,0xc2000000,0,0x3f800000};rr(REG_A5XX_GRAS_CL_VPORT_XOFFSET_0,viewport,6);
    const uint32_t scissor[]={0,63|(63u<<16)};rr(REG_A5XX_GRAS_SC_WINDOW_SCISSOR_TL,scissor,2);rr(REG_A5XX_GRAS_SC_SCREEN_SCISSOR_TL_0,scissor,2);rr(REG_A5XX_GRAS_SC_VIEWPORT_SCISSOR_TL_0,scissor,2);
    r(REG_A5XX_RB_WINDOW_OFFSET,0);r(REG_A5XX_RB_RESOLVE_CNTL_1,0);r(REG_A5XX_RB_RESOLVE_CNTL_2,scissor[1]);
    const uint32_t depthRegs[]={REG_A5XX_RB_DEPTH_CNTL,REG_A5XX_GRAS_LRZ_CNTL,REG_A5XX_RB_ALPHA_CONTROL,REG_A5XX_RB_DEPTH_BUFFER_INFO,REG_A5XX_GRAS_SU_DEPTH_BUFFER_INFO,REG_A5XX_RB_DEPTH_BUFFER_BASE,REG_A5XX_RB_DEPTH_BUFFER_BASE+1,REG_A5XX_RB_DEPTH_BUFFER_PITCH,REG_A5XX_RB_DEPTH_BUFFER_ARRAY_PITCH,REG_A5XX_RB_STENCIL_INFO,REG_A5XX_RB_STENCIL_BASE,REG_A5XX_RB_STENCIL_BASE+1,REG_A5XX_RB_STENCIL_PITCH,REG_A5XX_RB_STENCIL_ARRAY_PITCH,REG_A5XX_RB_STENCIL_CONTROL,REG_A5XX_RB_STENCILREFMASK,REG_A5XX_RB_STENCILREFMASK_BF};
    for(auto reg:depthRegs)r(reg,0);
    const uint32_t depthFlags[3]={},lrz[5]={};rr(REG_A5XX_RB_DEPTH_FLAG_BUFFER_BASE,depthFlags,3);rr(REG_A5XX_GRAS_LRZ_BUFFER_BASE,lrz,5);
    r(REG_A5XX_RB_DEPTH_PLANE_CNTL,0);r(REG_A5XX_GRAS_SU_DEPTH_PLANE_CNTL,0);
    r(REG_A5XX_RB_RENDER_COMPONENTS,15);r(REG_A5XX_SP_BLEND_CNTL,A5XX_SP_BLEND_CNTL_UNK8);r(REG_A5XX_RB_BLEND_CNTL,A5XX_RB_BLEND_CNTL_SAMPLE_MASK(0xffff));
    for(unsigned i=0;i<4;++i){r(REG_A5XX_RB_BLEND_RED+i*2,0);r(REG_A5XX_RB_BLEND_RED_F32+i*2,0);}
    for(unsigned i=0;i<8;++i){const bool bound=i==0;
        r(REG_A5XX_RB_MRT_CONTROL(i),A5XX_RB_MRT_CONTROL_ROP_CODE(12)|A5XX_RB_MRT_CONTROL_COMPONENT_ENABLE(bound?15:0));r(REG_A5XX_RB_MRT_BLEND_CONTROL(i),0);
        r(REG_A5XX_RB_MRT_BUF_INFO(i),bound?A5XX_RB_MRT_BUF_INFO_COLOR_FORMAT(0x30):0);r(REG_A5XX_RB_MRT_PITCH(i),bound?A5XX_RB_MRT_PITCH(Pitch):0);r(REG_A5XX_RB_MRT_ARRAY_PITCH(i),bound?A5XX_RB_MRT_ARRAY_PITCH(TargetBytes):0);
        const uint64_t at=bound?resources.target.address:0;const uint32_t base[]={uint32_t(at),uint32_t(at>>32)};rr(REG_A5XX_RB_MRT_BASE(i),base,2);
        r(REG_A5XX_SP_FS_MRT_REG(i),bound?A5XX_SP_FS_MRT_REG_COLOR_FORMAT(0x30):0);const uint32_t flags[4]={};rr(REG_A5XX_RB_MRT_FLAG_BUFFER(i),flags,4);}
    r(REG_A5XX_RB_RAS_MSAA_CNTL,0);r(REG_A5XX_RB_DEST_MSAA_CNTL,A5XX_RB_DEST_MSAA_CNTL_MSAA_DISABLE);
    r(REG_A5XX_GRAS_SC_RAS_MSAA_CNTL,0);r(REG_A5XX_GRAS_SC_DEST_MSAA_CNTL,A5XX_GRAS_SC_DEST_MSAA_CNTL_MSAA_DISABLE);
    r(REG_A5XX_TPL1_TP_RAS_MSAA_CNTL,0);r(REG_A5XX_TPL1_TP_DEST_MSAA_CNTL,A5XX_TPL1_TP_DEST_MSAA_CNTL_MSAA_DISABLE);
    const uint32_t visibility=1;p(0x64,&visibility,1);r(REG_A5XX_TPL1_VS_TEX_COUNT,0);r(REG_A5XX_TPL1_FS_TEX_COUNT,0);
    r(REG_A5XX_VFD_INDEX_OFFSET,0);r(REG_A5XX_VFD_INSTANCE_START_OFFSET,0);r(REG_A5XX_GRAS_SU_CNTL,A5XX_GRAS_SU_CNTL_FRONT_CW);
    const uint32_t draw[]={4u|(2u<<10)|(2u<<6),1,3};p(0x38,draw,3);p(0x26,nullptr,0);
    if(!ok || !b.flushWait(resources.scratch.address+8,29) || !b.flushWait(resources.scratch.address+16,4) || !e.barrier() || e.size()>capacity)return false;
    for(unsigned i=0;i<e.size();++i)words[i]=staged[i];written=e.size();return true;
}
bool encode(const Program &vs,const Program &fs,const Resources &resources,uint32_t *words,uint32_t capacity,uint32_t &written) {
    written=0;Plan q;if(!words||!plan(vs,fs,resources,q))return false;
    return encodePlanned(vs,fs,resources,q,false,words,capacity,written);
}
bool encodeAIR(const AIRProgram &vs,const AIRProgram &fs,const Resources &resources,uint32_t *words,uint32_t capacity,uint32_t &written) {
    written=0;Plan q;if(!words||!planAIR(vs,fs,resources,q))return false;
    return encodePlanned(vs.program,fs.program,resources,q,true,words,capacity,written);
}
bool verifyColor(const volatile uint32_t *p,size_t bytes,uint32_t color,Pixels &r) {
    r={};if(!p || bytes!=TargetBytes)return false;r.center=p[32*64+32];r.corner=p[0];
    // Window vertices (8,56),(56,56),(32,8). Integer half-pixel tests,
    // excluding a one-pixel boundary band; no claim about top-left edge rules.
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){const int X=2*x+1,Y=2*y+1;
        const int a=112-Y,b=2*X+Y-144,c=112-2*X+Y;
        bool inside=a>2 && b>5 && c>5,outside=a<-2 || b<-5 || c<-5;
        if(inside){++r.inside;r.mismatches+=p[y*64+x]!=color;}
        else if(outside){++r.outside;r.mismatches+=p[y*64+x]!=Background;}else ++r.uncheckedEdge;}
    return r.center==color && r.corner==Background && r.inside && r.outside && !r.mismatches;
}
bool verify(const volatile uint32_t *p,size_t bytes,Pixels &r) {return verifyColor(p,bytes,Center,r);}
}
void NativeMetal540Render::stage(Stage s,uint32_t i){record_.stage=s;record_.resource=i;record_.handle=bos_[i].handle;record_.iova=bos_[i].iova;record_.cpuBytes=bos_[i].bytes;if(transport_.record)transport_.record(transport_.context,record_);}
int NativeMetal540Render::fail(int rc){if(!record_.error)record_.error=rc;if(transport_.record)transport_.record(transport_.context,record_);return record_.error;}
int NativeMetal540Render::request(uint32_t m,const void *in,size_t n,void *out,size_t expected){size_t bytes=expected;int rc=transport_.call(transport_.context,m,in,n,out,&bytes);return rc?rc:bytes==expected?0:Invalid;}
int NativeMetal540Render::run(bool allComputeSucceeded) {
    if(attempted_)return AlreadyAttempted;attempted_=true;if(!allComputeSucceeded)return fail(Prerequisite);
    if(!transport_.call||!transport_.mapView||!transport_.order||transport_.policy!=InhibitCache)return fail(Invalid);
    stage(Begin);NativeMetal540Client client(transport_.call,transport_.context);stage(Query);int rc=client.query();if(rc)return fail(rc);
    NativeAdrenoAbiDeviceInfo info{};stage(Device);rc=request(NativeAdrenoMethodGetDeviceInfo,nullptr,0,&info,sizeof(info));if(rc)return fail(rc);
    constexpr uint64_t offset=0x100000,extent=9*4096;
    if(info.chip_id!=0x05040001u||info.reserved||info.flags!=NativeAdrenoDeviceClientIova || (info.va_start&4095) || info.va_start>=Native540Render::AddressLimit || info.va_size<offset+extent || info.va_size>Native540Render::AddressLimit-info.va_start)return fail(Invalid);
    uint64_t cursor=info.va_start+offset;
    const uint64_t sizes[]={8192,4096,4096,16384,4096};
    for(unsigned i=0;i<5;++i){stage(Allocate,i);rc=client.allocate(sizes[i],bos_[i].handle);if(rc)return fail(rc);
        for(unsigned j=0;j<i;++j)if(bos_[j].handle==bos_[i].handle)return fail(Invalid);
        stage(MapGPU,i);rc=client.map(bos_[i].handle,cursor,i<3?1:3,bos_[i].iova);if(rc)return fail(rc);cursor+=sizes[i];
        NativeAdrenoAbiBoCpuMap in{bos_[i].handle};NativeAdrenoAbiBoCpuMapReply out{};stage(MapCPU,i);rc=request(NativeAdrenoMethodBoCpuMap,&in,sizeof(in),&out,sizeof(out));if(rc)return fail(rc);
        if(!out.token||out.reserved||out.bytes!=sizes[i])return fail(Invalid);bos_[i].token=out.token;
        for(unsigned j=0;j<i;++j)if(out.token==bos_[j].token)return fail(Invalid);
        rc=transport_.mapView(transport_.context,out.token,bos_[i].address,bos_[i].bytes);if(rc)return fail(rc);
        const auto &b=bos_[i];if(!b.address||(b.address&3)||b.bytes!=sizes[i]||b.bytes>UINT64_MAX-b.address)return fail(Invalid);
        for(unsigned j=0;j<i;++j)if(b.address<bos_[j].address+bos_[j].bytes && bos_[j].address<b.address+b.bytes)return fail(Invalid);stage(MapCPU,i);}
    Native540Render::Resources resources{{bos_[0].iova,bos_[0].bytes},{bos_[1].iova,bos_[1].bytes},{bos_[2].iova,bos_[2].bytes},{bos_[3].iova,bos_[3].bytes},{bos_[4].iova,bos_[4].bytes}};
    uint32_t words[2048]={},written=0;stage(Encode);if(!Native540Render::encode(Native540RenderFixture::vs,Native540RenderFixture::fs,resources,words,2048,written)||!written)return fail(Invalid);
    record_.ibWords=written;record_.vsWords=Native540RenderFixture::vs.info.codeWords;record_.fsWords=Native540RenderFixture::fs.info.codeWords;stage(Upload);
    for(unsigned i=0;i<5;++i){auto *p=reinterpret_cast<volatile uint32_t *>(bos_[i].address);for(unsigned j=0;j<bos_[i].bytes/4;++j)p[j]=i==3?Native540Render::Sentinel:0;}
    const Native540Render::Program *programs[]={&Native540RenderFixture::vs,&Native540RenderFixture::fs};
    for(unsigned k=0;k<2;++k){auto *shader=reinterpret_cast<volatile uint32_t *>(bos_[k+1].address);for(unsigned i=0;i<programs[k]->info.codeWords;++i)shader[i]=programs[k]->code[i];}
    auto *ib=reinterpret_cast<volatile uint32_t *>(bos_[0].address);for(unsigned i=0;i<written;++i)ib[i]=words[i];transport_.order(transport_.context,true);
    for(unsigned i=0;i<written;++i)if(ib[i]!=words[i])return fail(Mismatch);
    for(unsigned k=0;k<2;++k){auto *shader=reinterpret_cast<volatile const uint32_t *>(bos_[k+1].address);for(unsigned i=0;i<programs[k]->info.codeWords;++i)if(shader[i]!=programs[k]->code[i])return fail(Mismatch);}
    const NativeAdrenoAbiSubmitBuffer submitted[]={{bos_[0].handle,bos_[0].iova,uint64_t(written)*4}};stage(Submit);rc=client.submit(submitted,1,record_.fence);if(rc)return fail(rc);stage(Fence);rc=client.wait(record_.fence,200000000ull);if(rc)return fail(rc);
    transport_.order(transport_.context,false);stage(Readback);auto *scratch=reinterpret_cast<volatile const uint32_t *>(bos_[4].address);
    record_.clearTimestamp=scratch[0];record_.colorTimestamp=scratch[2];record_.ucheTimestamp=scratch[4];
    record_.readbackVerified=Native540Render::verify(reinterpret_cast<volatile const uint32_t *>(bos_[3].address),bos_[3].bytes,record_.pixels);
    if(!record_.readbackVerified||record_.clearTimestamp!=1||record_.colorTimestamp!=2||record_.ucheTimestamp!=3)return fail(Mismatch);
    stage(Complete);return 0;
}
