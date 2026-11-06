// SPDX-License-Identifier: GPL-2.0-only
// Controlled offline A540 compute subset, not a device, shader verifier or ABI.
// Adapted from ios_sagit 673f85070fe9f7872793015eb230a5c3987d4a8c:
// AdrenoPipeline::{dispatch,constants,buffers,shaderObject,loadShader},
// GPUShaderProtocol::{threadgroupFootprint,dispatchFootprint,bindingBytes,
// memoryConstantWords}, GPUJITProtocol::validDescription. No A740 commands.
#pragma once
#include "../BoardRuntime/CheeseburgerGpuBlit540.h"

namespace NativeMetal540 {
constexpr uint32_t VariableLocal = 4, BoundedBuffers = 8, Barrier = 32,
    DynamicShared = 64, DispatchGeometry = 128, ExactBufferBytes = 1u << 26;
constexpr uint32_t SupportedFlags = VariableLocal | BoundedBuffers | Barrier |
    DynamicShared | DispatchGeometry | ExactBufferBytes;
constexpr uint32_t MaxShared = 32768, MaxBuffer = 1u << 28;
constexpr uint64_t AddressLimit = 1ull << 36;

// Deliberately not the upstream wire structure. Exporter rejects every omitted
// feature in all 621 reflection words before constructing this typed subset.
struct ComputeInfo {
    uint32_t codeWords, instrlen, constlen, fullRegs, halfRegs, branchStack,
        threadSize, subgroupSize, immediateBase, immediateCount, driverBase,
        driverCount, localID, groupID, bufferMask, writeMask, flags, sharedBytes,
        threadgroupMask, strides[8];
};
struct Program { ComputeInfo info; const uint32_t *code, *immediates; };
struct Region { uint64_t address = 0, bytes = 0; };
struct Binding {
    Region allocation{};
    uint64_t offset = 0;
    uint32_t bytes = 0;
    bool writable = false;
};
struct Dispatch {
    uint32_t groups[3] = {1,1,1}, local[3] = {64,1,1};
    uint32_t threadgroupLengths[8] = {};
    // No explicit-grid/tail dispatch, tile/image/sampler state or raw PM4.
};
struct Resources { Region shader{}, scratch{}; Binding buffers[8]{}; };
struct Plan {
    uint32_t constants[512] = {}, descriptorBytes[16] = {};
    uint64_t addresses[16] = {};
    uint32_t constantWords = 0, sharedBytes = 0, dimensions = 1, cached = 0;
};
inline uint32_t memoryConstantWords(const ComputeInfo &p) {
    return p.flags & ExactBufferBytes ? 112 : p.flags & DispatchGeometry ? 80 :
        p.threadgroupMask ? 16 : 0;
}
inline bool validInfo(const ComputeInfo &p) {
    const uint32_t n = p.constlen * 4;
    if (!p.codeWords || p.codeWords > 4096 || (p.codeWords & 31) ||
        !p.instrlen || p.instrlen > 128 || p.codeWords < p.instrlen * 32 ||
        p.codeWords > (p.instrlen + 1) * 32 || p.constlen > 128 || (p.constlen & 3) ||
        !p.fullRegs || p.fullRegs > 63 || p.halfRegs > 63 || p.branchStack > 32 ||
        p.threadSize > 1 || (p.subgroupSize != 32 && p.subgroupSize != 64) ||
        p.immediateBase > n || p.immediateCount > n - p.immediateBase ||
        p.driverBase > n || p.driverCount > n - p.driverBase || p.driverCount > 16 ||
        p.bufferMask > 255 || (p.writeMask & ~p.bufferMask) ||
        (p.flags & ~SupportedFlags) || p.localID > 252 || p.groupID > 252 ||
        (p.localID != 252 && (p.localID + 3 > p.fullRegs * 4 || p.localID > 249)) ||
        p.sharedBytes > MaxShared || (p.sharedBytes & 3) || p.threadgroupMask > 255 ||
        bool(p.threadgroupMask) != bool(p.flags & DynamicShared) ||
        (p.threadgroupMask && (p.sharedBytes & 15)) ||
        ((p.flags & Barrier) && p.branchStack > 3) ||
        ((p.flags & ExactBufferBytes) && !(p.flags & BoundedBuffers))) return false;
    if (p.immediateCount && p.driverCount && p.immediateBase < p.driverBase + p.driverCount &&
        p.driverBase < p.immediateBase + p.immediateCount) return false;
    const uint32_t prefix = memoryConstantWords(p);
    if (prefix > n || (p.immediateCount && p.immediateBase < prefix) ||
        (p.driverCount && p.driverBase < prefix)) return false;
    for (unsigned i = 0; i < 8; ++i) {
        const uint32_t s = p.strides[i];
        if (p.bufferMask & (1u << i)) {
            if (s != 2 && s != 4 && s != 8 && s != 16) return false;
        } else if (s) return false;
    }
    return true;
}
inline bool range(const Region &r) {
    return r.address && r.address < AddressLimit && r.bytes && r.bytes <= MaxBuffer &&
        r.bytes <= AddressLimit - r.address;
}
inline bool overlaps(const Region &a, const Region &b) {
    return a.address < b.address + b.bytes && b.address < a.address + a.bytes;
}
inline bool plan(const Program &program, const Dispatch &d, const Resources &r, Plan &out) {
    out = {};
    const auto &p = program.info;
    if (!validInfo(p) || !program.code || (p.immediateCount && !program.immediates) ||
        !range(r.shader) || (r.shader.address & 127) || r.shader.bytes < uint64_t(p.codeWords) * 4 ||
        !range(r.scratch) || (r.scratch.address & 3) || r.scratch.bytes < 4 ||
        overlaps(r.shader, r.scratch)) return false;
    uint64_t threads = 1, invocations = 1;
    for (unsigned i = 0; i < 3; ++i) {
        if (!d.local[i] || d.local[i] > (i == 2 ? 64u : 1024u) ||
            !d.groups[i] || d.groups[i] > 65535 || threads > 1024 / d.local[i]) return false;
        threads *= d.local[i];
        const uint64_t size = uint64_t(d.local[i]) * d.groups[i];
        if (invocations > 16777216 / size) return false;
        invocations *= size;
        if (i && size > 1) out.dimensions = i + 1;
    }
    if (!(p.flags & VariableLocal) && (d.local[0] != 64 || d.local[1] != 1 ||
        d.local[2] != 1 || d.groups[1] != 1 || d.groups[2] != 1)) return false;
    out.sharedBytes = p.sharedBytes;
    for (unsigned i = 0; i < 8; ++i) {
        const uint32_t length = d.threadgroupLengths[i];
        if (!(p.threadgroupMask & (1u << i))) { if (length) return false; continue; }
        if (!length || (length & 15) || length > MaxShared - out.sharedBytes) return false;
        out.constants[i * 2] = out.sharedBytes - p.sharedBytes;
        out.constants[i * 2 + 1] = length;
        out.sharedBytes += length;
    }
    Region active[8]{};
    for (unsigned i = 0; i < 8; ++i) {
        const auto &b = r.buffers[i];
        if (!(p.bufferMask & (1u << i))) {
            if (b.allocation.address || b.allocation.bytes || b.offset || b.bytes || b.writable) return false;
            continue;
        }
        const uint64_t required = uint64_t(p.strides[i]) *
            ((p.flags & BoundedBuffers) ? 1 : uint64_t(d.groups[0]) * d.local[0]);
        if (!range(b.allocation) || b.offset > b.allocation.bytes || !b.bytes ||
            b.bytes > b.allocation.bytes - b.offset || b.bytes < required ||
            (b.offset & (p.strides[i] - 1)) ||
            ((b.allocation.address + b.offset) & (p.strides[i] - 1)) ||
            ((p.writeMask & (1u << i)) && !b.writable)) return false;
        active[i] = {b.allocation.address + b.offset, b.bytes};
        if (overlaps(active[i], r.shader) || overlaps(active[i], r.scratch)) return false;
        for (unsigned j = 0; j < i; ++j)
            if (active[j].bytes && overlaps(active[i], active[j]) &&
                (p.writeMask & ((1u << i) | (1u << j)))) return false;
        out.addresses[i] = active[i].address;
        out.descriptorBytes[i] = b.bytes;
        if (!(b.bytes & 65535)) {
            // sagit exact native bounds only: HW extent +1, shader length unchanged.
            if (!(p.flags & ExactBufferBytes)) return false;
            ++out.descriptorBytes[i];
        }
        out.constants[96 + i] = b.bytes;
    }
    for (unsigned i = 0; i < 3; ++i) {
        out.constants[64 + 8 + i] = d.groups[i] * d.local[i];
        out.constants[64 + 12 + i] = d.groups[i];
    }
    out.constants[67] = d.local[0]; out.constants[71] = d.local[1]; out.constants[75] = d.local[2];
    uint32_t driver[16] = {d.groups[0], d.groups[1], d.groups[2], out.dimensions,
        0,0,0,p.subgroupSize,d.local[0],d.local[1],d.local[2],p.subgroupSize == 64 ? 6u : 5u,0,0,0,0};
    // Only the selected prefix is visible. Do not leak geometry/size constants
    // into a driver/immediate span for a smaller memory ABI.
    for (uint32_t i = memoryConstantWords(p); i < 512; ++i) out.constants[i] = 0;
    for (uint32_t i = 0; i < p.immediateCount; ++i)
        out.constants[p.immediateBase + i] = program.immediates[i];
    for (uint32_t i = 0; i < p.driverCount; ++i)
        out.constants[p.driverBase + i] = driver[i];
    out.constantWords = p.constlen * 4;
    out.cached = p.instrlen <= 32 ? p.instrlen : 0;
    return true;
}

// Userland-only: staged stream, plan and constant packet use >12 KiB of stack;
// never call this from the kernel or a panic context. All-or-nothing CPU
// encoding: nothing is uploaded/submitted. validInfo() is callable without the
// exporter, so the owner MUST hash-attest an immutable compiler-owned ISA and
// metadata snapshot before using either entry point; hashes are not signatures.
// Upload the EXACT exported ISA to retained read-only shader mappings, attest
// physical disjointness (IOVA checks cannot detect imported aliases), serialize
// BO lifetime, zero scratch, and use existing client submit/fence/cache rules.
// Structural metadata admission is NOT IR3 instruction/security validation.
inline bool encode(const Program &program, const Dispatch &d, const Resources &resources,
                   uint32_t *words, uint32_t capacity, uint32_t &written,bool directShader=false) {
    written = 0;
    Plan q;
    if (!words || !plan(program, d, resources, q)) return false;
    uint32_t stage[2048] = {};
    CheeseburgerGpu::Encoder540 e(stage, 2048);
    CheeseburgerGpu::Blit540 base(e);
    if (!base.restore()) return false;
    bool ok = true;
    auto r = [&](uint32_t a, uint32_t v) { if (ok && !e.reg(a, v)) ok = false; };
    auto packet = [&](uint32_t op, const uint32_t *v, uint32_t n) {
        if (ok && !e.packet(op, v, n)) ok = false;
    };
    const auto &p = program.info;
    r(REG_A5XX_SP_SP_CNTL, 0);
    r(REG_A5XX_HLSQ_CONTROL_0_REG, A5XX_HLSQ_CONTROL_0_REG_CSTHREADSIZE(p.threadSize) | 0x880);
    r(REG_A5XX_SP_VS_CONFIG, 0); r(REG_A5XX_SP_FS_CONFIG, 0);
    r(REG_A5XX_HLSQ_VS_CONFIG, 0); r(REG_A5XX_HLSQ_FS_CONFIG, 0);
    r(REG_A5XX_HLSQ_VS_CONSTLEN, 0); r(REG_A5XX_HLSQ_FS_CONSTLEN, 0);
    r(REG_A5XX_HLSQ_VS_INSTRLEN, 0); r(REG_A5XX_HLSQ_FS_INSTRLEN, 0);
    r(REG_A5XX_HLSQ_CS_CONFIG, A5XX_HLSQ_CS_CONFIG_ENABLED);
    r(REG_A5XX_SP_CS_CONFIG, A5XX_SP_CS_CONFIG_ENABLED);
    r(REG_A5XX_SP_CS_CTRL_REG0, 2 | (q.cached ? A5XX_SP_CS_CTRL_REG0_BUFFER : 0) |
        A5XX_SP_CS_CTRL_REG0_THREADSIZE(p.threadSize) |
        A5XX_SP_CS_CTRL_REG0_FULLREGFOOTPRINT(p.fullRegs) |
        A5XX_SP_CS_CTRL_REG0_HALFREGFOOTPRINT(p.halfRegs) |
        A5XX_SP_CS_CTRL_REG0_BRANCHSTACK(p.branchStack));
    r(REG_A5XX_HLSQ_CS_CNTL, A5XX_HLSQ_CS_CNTL_INSTRLEN(q.cached) |
        (p.bufferMask ? A5XX_HLSQ_CS_CNTL_SSBO_ENABLE : 0));
    r(REG_A5XX_HLSQ_CS_CONSTLEN, p.constlen / 4);
    r(REG_A5XX_HLSQ_CS_INSTRLEN, q.cached);
    const uint32_t object[]={uint32_t(resources.shader.address),uint32_t(resources.shader.address>>32),0,0,0,0};
    if(!e.regs(REG_A5XX_SP_CS_OBJ_START,object,6))ok=false;
    r(REG_A5XX_HLSQ_UPDATE_CNTL, 0x1f00000);
    r(REG_A5XX_HLSQ_CS_CNTL_0, A5XX_HLSQ_CS_CNTL_0_WGIDCONSTID(p.groupID) |
        A5XX_HLSQ_CS_CNTL_0_UNK0(252) | A5XX_HLSQ_CS_CNTL_0_UNK1(252) |
        A5XX_HLSQ_CS_CNTL_0_LOCALIDREGID(p.localID));
    r(REG_A5XX_HLSQ_CS_CNTL_1, 1); // A5xx fixed local store, no A6xx SHARED_SIZE.
    if (q.cached) {
        const uint32_t load[] = {(2u << 16) | (13u << 18) | (q.cached << 22),
            uint32_t(resources.shader.address), uint32_t(resources.shader.address >> 32)};
        if(directShader){
            if(p.codeWords>512)return false;
            uint32_t direct[515]={};direct[0]=(13u<<18)|(q.cached<<22);
            for(unsigned i=0;i<p.codeWords;++i)direct[3+i]=program.code[i];
            packet(0x30,direct,p.codeWords+3);
        }else packet(0x30, load, 3);
    }
    if (p.bufferMask) {
        const uint32_t descriptorCount=32u-__builtin_clz(p.bufferMask);
        uint32_t values[35] = {(15u << 18) | (descriptorCount << 22), 1, 0};
        for (unsigned i = 0; i < 16; ++i) {
            values[3 + i*2] = A5XX_SSBO_1_0_WIDTH(q.descriptorBytes[i] & 65535);
            values[4 + i*2] = A5XX_SSBO_1_1_HEIGHT(q.descriptorBytes[i] >> 16);
        }
        packet(0x30, values, 3+descriptorCount*2); values[1] = 2;
        for (unsigned i = 0; i < 16; ++i) {
            values[3 + i*2] = uint32_t(q.addresses[i]);
            values[4 + i*2] = uint32_t(q.addresses[i] >> 32);
        }
        packet(0x30, values, 3+descriptorCount*2);
    }
    r(REG_A5XX_TPL1_CS_TEX_COUNT, 0);
    if (q.constantWords) {
        uint32_t values[515] = {(13u << 18) | (p.constlen << 22), 1, 0};
        for (uint32_t i = 0; i < q.constantWords; ++i) values[3 + i] = q.constants[i];
        packet(0x30, values, q.constantWords + 3);
    }
    r(REG_A5XX_HLSQ_CS_NDRANGE_0, A5XX_HLSQ_CS_NDRANGE_0_KERNELDIM(q.dimensions) |
        A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEX(d.local[0]-1) |
        A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEY(d.local[1]-1) |
        A5XX_HLSQ_CS_NDRANGE_0_LOCALSIZEZ(d.local[2]-1));
    for (unsigned i = 0; i < 3; ++i) {
        r(REG_A5XX_HLSQ_CS_NDRANGE_1 + i*2, d.groups[i] * d.local[i]);
        r(REG_A5XX_HLSQ_CS_NDRANGE_2 + i*2, 0);
        r(REG_A5XX_HLSQ_CS_KERNEL_GROUP_X + i, 1);
    }
    const uint32_t exec[] = {0, d.groups[0], d.groups[1], d.groups[2]};
    packet(0x33, exec, 4);
    if (!ok || !base.flushWait(resources.scratch.address, 4) || e.size() > capacity) return false;
    for (uint32_t i = 0; i < e.size(); ++i) words[i] = stage[i];
    written = e.size();
    return true;
}
} // namespace NativeMetal540
