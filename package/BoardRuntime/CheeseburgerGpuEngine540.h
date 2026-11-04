// SPDX-License-Identifier: GPL-2.0-only
// A540 user-submission engine, ported from the pinned sagit Engine/Encoder
// (docs/upstream/reference/sagit/QcomGPU/AdrenoSubmit.h). The already verified
// startup in CheeseburgerGpuStart.h still owns reset/ME_INIT/GPMU/ZAP; this
// engine adopts the running CP and gives every user submission one model:
// ring space and wrap, 64-bit CP_INDIRECT_BUFFER, A5xx packets, the timestamp
// fence at fence+4, and a fail-closed timeout that retains every DMA object.
//
// Failure contract, as in sagit: a submission that cannot be proven complete
// marks the engine broken and leaves the ring, the IB and the fence untouched
// for the rest of the boot. No later submit, BO release or mapping update may
// assume the CP is idle after that.
#pragma once
#include <stdint.h>
#include "CheeseburgerGpuStart.h"

namespace CheeseburgerGpu {

struct Ib540 { uint64_t va; uint32_t dwords; };

struct SubmitRecord {
    uint32_t stage = 0;      // 1 cursor, 2 space, 3 encode, 4 kick, 5 fence, 6 done
    uint32_t seq = 0;
    uint32_t rptr = 0;       // fence page +0: the CP's RPTR shadow
    uint32_t wptr = 0;       // producer cursor after this stream
    uint32_t fence = 0;      // fence page +4: the completion timestamp
    uint32_t words = 0;      // ring dwords this stream occupied
    bool wrapped = false;
    bool fenced = false;
    bool timedOut = false;
    bool broken = false;
};

// The last few streams as the CP reads them (same record sheng keeps in
// NativeAdrenoRing): [slot] seq, start, end, count, then the ring words.
extern "C" {
volatile uint32_t native_adreno_ring_log540[4][4 + 64];
volatile uint32_t native_adreno_ring_log540_count;
}

// Sagit Encoder, restricted to the packets this board emits. Every count and
// opcode carries odd parity; a buffer too small to hold a packet is fatal to
// the encoding (the caller must not kick a partial stream).
class Encoder540 {
    uint32_t *w;
    uint32_t cap, n = 0;
public:
    Encoder540(uint32_t *out, uint32_t capacity) : w(out), cap(capacity) {}
    uint32_t size() const { return n; }
    bool packet(uint32_t op, const uint32_t *data, uint32_t count) {
        if (count > 0x3fff || n + 1 + count > cap) return false;
        w[n++] = packet7(op, count);
        for (uint32_t i = 0; i < count; ++i) w[n++] = data[i];
        return true;
    }
    bool barrier() {
        return packet(0x12, nullptr, 0) && packet(0x26, nullptr, 0);
    }
    bool indirect(uint64_t va, uint32_t dwords) {
        const uint32_t d[3] = {uint32_t(va), uint32_t(va >> 32), dwords};
        return packet(0x3f, d, 3);
    }
    // One CP_MEM_WRITE of consecutive dwords to a 64-bit GPU address.
    bool memWrite(uint64_t address, const uint32_t *values, uint32_t count) {
        if (!count || count > 0x3ff0 || n + 3 + count > cap) return false;
        w[n++] = packet7(0x3d, count + 2);
        w[n++] = uint32_t(address); w[n++] = uint32_t(address >> 32);
        for (uint32_t i = 0; i < count; ++i) w[n++] = values[i];
        return true;
    }
    // A5xx CP_MEM_TO_MEM: one 32-bit load/store with wait-for-writes. Used to
    // read a page the CP has never fetched without writing user memory.
    bool memToMem(uint64_t destination, uint64_t source) {
        const uint32_t d[5] = {1u << 30, uint32_t(destination), uint32_t(destination >> 32),
            uint32_t(source), uint32_t(source >> 32)};
        return packet(0x73, d, 5);
    }
    // CACHE_FLUSH_TS with IRQ. The timestamp lives at fence+4; +0 is the CP's
    // RPTR shadow (sheng NativeAdrenoRing::emitFence).
    bool fence(uint64_t fenceVA, uint32_t sequence) {
        const uint64_t at = fenceVA + 4;
        const uint32_t d[4] = {4u | (1u << 31), uint32_t(at), uint32_t(at >> 32), sequence};
        return packet(0x46, d, 4);
    }
    // Sagit Encoder::pageTable: kernel-only A5xx SMMU transition, never a
    // vendor opcode. Only emitted when the caller has a programmed TTBR.
    bool pageTable(uint64_t ttbr) {
        const uint32_t zero = 0, one = 1;
        const uint32_t table[3] = {uint32_t(ttbr), uint32_t(ttbr >> 32), 0};
        const uint32_t invalidate[3] = {0x40000, 1, 0};
        return barrier() && packet(0x5f, &zero, 1) && reg(0x831, 1) &&
            packet(0x13, nullptr, 0) && packet(0x53, table, 3) &&
            packet(0x43, invalidate, 3) && reg(0x831, 0) &&
            packet(0x5f, &one, 1) && packet(0x13, nullptr, 0);
    }
    bool reg(uint32_t address, uint32_t value) {
        if (address > 0x3ffff || n + 2 > cap) return false;
        w[n++] = 0x40000000u | 1u | (parity(1) << 7) | (address << 8) | (parity(address) << 27);
        w[n++] = value;
        return true;
    }
};

template <class R> class Engine540 {
    R &r;
    volatile uint32_t *ring = nullptr, *fence = nullptr;
    uint64_t fenceVA = 0;
    uint32_t ringDwords = 0;
    uint32_t read = 0, write = 0, issued = 0, completed = 0;
    bool online = false, broken = false;
    static constexpr uint32_t MaxIbs = 16;
    static constexpr uint32_t StageDwords = 4 * MaxIbs + 64;

    uint32_t mask() const { return ringDwords - 1; }
    uint32_t used() const { return (write - read) & mask(); }
    void put(uint32_t value) { ring[write] = value; write = (write + 1) & mask(); }

    void recordStream(uint32_t seq, uint32_t start, uint32_t count, const uint32_t *words) {
        const uint32_t slot = native_adreno_ring_log540_count & 3;
        volatile uint32_t *rec = native_adreno_ring_log540[slot];
        rec[0] = seq; rec[1] = start; rec[2] = write; rec[3] = count;
        for (uint32_t i = 0; i < 64; ++i) rec[4 + i] = i < count ? words[i] : 0;
        native_adreno_ring_log540_count = native_adreno_ring_log540_count + 1;
    }

public:
    explicit Engine540(R &registers) : r(registers) {}
    Engine540(const Engine540 &) = delete;
    Engine540 &operator=(const Engine540 &) = delete;

    bool ready() const { return online && !broken; }
    bool halted() const { return broken; }
    uint32_t lastIssued() const { return issued; }
    uint32_t lastCompleted() const { return completed; }
    void quarantine() { broken = true; }

    // Take over the ring the startup already proved with its own fences.
    // lastSequence continues the CPU sequence so a later fence cannot reuse a
    // number the probe already consumed.
    bool adopt(volatile uint32_t *ringCPU, uint32_t dwords, uint64_t ringVA_,
               volatile uint32_t *fenceCPU, uint64_t fenceVA_, uint32_t lastSequence) {
        if (online || broken || !ringCPU || !fenceCPU || !dwords ||
            (dwords & (dwords - 1)) || (ringVA_ & 4095) || (fenceVA_ & 4095)) return false;
        const uint32_t hw = r.read(RBWrite);
        if (hw >= dwords) return false;
        ring = ringCPU; ringDwords = dwords;
        fence = fenceCPU; fenceVA = fenceVA_;
        write = read = hw;
        issued = completed = lastSequence;
        online = true;
        return true;
    }

    // One submission: optional trusted prelude IB, the caller's IBs, a
    // barrier and the completion timestamp. Returns 0 only after the CP wrote
    // this sequence into fence+4.
    int submit(const Ib540 *ibs, uint32_t count, uint32_t sequence,
               uint64_t timeoutUs, SubmitRecord &rec,
               uint64_t ttbr = 0, const Ib540 *prelude = nullptr) {
        rec = SubmitRecord();
        rec.seq = sequence;
        if (!ready()) { rec.stage = 1; return -1; }
        if (!sequence || sequence <= issued || count > MaxIbs || (count && !ibs) ||
            (prelude && (!prelude->va || (prelude->va & 3) || !prelude->dwords || prelude->dwords > 0x3fff))) {
            rec.stage = 1; return -1;
        }
        for (uint32_t i = 0; i < count; ++i)
            if (!ibs[i].va || (ibs[i].va & 3) || !ibs[i].dwords || ibs[i].dwords > 0x3fff) {
                rec.stage = 1; return -1;
            }
        // The CPU has waited for every earlier fence, so the consumer cursor is
        // the only authority for free space. A cursor outside the ring or ahead
        // of the producer means the state is not the one this engine left.
        const uint32_t hw = r.read(RBRead);
        if (hw >= ringDwords || hw != read) { broken = true; rec.stage = 1; rec.broken = true; return -2; }

        uint32_t staged[StageDwords];
        Encoder540 e(staged, StageDwords);
        bool ok = e.barrier();
        if (ok && ttbr) ok = e.pageTable(ttbr);
        if (ok && prelude) ok = e.indirect(prelude->va, prelude->dwords);
        for (uint32_t i = 0; ok && i < count; ++i) ok = e.indirect(ibs[i].va, ibs[i].dwords);
        if (ok) ok = e.barrier();
        if (ok) ok = e.fence(fenceVA, sequence);
        if (!ok) { rec.stage = 3; return -3; }
        const uint32_t n = e.size();
        if (n > ringDwords) { rec.stage = 2; return -3; }
        // Keep one dword of slack so a full ring is never presented as empty.
        if (((read - write - 1) & mask()) < n) { rec.stage = 2; return -4; }

        const uint32_t start = write;
        if (write + n > ringDwords) {
            // A packet may not cross the wrap: NOP-fill the tail and restart.
            while (write != 0) put(packet7(0x10, 0));
            rec.wrapped = true;
        }
        for (uint32_t i = 0; i < n; ++i) put(staged[i]);
        rec.words = n;
        rec.wptr = write;
        // The CP's own record of the completion timestamp, then the whole ring
        // (a stream may wrap), exactly like sheng NativeAdrenoRing::submitWords.
        fence[1] = 0;
        r.clean((volatile void *)fence, 8);
        recordStream(sequence, start, n, staged);
        r.clean((volatile void *)ring, ringDwords * 4);
        r.publish();
        r.write(RBWrite, write);
        rec.stage = 4;

        for (uint32_t waited = 0; waited < timeoutUs; waited += 10) {
            r.clean((volatile void *)fence, 8);
            __asm__ volatile("dsb sy" ::: "memory");
            const uint32_t value = fence[1];
            rec.fence = value;
            if (value > issued + 1) { broken = true; rec.stage = 5; rec.broken = true; return -5; }
            if (value == sequence) {
                __asm__ volatile("dmb sy" ::: "memory");
                rec.rptr = fence[0];
                completed = issued = sequence;
                // The timestamp is the last packet of the stream, so a proven
                // fence also proves the CP consumed it: the next producer
                // cursor starts at the end of this stream.
                read = write;
                rec.fenced = true; rec.stage = 6;
                return 0;
            }
            r.delay(10);
        }
        // The CP did not report this stream. Do not read the GPU bus again and
        // do not release anything: the ring, its IBs and any mapping they cite
        // stay owned for the rest of the boot.
        rec.rptr = fence[0];
        rec.timedOut = true; rec.broken = true; rec.stage = 5;
        broken = true;
        return -4;
    }
};

}  // namespace CheeseburgerGpu
