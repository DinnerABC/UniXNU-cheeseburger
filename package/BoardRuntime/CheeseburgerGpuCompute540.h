// SPDX-License-Identifier: GPL-2.0-only
// A540 compute dispatch (sagit AdrenoPipeline.h `dispatch` + Mesa IR3).
//
// The shader is Mesa IR3 for a5xx, assembled offline by a small host tool
// (tools/gpu-ir3/assemble.py documents the command) from:
//
//   @localsize 1, 1, 1
//   @buf 69120
//   @invocationid(r0.x)
//   shl.b r1.x, r0.x, 8
//   stib.b.untyped.1d.u32.1.imm r1.x, r0.x, 0
//   end
//
// One invocation stores one dword into SSBO 0 at offset id; with 64 rows of
// 1080 pixels the SSBO is the first 276480 bytes of the firmware scanout, so
// the byte-addressed store bypasses the 64-byte 2D/3D pitch rule that blocks
// the GOP's 4320-byte pitch. State order follows sagit's compute path
// (restore, CS config, shader object, SSBO descriptors, NDRANGE, CP 0x33)
// and ends with the shader-memory flush the CPU can wait on.
#pragma once
#include "CheeseburgerGpuBlit540.h"

namespace CheeseburgerGpu {

// Same dispatch state and metadata as ScanBand540; only the 32-word ISA
// differs. Probe540 writes 0xdeadbeef to every covered dword, which separates
// "dispatch/shader did not write" from "the scanout address or flush is wrong".
struct Probe540 {
    static constexpr uint32_t Code[32] = {
        0xdeadbeefu, 0x204cc004u, 0x00000000u, 0x00000500u,
        0x00800001u, 0xc7660004u, 0x00000000u, 0x03000000u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    };
};

struct ScanBand540 {
    // Mesa IR3 machine code, a5xx (gpu-id 540), 32 words (one instruction unit).
    static constexpr uint32_t Code[32] = {
        0x20080000u, 0x46d00004u, 0x00000000u, 0x00000400u,
        0x00800001u, 0xc7660004u, 0x00000000u, 0x03000000u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    };
    static constexpr uint32_t CodeWords = 32;
    static constexpr uint32_t InstrLen = 1;
    static constexpr uint32_t ConstLen = 0;
    static constexpr uint32_t FullRegs = 2;    // max_reg + 1 (Mesa a4xx/a5xx)
    static constexpr uint32_t HalfRegs = 0;
    static constexpr uint32_t BranchStack = 0;
    static constexpr uint32_t ThreadSize = 1;  // double_threadsize: FOUR_QUADS
    static constexpr uint32_t LocalIdReg = 0;  // @invocationid(r0.x)
    static constexpr uint32_t WgIdConst = 252; // no @wgid
    static constexpr uint32_t ComputeBlock = 13;

    // CS state under test. Variant 0 is sagit's exact configuration; the
    // others isolate one field at a time when a dispatch fences without any
    // observed store.
    struct Variant {
        uint32_t wgid, unk0, unk1, threadSize, ctrlBase, bufBlock, bufCount;
        bool bufferBit;
    };
    static constexpr Variant DefaultVariant() {
        return Variant{252, 252, 252, ThreadSize, 0x2, 15, 16, true};
    }

    static bool encode(Encoder540 &e, uint64_t shaderVA, uint64_t bufferVA,
                       uint32_t bufferBytes, uint32_t local, uint32_t groups,
                       uint64_t scratch) {
        return encodeVariant(e, shaderVA, bufferVA, bufferBytes, local, groups,
                             scratch, DefaultVariant());
    }

    static bool encodeVariant(Encoder540 &e, uint64_t shaderVA, uint64_t bufferVA,
                              uint32_t bufferBytes, uint32_t local, uint32_t groups,
                              uint64_t scratch, const Variant &v, const uint32_t *directCode=nullptr) {
        Blit540 base(e);
        if (!base.restore()) return false;
        bool ok = true;
        auto r = [&](uint32_t a, uint32_t v) { if (ok && !e.reg(a, v)) ok = false; };
        auto p = [&](uint32_t op, const uint32_t *d, uint32_t n) {
            if (ok && !e.packet(op, d, n)) ok = false;
        };
        if (!local || !groups || (local & (local - 1)) || local > 1024 || !bufferBytes)
            return false;
        r(REG_A5XX_SP_SP_CNTL, 0);
        r(REG_A5XX_HLSQ_CONTROL_0_REG,
          A5XX_HLSQ_CONTROL_0_REG_CSTHREADSIZE(v.threadSize) | 0x880);
        r(REG_A5XX_SP_VS_CONFIG, 0); r(REG_A5XX_SP_FS_CONFIG, 0);
        r(REG_A5XX_HLSQ_VS_CONFIG, 0); r(REG_A5XX_HLSQ_FS_CONFIG, 0);
        r(REG_A5XX_HLSQ_VS_CONSTLEN, 0); r(REG_A5XX_HLSQ_FS_CONSTLEN, 0);
        r(REG_A5XX_HLSQ_VS_INSTRLEN, 0); r(REG_A5XX_HLSQ_FS_INSTRLEN, 0);
        r(REG_A5XX_HLSQ_CS_CONFIG, A5XX_HLSQ_CS_CONFIG_ENABLED);
        r(REG_A5XX_SP_CS_CONFIG, A5XX_SP_CS_CONFIG_ENABLED);
        r(REG_A5XX_SP_CS_CTRL_REG0,
          v.ctrlBase | (v.bufferBit ? A5XX_SP_CS_CTRL_REG0_BUFFER : 0u) |
          A5XX_SP_CS_CTRL_REG0_THREADSIZE(v.threadSize) |
          A5XX_SP_CS_CTRL_REG0_FULLREGFOOTPRINT(FullRegs) |
          A5XX_SP_CS_CTRL_REG0_HALFREGFOOTPRINT(HalfRegs) |
          A5XX_SP_CS_CTRL_REG0_BRANCHSTACK(BranchStack));
        r(REG_A5XX_HLSQ_CS_CNTL,
          A5XX_HLSQ_CS_CNTL_INSTRLEN(InstrLen) | A5XX_HLSQ_CS_CNTL_SSBO_ENABLE);
        r(REG_A5XX_HLSQ_CS_CONSTLEN, ConstLen / 4);
        r(REG_A5XX_HLSQ_CS_INSTRLEN, InstrLen);
        // SP_CS_OBJ_START is a 64-bit address followed by four zero words in
        // sagit's object write; Mesa 26 exposes the pair as one reg64.
        const uint32_t object[6]={uint32_t(shaderVA),uint32_t(shaderVA>>32),0,0,0,0};
        if(!e.regs(REG_A5XX_SP_CS_OBJ_START,object,6))ok=false;
        r(REG_A5XX_HLSQ_UPDATE_CNTL, 0x1f00000);
        r(REG_A5XX_HLSQ_CS_CNTL_0,
          A5XX_HLSQ_CS_CNTL_0_WGIDCONSTID(v.wgid) |
          A5XX_HLSQ_CS_CNTL_0_UNK0(v.unk0) | A5XX_HLSQ_CS_CNTL_0_UNK1(v.unk1) |
          A5XX_HLSQ_CS_CNTL_0_LOCALIDREGID(LocalIdReg));
        r(REG_A5XX_HLSQ_CS_CNTL_1, 1);
        // SS4 load of the cached instruction unit from the shader object.
        const uint32_t load[3] = {(2u << 16) | (ComputeBlock << 18) | (InstrLen << 22),
                                  uint32_t(shaderVA), uint32_t(shaderVA >> 32)};
        if(directCode){
            uint32_t direct[3+CodeWords]={};
            direct[0]=(ComputeBlock<<18)|(InstrLen<<22);
            for(unsigned i=0;i<CodeWords;++i)direct[3+i]=directCode[i];
            p(0x30,direct,3+CodeWords);
        }else p(0x30, load, 3);
        // Compute SSBO descriptors: state block 15, 16 entries. Only buffer 0
        // is bound; WIDTH/HEIGHT carry its byte size, the second packet its
        // 64-bit GPU address.
        uint32_t values[3 + 16 * 2] = {};
        const uint32_t count = v.bufCount > 16 ? 16 : v.bufCount;
        values[0] = (v.bufBlock << 18) | (count << 22);
        values[1] = 1;
        values[3] = A5XX_SSBO_1_0_WIDTH(bufferBytes & 0xffffu);
        values[4] = A5XX_SSBO_1_1_HEIGHT(bufferBytes >> 16);
        p(0x30, values, 3 + count * 2);
        values[1] = 2;
        values[3] = uint32_t(bufferVA);
        values[4] = uint32_t(bufferVA >> 32);
        p(0x30, values, 3 + count * 2);
        r(REG_A5XX_HLSQ_CS_NDRANGE_0,
          A5XX_HLSQ_CS_NDRANGE_0_KERNELDIM(1) |
          A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEX(local - 1) |
          A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEY(0) |
          A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEZ(0));
        r(REG_A5XX_HLSQ_CS_NDRANGE_1, groups * local);
        r(REG_A5XX_HLSQ_CS_NDRANGE_2, 0);
        r(REG_A5XX_HLSQ_CS_NDRANGE_3, 1);
        r(REG_A5XX_HLSQ_CS_NDRANGE_4, 0);
        r(REG_A5XX_HLSQ_CS_NDRANGE_5, 1);
        r(REG_A5XX_HLSQ_CS_NDRANGE_6, 0);
        r(REG_A5XX_HLSQ_CS_KERNEL_GROUP_X, 1);
        r(REG_A5XX_HLSQ_CS_KERNEL_GROUP_Y, 1);
        r(REG_A5XX_HLSQ_CS_KERNEL_GROUP_Z, 1);
        const uint32_t exec[4] = {0, groups, 1, 1};
        p(0x33, exec, 4);
        p(0x26, nullptr, 0);
        // Publish UAV writes: sagit's code 4 flush, then the CP wait.
        Blit540 flush(e);
        if (!flush.flushWait(scratch, 4)) ok = false;
        return ok && e.size() > 0;
    }
};

}  // namespace CheeseburgerGpu
