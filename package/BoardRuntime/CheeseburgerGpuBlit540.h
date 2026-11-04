// SPDX-License-Identifier: GPL-2.0-only
// A540 2D graphics path. The state and packet order are ported from the
// pinned Xiaomi 6 (sagit) AdrenoPipeline.h `clearRows`/`restore`, which follows
// Mesa 24.3.4 fd5_emit/fd5_blitter (RB/GRAS 2D blitter with a solid source).
// This is real RB/CCU work: the CP drives the 2D engine, the color pipe writes
// the render target, and the stream ends with a color-cache flush the CPU can
// wait on. It is not a row-by-row CP_MEM_WRITE.
//
// Every encoder failure is fatal to the stream; a caller must not kick a
// partially encoded stream (same contract as Encoder540).
#pragma once
#include "CheeseburgerGpuEngine540.h"
#include "A5xxRegs.h"

namespace CheeseburgerGpu {

class Blit540 {
    Encoder540 &e;
    bool ok_ = true;
    uint32_t flushSeq_ = 0;

    void r(uint32_t address, uint32_t value) {
        if (ok_ && !e.reg(address, value)) ok_ = false;
    }
    void rr(uint32_t address, const uint32_t *values, uint32_t count) {
        for (uint32_t i = 0; ok_ && i < count; ++i) r(address + i, values[i]);
    }
    void p(uint32_t op, const uint32_t *data, uint32_t count) {
        if (ok_ && !e.packet(op, data, count)) ok_ = false;
    }
    void idle() { p(0x26, nullptr, 0); }
    // Mesa fd5 sysmem flushes: event 25 invalidates CCU color/depth, 24 flushes
    // the UCHE. They carry no payload and are ordered by the following idle.
    void event(uint32_t code) { p(0x46, &code, 1); }
    void invalidateUCHE() {
        r(REG_A5XX_UCHE_CACHE_INVALIDATE_MIN_LO, 0);
        r(REG_A5XX_UCHE_CACHE_INVALIDATE_MIN_HI, 0);
        r(REG_A5XX_UCHE_CACHE_INVALIDATE_MAX_LO, 0);
        r(REG_A5XX_UCHE_CACHE_INVALIDATE_MAX_HI, 0);
        r(REG_A5XX_UCHE_CACHE_INVALIDATE, 0x12);
        idle();
    }

public:
    explicit Blit540(Encoder540 &encoder) : e(encoder) {}
    bool ok() const { return ok_; }
    uint32_t words() const { return e.size(); }

    // fd5_emit_restore for a linear (GMEM bypass) render target. The sagit
    // bitmap is reproduced register for register; the fields this board does
    // not program (GMEM layout, raster cache) are the fixed bypass values.
    bool restore() {
        idle();
        const uint32_t mode[5] = {1, 0, 0, 0, 0};  // SET_MODE: direct/linear
        p(0x6c, mode, 5);
        // SP/RBCCU can lose non-context state in a runtime power collapse.
        // Reassert the A5xx 64-bit addressing contract before object state.
        r(REG_A5XX_SP_ADDR_MODE_CNTL, 1);
        r(REG_A5XX_RB_ADDR_MODE_CNTL, 1);
        r(REG_A5XX_TPL1_ADDR_MODE_CNTL, 1);
        const uint32_t skip = 0;
        p(0x1d, &skip, 1);  // no inherited IB2 skip state
        invalidateUCHE();
        r(REG_A5XX_HLSQ_UPDATE_CNTL, 0xfffff);
        r(REG_A5XX_PC_RESTART_INDEX, 0xffffffff);
        r(REG_A5XX_PC_RASTER_CNTL, 0x12);
        r(REG_A5XX_UNKNOWN_E292, 0);
        r(REG_A5XX_UNKNOWN_E292 + 1, 0);
        r(REG_A5XX_TPL1_TP_FS_ROTATION_CNTL, 0);
        r(REG_A5XX_UNKNOWN_E004, 0);
        r(REG_A5XX_GRAS_SU_LAYERED, 0);
        r(REG_A5XX_PC_GS_LAYERED, 0);
        r(REG_A5XX_UNKNOWN_E5AB, 0);
        r(REG_A5XX_UNKNOWN_E5C2, 0);
        r(REG_A5XX_UNKNOWN_E5DB, 0);
        r(REG_A5XX_SP_HS_CTRL_REG0, 0);
        r(REG_A5XX_SP_DS_CTRL_REG0, 0);
        r(REG_A5XX_SP_GS_CTRL_REG0, 0);
        r(REG_A5XX_RB_CLEAR_CNTL, 0);
        static const uint32_t resetRanges[] = {
            REG_A5XX_UNKNOWN_E7C0, REG_A5XX_UNKNOWN_E7C5, REG_A5XX_UNKNOWN_E7CA,
            REG_A5XX_UNKNOWN_E7CF, REG_A5XX_UNKNOWN_E7D4, REG_A5XX_UNKNOWN_E7D9};
        const uint32_t resetValues[3] = {0, 0, 0};
        for (unsigned i = 0; i < sizeof resetRanges / sizeof resetRanges[0]; ++i)
            rr(resetRanges[i], resetValues, 3);
        r(REG_A5XX_GRAS_SU_CONSERVATIVE_RAS_CNTL, 0);
        r(REG_A5XX_GRAS_SC_SCREEN_SCISSOR_CNTL, 0);
        r(REG_A5XX_SP_VS_CONFIG_MAX_CONST, 0);
        r(REG_A5XX_SP_FS_CONFIG_MAX_CONST, 0);
        r(REG_A5XX_RB_MODE_CNTL, 0x44);
        r(REG_A5XX_RB_DBG_ECO_CNTL, 0x100000);
        r(REG_A5XX_RB_RENDER_CNTL, 8);
        r(REG_A5XX_GRAS_SC_CNTL, 8);
        r(REG_A5XX_VFD_MODE_CNTL, 0);
        r(REG_A5XX_PC_MODE_CNTL, 0x1f);
        r(REG_A5XX_SP_MODE_CNTL, 0x1e);
        r(REG_A5XX_SP_DBG_ECO_CNTL, 0x800);
        r(REG_A5XX_HLSQ_DBG_ECO_CNTL, 0);
        r(REG_A5XX_TPL1_MODE_CNTL, 0x544);
        r(REG_A5XX_HLSQ_MODE_CNTL, 1);
        r(REG_A5XX_HLSQ_TIMEOUT_THRESHOLD_0, 0x80);
        r(REG_A5XX_HLSQ_TIMEOUT_THRESHOLD_1, 0);
        r(REG_A5XX_VPC_DBG_ECO_CNTL, 0x400);
        r(REG_A5XX_VPC_MODE_CNTL, 0);
        const uint32_t drawStates[3] = {1u << 18, 0, 0};  // disable draw-state groups
        p(0x43, drawStates, 3);
        r(REG_A5XX_GRAS_SC_BIN_CNTL, 0);
        r(REG_A5XX_VPC_FS_PRIMITIVEID_CNTL, 0xff);
        r(REG_A5XX_VPC_SO_OVERRIDE, A5XX_VPC_SO_OVERRIDE_SO_DISABLE);
        r(REG_A5XX_VPC_SO_CNTL, 0);
        r(REG_A5XX_VPC_SO_BUF_CNTL, 0);
        r(REG_A5XX_PC_GS_PARAM, 0);
        r(REG_A5XX_PC_HS_PARAM, 0);
        r(REG_A5XX_SP_HS_CONFIG, 0);
        r(REG_A5XX_SP_DS_CONFIG, 0);
        r(REG_A5XX_SP_GS_CONFIG, 0);
        r(REG_A5XX_HLSQ_HS_CONFIG, 0);
        r(REG_A5XX_HLSQ_DS_CONFIG, 0);
        r(REG_A5XX_HLSQ_GS_CONFIG, 0);
        r(REG_A5XX_HLSQ_HS_CNTL, 0);
        r(REG_A5XX_HLSQ_DS_CNTL, 0);
        r(REG_A5XX_HLSQ_GS_CNTL, 0);
        r(REG_A5XX_HLSQ_HS_CONSTLEN, 0);
        r(REG_A5XX_HLSQ_DS_CONSTLEN, 0);
        r(REG_A5XX_HLSQ_GS_CONSTLEN, 0);
        r(REG_A5XX_TPL1_VS_TEX_COUNT, 0);
        r(REG_A5XX_TPL1_FS_TEX_COUNT, 0);
        r(REG_A5XX_TPL1_HS_TEX_COUNT, 0);
        r(REG_A5XX_TPL1_DS_TEX_COUNT, 0);
        r(REG_A5XX_TPL1_GS_TEX_COUNT, 0);
        r(REG_A5XX_TPL1_CS_TEX_COUNT, 0);
        r(REG_A5XX_PC_POWER_CNTL, 3);   // A540: four SP cores
        r(REG_A5XX_VFD_POWER_CNTL, 3);
        r(REG_A5XX_RB_CCU_CNTL, 0x10000000);
        r(REG_A5XX_RB_CNTL, A5XX_RB_CNTL_BYPASS);
        return ok_;
    }

    // CACHE_FLUSH_TS with a color-cache flush code, then CP_WAIT_REG_MEM for
    // the sequence word. The caller must zero the scratch cell before the kick
    // so an old value cannot satisfy this wait.
    bool flushWait(uint64_t scratch, uint32_t code) {
        if (!scratch || (scratch & 3)) { ok_ = false; return false; }
        idle();
        const uint32_t sequence = ++flushSeq_;
        const uint32_t payload[4] = {code, uint32_t(scratch), uint32_t(scratch >> 32), sequence};
        p(0x46, payload, 4);
        const uint32_t wait[6] = {0x13, uint32_t(scratch), uint32_t(scratch >> 32),
                                  sequence, 0xffffffff, 16};
        p(0x3c, wait, 6);
        idle();
        return ok_;
    }

    // fd5 sysmem prep before a 2D operation: invalidate CCU color/depth after
    // any earlier flushes, then settle the pipe. Once per 2D group.
    bool begin2D() {
        event(25);
        event(24);
        idle();
        return ok_;
    }

    // One CP_BLIT R32_UINT solid fill into a linear image. pitch/address are in
    // bytes; width/height are in pixels. The destination must stay inside the
    // caller's validated range (see eligible()).
    bool solidFill(uint64_t address, uint32_t width, uint32_t height, uint32_t pitch,
                   uint32_t color, uint64_t scratch) {
        if (!width || !height || !pitch || (address & 63) || (pitch & 63) ||
            (uint64_t(width) * 4 > pitch)) { ok_ = false; return false; }
        // Clear-state setup for the 2D path: bypass disable, blit mode, no
        // shader stages, and the linear destination format.
        r(REG_A5XX_RB_2D_BLIT_CNTL, 0);
        r(REG_A5XX_GRAS_2D_BLIT_CNTL, 0);
        r(REG_A5XX_UNKNOWN_2184, 1);
        r(REG_A5XX_RB_MODE_CNTL, 4);
        r(REG_A5XX_SP_MODE_CNTL, 12);
        r(REG_A5XX_TPL1_MODE_CNTL, 0x344);
        r(REG_A5XX_HLSQ_MODE_CNTL, 2);
        r(REG_A5XX_GRAS_CL_CNTL, 0x181);
        uint32_t mode = 5;
        p(0x6c, &mode, 1);  // BLIT2D
        const uint32_t values[4] = {color, 0, 0, 0};
        rr(REG_A5XX_RB_2D_SRC_SOLID_DW0, values, 4);
        const uint32_t format = 75;  // RB5_R32_UINT
        r(REG_A5XX_RB_2D_SRC_INFO, format);
        r(REG_A5XX_GRAS_2D_SRC_INFO, format);
        r(REG_A5XX_RB_2D_DST_INFO, format);
        r(REG_A5XX_GRAS_2D_DST_INFO, format);
        r(REG_A5XX_RB_2D_DST_LO, uint32_t(address));
        r(REG_A5XX_RB_2D_DST_HI, uint32_t(address >> 32));
        // The packed 2D pitch fields have a 64-byte unit; an odd stride sets
        // bit 16 and shifts every row by 4 MiB (sagit's measured behaviour).
        const uint32_t layerStride = (pitch * height + 127u) & ~127u;
        r(REG_A5XX_RB_2D_DST_SIZE,
          A5XX_RB_2D_DST_SIZE_PITCH(pitch) | A5XX_RB_2D_DST_SIZE_ARRAY_PITCH(layerStride));
        for (unsigned i = 4; i < 9; ++i) r(REG_A5XX_RB_2D_DST_INFO + i, 0);
        const uint32_t end = (width - 1) | ((height - 1) << 16);
        const uint32_t fill[5] = {0, 0, end, 0, end};
        p(0x2c, fill, 5);
        mode = 8;
        p(0x6c, &mode, 1);  // END2D
        idle();
        return flushWait(scratch, 29) && ok_;
    }

    // Full clear: the fd5 restore baseline, then one solid fill. Kept for a
    // caller that wants a self-contained stream.
    bool clearRows(uint64_t address, uint32_t width, uint32_t height, uint32_t pitch,
                   uint32_t color, uint64_t scratch) {
        return restore() && begin2D() &&
               solidFill(address, width, height, pitch, color, scratch);
    }
};

}  // namespace CheeseburgerGpu
