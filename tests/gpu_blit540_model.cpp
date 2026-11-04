// SPDX-License-Identifier: GPL-2.0-only
// Host model for CheeseburgerGpu::Blit540: parses the encoded A5xx stream and
// checks the register values, the BLIT2D packet pair, the R32_UINT solid fill
// bounds, and the color-flush wait. No hardware is touched.
#include "../package/BoardRuntime/CheeseburgerGpuBlit540.h"

#include <cstdio>
#include <cstring>
#include <map>

using namespace CheeseburgerGpu;

namespace {

struct Decoded {
    std::map<uint32_t, uint32_t> regs;
    uint32_t blit2d = 0, end2d = 0, fillPackets = 0, copyPackets = 0, flushPackets = 0, waits = 0;
    uint32_t copyEnd = 0, srcPitch = 0, dstPitch = 0;
    uint32_t fillEnd = 0;
    uint32_t dstSize = 0, solid = 0, dstLo = 0, dstHi = 0;
    uint32_t restoreMode = 0, invalidate = 0;
    bool bad = false;
};

bool decode(const uint32_t *w, uint32_t n, Decoded &d) {
    uint32_t i = 0;
    while (i < n) {
        const uint32_t h = w[i++];
        if ((h & 0xf0000000u) == 0x40000000u) {
            const uint32_t count = h & 0x3f;
            const uint32_t addr = (h >> 8) & 0x3ffff;
            if (!count || i + count > n) return false;
            for (uint32_t k = 0; k < count; ++k) d.regs[addr + k] = w[i + k];
            i += count;
            continue;
        }
        if ((h & 0xff000000u) != 0x70000000u) return false;
        const uint32_t count = h & 0x3fff;
        const uint32_t op = (h >> 16) & 0x7f;  // bit 23 is the opcode parity
        if (i + count > n) return false;
        const uint32_t *body = w + i;
        if (op == 0x6c && count == 1) {
            if (body[0] == 5) d.blit2d++;
            if (body[0] == 8) d.end2d++;
            if (count == 1 && body[0] == 1) d.restoreMode = 1;
        }
        if (op == 0x2c && count == 5) {
            if (body[0] == 0) { ++d.fillPackets; d.fillEnd = body[2]; }
            else { ++d.copyPackets; d.copyEnd = body[2]; }
        }
        if (op == 0x46 && count == 4) ++d.flushPackets;
        if (op == 0x3c && count == 6) ++d.waits;
        i += count;
    }
    return true;
}

int failures = 0;
void expect(bool condition, const char *what) {
    if (!condition) { std::printf("FAIL %s\n", what); ++failures; }
}

}  // namespace

int main() {
    constexpr uint32_t Words = 4096;
    static uint32_t stream[Words] = {};
    Encoder540 e(stream, Words);
    Blit540 b(e);
    const uint64_t target = 0x2060000, scratch = 0x2050000;
    const uint32_t pitch = 1024, color = 0xff203040u;

    expect(b.restore(), "restore encodes");
    expect(b.begin2D(), "begin2D encodes");
    expect(b.solidFill(target, 128, 32, pitch, color, scratch), "fill0 encodes");
    expect(b.solidFill(target + 128 * 4, 128, 32, pitch, 0xff405060u, scratch), "fill1 encodes");
    expect(b.solidFill(target + 32 * pitch, 128, 32, pitch, 0xff607080u, scratch), "fill2 encodes");
    expect(b.solidFill(target + 32 * pitch + 128 * 4, 128, 32, pitch, 0xffa0b0c0u, scratch), "fill3 encodes");
    expect(b.words() < Words, "stream fits");

    Decoded d;
    expect(decode(stream, e.size(), d), "stream decodes");

    // Restore baseline.
    expect(d.regs[REG_A5XX_SP_ADDR_MODE_CNTL] == 1, "SP addr mode 64-bit");
    expect(d.regs[REG_A5XX_RB_ADDR_MODE_CNTL] == 1, "RB addr mode 64-bit");
    expect(d.regs[REG_A5XX_TPL1_ADDR_MODE_CNTL] == 1, "TPL1 addr mode 64-bit");
    expect(d.regs[REG_A5XX_PC_POWER_CNTL] == 3, "four SP cores");
    expect(d.regs[REG_A5XX_RB_CNTL] == A5XX_RB_CNTL_BYPASS, "GMEM bypass");
    expect(d.regs[REG_A5XX_RB_CCU_CNTL] == 0x10000000u, "CCU default");
    expect(d.regs[REG_A5XX_UCHE_CACHE_INVALIDATE] == 0x12, "UCHE invalidate");
    expect(d.regs[REG_A5XX_RB_MODE_CNTL] == 4, "2D RB mode");
    expect(d.regs[REG_A5XX_SP_MODE_CNTL] == 12, "2D SP mode");
    expect(d.regs[REG_A5XX_TPL1_MODE_CNTL] == 0x344, "2D TPL1 mode");
    expect(d.regs[REG_A5XX_HLSQ_MODE_CNTL] == 2, "2D HLSQ mode");
    expect(d.regs[REG_A5XX_GRAS_CL_CNTL] == 0x181, "2D GRAS CL");
    expect(d.blit2d == 4 && d.end2d == 4, "BLIT2D/END2D per fill");
    expect(d.fillPackets == 4, "four fill packets");
    expect(d.flushPackets == 4 && d.waits == 4, "flush and wait per fill");
    expect(d.fillEnd == ((128 - 1) | ((32 - 1) << 16)), "fill bounds");
    const uint32_t layer = (pitch * 32 + 127u) & ~127u;
    expect(d.regs[REG_A5XX_RB_2D_DST_SIZE] ==
               (A5XX_RB_2D_DST_SIZE_PITCH(pitch) | A5XX_RB_2D_DST_SIZE_ARRAY_PITCH(layer)),
           "dst size pitch/array pitch");
    expect(d.regs[REG_A5XX_RB_2D_DST_LO] == uint32_t(target + 32 * pitch + 128 * 4), "dst lo");
    expect(d.regs[REG_A5XX_RB_2D_DST_HI] == 0, "dst hi");
    expect(d.regs[REG_A5XX_RB_2D_DST_INFO] == 75, "R32_UINT destination");
    expect(d.regs[REG_A5XX_RB_2D_SRC_SOLID_DW0] == 0xffa0b0c0u, "last solid color");

    // Copy path (sagit snapshot2D): separate source/destination pitch, R32
    // format, sourced CP_BLIT and a single color flush for the whole group.
    static uint32_t copyStream[2048] = {};
    Encoder540 ce(copyStream, 2048);
    Blit540 cb(ce);
    expect(cb.restore() && cb.begin2D() && cb.copySetup(), "copy setup encodes");
    expect(cb.copyTile(target, 1024, 0x2100000, 4992, 256, 64), "copy tile 0");
    expect(cb.copyTile(target, 1024, 0x2100000 + 1024, 4992, 256, 64), "copy tile 1");
    expect(cb.copyEnd(scratch), "copy end encodes");
    Decoded cd;
    expect(decode(copyStream, ce.size(), cd), "copy stream decodes");
    expect(cd.copyPackets == 2 && cd.fillPackets == 0, "two sourced copies");
    expect(cd.copyEnd == ((256 - 1) | ((64 - 1) << 16)), "copy bounds");
    expect(cd.regs[REG_A5XX_RB_2D_BLIT_CNTL] == 0x86000000u, "copy RB 2D control");
    expect(cd.regs[REG_A5XX_UNKNOWN_2184] == 9, "copy UNKNOWN_2184");
    expect(cd.regs[REG_A5XX_RB_2D_SRC_INFO] == 75, "copy source R32_UINT");
    expect(cd.regs[REG_A5XX_RB_2D_DST_INFO] == 75, "copy dest R32_UINT");
    expect(cd.regs[REG_A5XX_RB_2D_SRC_SIZE] ==
               (A5XX_RB_2D_SRC_SIZE_PITCH(1024) | A5XX_RB_2D_SRC_SIZE_ARRAY_PITCH(1024 * 64)),
           "copy source pitch");
    expect(cd.regs[REG_A5XX_RB_2D_DST_SIZE] ==
               (A5XX_RB_2D_DST_SIZE_PITCH(4992) | A5XX_RB_2D_DST_SIZE_ARRAY_PITCH(4992 * 64)),
           "copy dest pitch");
    expect(cd.regs[REG_A5XX_RB_2D_SRC_LO] == uint32_t(target), "copy source address");
    expect(cd.regs[REG_A5XX_RB_2D_DST_LO] == 0x2100000 + 1024, "last copy dest address");
    expect(cd.flushPackets == 1 && cd.waits == 1, "one flush per copy group");

    // Fail-closed: a stream that does not fit must report failure, not a
    // truncated but "valid" stream.
    static uint32_t small[64] = {};
    Encoder540 es(small, 64);
    Blit540 bs(es);
    expect(!bs.restore(), "small buffer fails closed");
    expect(!bs.ok(), "status stays failed");

    // Full-screen frame stream (1088x1920 at stride 4352): the display
    // handoff packs four 480-row bands into one 2048-word IB.
    static uint32_t frame[2048] = {};
    Encoder540 fe(frame, 2048);
    Blit540 fb(fe);
    expect(fb.restore() && fb.begin2D(), "frame setup encodes");
    for (unsigned i = 0; i < 4; ++i)
        expect(fb.solidFill(0x2200000ull + uint64_t(i) * 480 * 4352, 1088, 480, 4352,
                            0xff2040c0u + i, scratch), "frame band encodes");
    expect(fe.size() < 2048, "frame stream fits the 2048-word IB");

    // Argument validation.
    static uint32_t more[256] = {};
    Encoder540 em(more, 256);
    Blit540 bm(em);
    expect(!bm.solidFill(0x2060001, 64, 16, 256, 0xffffffffu, scratch), "unaligned dest rejected");
    expect(!bm.solidFill(target, 128, 16, 256, 0xffffffffu, scratch), "row wider than pitch rejected");
    Encoder540 cc(more, 256);
    Blit540 bc(cc);
    expect(!bc.copyTile(target + 1, 1024, 0x2100000, 4992, 64, 16), "unaligned copy source rejected");
    expect(!bc.copyTile(target, 1024, 0x2100000, 4096, 256, 16), "copy wider than dest pitch rejected");

    if (failures) { std::printf("gpu_blit540_model: %d failures\n", failures); return 1; }
    std::printf("gpu_blit540_model: PASS words=%u\n", e.size());
    return 0;
}
