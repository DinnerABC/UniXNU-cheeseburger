// SPDX-License-Identifier: GPL-2.0-only
// A540 CP reset/configure/start, ported from the pinned sagit AdrenoStartup.h
// (Linux v6.12 a5xx_gpu.c). The state machine is parameterised on a register
// accessor so the kernel can use MMIO and a host test can inject every failure
// point. Register offsets are byte offsets into the GPU aperture.
//
// Failure contract: every failure leaves the CP and VBIF halted, marks the
// attempt abandoned and never unhalts or submits again. Host tests and the
// kernel diagnostics rely on this; do not weaken it.
#pragma once
#include <stdint.h>

namespace CheeseburgerGpu {

enum : uint32_t {
    RBBase = 0x800 * 4, RBControl = 0x802 * 4, RBReadAddr = 0x804 * 4, RBReadAddrHi = 0x805 * 4,
    RBRead = 0x806 * 4, RBWrite = 0x807 * 4,
    MEControl = 0x832 * 4, PFPBase = 0x835 * 4, MEBase = 0x838 * 4, ContextSwitch = 0x83b * 4,
    CPInterruptStatus = 0xb18 * 4, InterruptClear = 0x37 * 4, InterruptMask = 0x38 * 4,
    InterruptStatus = 0x4e1 * 4, Status = 0x4f5 * 4, Status3 = 0x530 * 4,
    TrustControl = 0xf400 * 4, GPMUReady = 0xa8e0 * 4, GPMUError = 0xa8e1 * 4,
    VBIFHalt = 0x3080 * 4, VBIFHalted = 0x3081 * 4, RBBMSwReset = 0x43 * 4,
    GPMUCM3SysReset = 0xa8d8 * 4,
    // A5xx GPMU bring-up registers (sagit AdrenoStartup.h, dword -> byte).
    GPMUVoltage = 0xac60 * 4, GPMUPwrThreshold = 0xac80 * 4,
    GPMUPwrColInterFrameCtrl = 0xa892 * 4, GPMUPwrColBinningCtrl = 0xa894 * 4,
    GPMUPwrColInterFrameHyst = 0xa893 * 4, GPMUPwrColStaggerDelay = 0xa891 * 4,
    CPAddrModeCntl = 0x860 * 4, CPProtectCntl = 0x8a0 * 4, CPHwFault = 0xb1a * 4,
    CPProtectStatus = 0xb1c * 4, CPAhbFault = 0xb1b * 4, CPControl = 0x831 * 4,
    Halt = 0x303u,
    // Vendor A5XX_CP_RB_CNTL_DEFAULT: 32 KiB / 32-byte block, RPTR shadow
    // enabled at CP_RB_RPTR_ADDR. Linux additionally sets NO_UPDATE and skips
    // the scratch; the device's vendor driver always provides the scratch, so
    // follow it and keep the CP from writing its cursor to an unmapped address.
    RingControl = 12 | (2 << 8),
    ErrorIRQs = 0x7e | (1u << 9) | (1u << 23) | (1u << 24) | (1u << 25) | (1u << 28),
    OwnedIRQs = (1u << 20) | 0x7e | (1u << 9) | (1u << 23) | (1u << 24) | (1u << 25) | (1u << 28),
    RingDwords = 8192
};

inline uint32_t parity(uint32_t v) {
    v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (~v) & 1;
}
inline uint32_t packet7(uint32_t op, uint32_t n) {
    return 0x70000000u | n | (parity(n) << 15) | (op << 16) | (parity(op) << 23);
}
inline uint32_t protect(uint32_t base, unsigned log2) {
    return 0x60000000u | (uint32_t(log2) << 24) | (base << 2);
}
// Halted-pipeline mask used before the first start (sagit haltedPipelineIdle).
inline bool idleStatus(uint32_t s) { return !(s & ~0x800001c1u); }

struct Record {
    unsigned stage = 0;   // 1 reset done, 2 addresses/configure done, 3 kicked, 4 running
    int rc = 0;
    // Registers sampled only while the bus was known healthy. After an error
    // IRQ or a kick timeout the fields not yet sampled hold 0xffffffff and
    // postRead stays false: they are "not read", never a zeroed observation.
    uint32_t vbif = 0, me = 0, status = 0, retries = 0;
    uint32_t read = 0, write = 0, irq = 0, cpIrq = 0, status3 = 0;
    uint32_t trust = 0, cpControl = 0;
    // Interrupt latches sampled before the user submission's doorbell. A stale
    // RBBM bit is cleared like sagit does; a latched CP fault refuses the kick.
    uint32_t preIrq = 0, preCpIrq = 0;
    bool fenced = false;     // the fence page held the sequence despite the rc
    bool abandoned = false;
    bool postRead = false;   // a post-failure bus read happened (must stay false)
    bool converged = false;  // posted halt writes were issued (not proof of stop)
};

template <class R> class Startup {
    R &r;
    bool resetDone = false, abandonedFlag = false, runningFlag = false, haltAck = false;
    volatile uint32_t *ringPtr = nullptr;
    unsigned ringCursor = 0;

    void wr(uint32_t off, uint32_t value) { r.write(off, value); }
    uint32_t rd(uint32_t off) { return r.read(off); }

    void haltBestEffort() {
        // Posted writes only: a failed kick can leave the GPU AHB stalled, and
        // the trailing DSB of a normal write is what turns cleanup into an
        // unbounded wait. The halt still takes effect; the caller observes it
        // from the Record rather than by reading the stalled bus.
        r.writePosted(MEControl, Halt);
        r.writePosted(VBIFHalt, 15);
    }
    int fail(Record &rec, int rc, unsigned stage) {
        // Establish the failure state first. Nothing here may wait on the
        // persistent log, the console or the possibly stalled GPU bus.
        rec.rc = rc; rec.stage = stage; rec.abandoned = true;
        abandonedFlag = true; resetDone = false; runningFlag = false;
        rec.postRead = false;
        // Posted halt requests only: no reads, no DSB, no log. They request a
        // halt; they are not evidence that DMA stopped, so haltConfirmed()
        // stays false on this path.
        haltBestEffort();
        rec.converged = true;
        return rc;
    }

    void configure() {
        wr(0x3049 * 4, 3); wr(0x302a * 4, 9);                    // VBIF round-robin, gate-off WRREQ
        wr(0x46f * 4, 0xffffffff); wr(0x93 * 4, 1); wr(0x2f * 4, 0x4000ffff);
        wr(0x464 * 4, 1); wr(0xbb0 * 4, 0); wr(0x46b * 4, 6);
        wr(0xe96 * 4, 2);
        wr(0xe89 * 4, 0xffff0000); wr(0xe8a * 4, 0x1ffff);
        wr(0xe87 * 4, 0xffff0000); wr(0xe88 * 4, 0x1ffff);
        wr(0xe8b * 4, 0x100000); wr(0xe8c * 4, 0); wr(0xe8d * 4, 0x1fffff); wr(0xe8e * 4, 0);
        wr(0x825 * 4, 0x40); wr(0x826 * 4, 0x400);
        wr(0x820 * 4, 0x80000060); wr(0x81f * 4, 0x40201b16);
        wr(0xd00 * 4, (0x400u << 11) | (0x300u << 22));
        wr(0xe81 * 4, 1u << 29); wr(0x833 * 4, 0x02000000); wr(0x94 * 4, 0xa6ffffff);
        // hardwareClockGating(true) deliberately omitted: it only adds per-block
        // gating; the reset default keeps functional clocks running.
        wr(0x95 * 4, 0x3f); wr(0xf02 * 4, 2u << 7); wr(0xcc6 * 4, 2u << 1); wr(0xe8f * 4, 2);
        wr(CPProtectCntl, 7);
        wr(0xe60 * 4, rd(0xe60 * 4) | (1u << 10) | (1u << 23));
        wr(0xe04 * 4, rd(0xe04 * 4) & ~(1u << 18));
        static const struct { uint32_t base; unsigned log2; } cpProtect[] = {
            {0x4, 2}, {0x8, 3}, {0x10, 4}, {0x20, 5}, {0x40, 6}, {0x80, 6}, {0xf800, 4}, {0xf400, 1}, {0x800, 6},
            {0x840, 3}, {0x880, 5}, {0xaa0, 0}, {0xcc0, 0}, {0xcf0, 1}, {0xe68, 3}, {0xe70, 4}, {0xe80, 4}, {0x10000, 15}};
        for (unsigned i = 0; i < sizeof cpProtect / sizeof cpProtect[0]; ++i)
            wr((0x880 + i) * 4, protect(cpProtect[i].base, cpProtect[i].log2));
        wr(0xf803 * 4, 0); wr(0xf800 * 4, 0); wr(0xf801 * 4, 0); wr(0xf802 * 4, 0);
        wr(CPAddrModeCntl, 1); wr(0xbc1 * 4, 1); wr(0xc81 * 4, 1); wr(0xcc5 * 4, 1);
        wr(0xd01 * 4, 1); wr(0xe05 * 4, 1); wr(0xe41 * 4, 1); wr(0xe61 * 4, 1);
        wr(0xe80 * 4, 1); wr(0xec1 * 4, 1); wr(0xf01 * 4, 1); wr(0xf810 * 4, 1);
    }

    int kickTo(Record &rec, unsigned target) {
        // Snapshot what is still safe to read before the kick. A failed kick
        // may leave the AHB stalled, so the error path must not read the CP,
        // VBIF, interrupt or status registers again.
        rec.read = rd(RBRead); rec.write = rd(RBWrite);
        rec.status = 0xffffffffu; rec.status3 = 0xffffffffu;
        rec.me = 0xffffffffu; rec.vbif = 0xffffffffu;
        rec.irq = 0; rec.cpIrq = 0; rec.postRead = false;
        auto poll = [&](unsigned budget) {
            uint32_t lastRead = rec.read, lastStatus = 0xffffffffu, lastStatus3 = 0xffffffffu;
            uint32_t lastIrq = 0, lastCpIrq = 0;
            for (unsigned waited = 0; waited < budget; waited += 10) {
                const uint32_t irq = rd(InterruptStatus), cpirq = rd(CPInterruptStatus);
                if ((irq & ErrorIRQs) || (cpirq & 0x3f)) {
                    // First and only capture: the poll stops here, and the
                    // caller must not sample the bus again.
                    rec.irq = irq; rec.cpIrq = cpirq;
                    rec.read = lastRead; rec.status = lastStatus; rec.status3 = lastStatus3;
                    rec.write = 0xffffffffu;
                    return -3;
                }
                const uint32_t read = rd(RBRead), status = rd(Status), status3 = rd(Status3);
                lastRead = read; lastStatus = status; lastStatus3 = status3;
                lastIrq = irq; lastCpIrq = cpirq;
                if (read == target && idleStatus(status) && !(status3 & (1u << 24))) {
                    rec.read = read; rec.write = rd(RBWrite); rec.status = status; rec.status3 = status3;
                    rec.irq = irq; rec.cpIrq = cpirq;
                    rec.stage = 4; rec.rc = 0; r.mark("done");
                    runningFlag = true; resetDone = false;
                    return 0;
                }
                r.delay(10);
            }
            // Timeout: keep the last values sampled while polling, then stop.
            rec.read = lastRead; rec.status = lastStatus; rec.status3 = lastStatus3;
            rec.irq = lastIrq; rec.cpIrq = lastCpIrq; rec.write = 0xffffffffu;
            return 1;
        };
        const int polled = poll(300000);
        if (polled == 0) return 0;
        // No retry and no post-failure reads. The posted halt requests are
        // issued by fail(); the record says which fields were never sampled.
        // -3 keeps the first error IRQ, -4 is a poll timeout.
        rec.stage = 3;
        return fail(rec, polled == -3 ? -3 : -4, 3);
    }

public:
    explicit Startup(R &access) : r(access) {}
    Startup(const Startup &) = delete;
    Startup &operator=(const Startup &) = delete;

    bool abandoned() const { return abandonedFlag; }
    bool running() const { return runningFlag; }
    bool haltConfirmed() const { return haltAck; }

    // Leaves the GPU halted on failure. Callable repeatedly; never unhalts.
    void abandon(Record &rec) {
        if (!abandonedFlag) {
            rec.abandoned = true;
            abandonedFlag = true; resetDone = false; runningFlag = false;
        }
        haltBestEffort();
    }

    int reset(Record &rec, bool clearTrust = false) {
        if (resetDone || abandonedFlag || runningFlag) return fail(rec, -10, 0);
        r.mark("halt");
        wr(VBIFHalt, 15);
        unsigned waited = 0;
        while ((rd(VBIFHalted) & 15) != 15) {
            if (waited >= 100000) { rec.vbif = rd(VBIFHalted); return fail(rec, -1, 1); }
            r.delay(10); waited += 10;
        }
        rec.vbif = rd(VBIFHalted);
        r.mark("ack");
        // The drain busy bit can lag the halt ack; sample before declaring failure.
        uint32_t status = rd(Status);
        unsigned retries = 0;
        while (!idleStatus(status) && retries < 20) { r.delay(10000); status = rd(Status); ++retries; }
        rec.status = status; rec.retries = retries; rec.me = rd(MEControl);
        if (!idleStatus(status)) return fail(rec, -2, 1);
        if ((rec.me & Halt) != Halt) return fail(rec, -3, 1);
        r.mark("idle");

        wr(RBBMSwReset, 1); (void)rd(RBBMSwReset); wr(RBBMSwReset, 0);
        wr(InterruptMask, 0); wr(MEControl, Halt); wr(VBIFHalt, 15);
        waited = 0;
        while (((rd(MEControl) & Halt) != Halt) || ((rd(VBIFHalted) & 15) != 15)) {
            if (waited >= 100000) return fail(rec, -4, 1);
            r.delay(10); waited += 10;
        }
        rec.me = rd(MEControl); rec.vbif = rd(VBIFHalted);
        // Both stop controls read back halted: the only state in which DMA is
        // known to be stopped. A failure elsewhere leaves this false, so a
        // later teardown keeps the buffer instead of guessing.
        haltAck = ((rec.me & Halt) == Halt) && ((rec.vbif & 15) == 15);
        r.mark("swrst");
        if (rd(RBBMSwReset) || rd(RBRead) || rd(RBWrite) || rd(ContextSwitch) || rd(TrustControl) > 1)
            return fail(rec, -5, 1);
        wr(GPMUCM3SysReset, 1); wr(GPMUReady, 0); wr(GPMUError, 0);
        if (rd(GPMUCM3SysReset) != 1 || rd(GPMUReady) || rd(GPMUError)) return fail(rec, -6, 1);
        r.mark("cm3");

        // Leaving secure mode by clearing RBBM_SECVID_TRUST_CNTL hung this
        // SoC's GPU access in the step-marked run, so it is off by default.
        // Only an explicit opt-in may try it; ZAP/PAS premises come first.
        if (clearTrust) {
            r.mark("trustw");
            wr(TrustControl, 0);
            r.mark("trustwr");
            rec.trust = rd(TrustControl);
        } else {
            rec.trust = rd(TrustControl);
        }
        rec.cpControl = rd(CPControl);
        if (clearTrust) r.mark("trust");
        r.mark("ready");
        rec.stage = 1; rec.rc = 0;
        resetDone = true;
        return 0;
    }

    // Loads the CP microcode addresses and submits CP_ME_INIT. A true result
    // means RBRead advanced through the ring. Any failure re-halts and abandons.
    int start(volatile uint32_t *ring, unsigned ringDwords, uint64_t ringVA, uint64_t pm4VA,
              uint64_t pfpVA, uint64_t scratchVA, Record &rec) {
        if (!resetDone || abandonedFlag || runningFlag) return fail(rec, -11, 0);
        if (!ring || ringDwords != RingDwords || (ringVA & 4095) || (pm4VA & 4095) ||
            (pfpVA & 4095) || (scratchVA & 4095) || !scratchVA)
            return fail(rec, -12, 0);
        for (unsigned i = 0; i < ringDwords; ++i) ring[i] = 0;
        configure();
        r.mark("cfg");
        wr(MEBase, uint32_t(pm4VA)); wr(MEBase + 4, uint32_t(pm4VA >> 32));
        wr(PFPBase, uint32_t(pfpVA)); wr(PFPBase + 4, uint32_t(pfpVA >> 32));
        wr(RBBase, uint32_t(ringVA)); wr(RBBase + 4, uint32_t(ringVA >> 32));
        wr(RBReadAddr, uint32_t(scratchVA)); wr(RBReadAddrHi, uint32_t(scratchVA >> 32));
        wr(ContextSwitch, 0); wr(RBControl, RingControl); wr(RBWrite, 0);
        wr(InterruptMask, 0); wr(InterruptClear, OwnedIRQs);
        if (rd(MEBase) != uint32_t(pm4VA) || rd(PFPBase) != uint32_t(pfpVA) ||
            rd(RBBase) != uint32_t(ringVA) || rd(RBReadAddr) != uint32_t(scratchVA) ||
            rd(RBReadAddrHi) != uint32_t(scratchVA >> 32) || rd(RBControl) != RingControl ||
            rd(RBRead) || rd(RBWrite) || rd(ContextSwitch) ||
            rd(CPAddrModeCntl) != 1 || rd(CPProtectCntl) != 7)
            return fail(rec, -2, 2);
        r.mark("addr");
        rec.stage = 2;

        // sagit order: release the engine first, then stage the ring and kick.
        r.publish(); wr(VBIFHalt, 0); wr(MEControl, 0);
        haltAck = false;
        unsigned cursor = 0;
        ring[cursor++] = packet7(0x48, 8);  // CP_ME_INIT, A540 payload
        static const uint32_t init[] = {0x2f, 3, 0x20000000, 0, 0, 0, 0, 0};
        for (unsigned i = 0; i < sizeof init / sizeof init[0]; ++i) ring[cursor++] = init[i];
        r.clean((volatile void *)ring, ringDwords * 4);
        ringPtr=ring;ringCursor=cursor;
        r.mark("kick");
        r.publish(); wr(RBWrite, cursor);

        return kickTo(rec, cursor);
    }

    // Upload the GPMU firmware after CP_ME_INIT, before ZAP, as sagit does.
    bool gpmu(uint64_t commandsVA, uint32_t words, uint32_t microvolts, uint32_t frequency, Record &rec) {
        if (!runningFlag || !ringPtr || ringCursor != 9 || !commandsVA || (commandsVA & 3) || !words || words > 0x1021) {
            fail(rec, -20, 4); return false;
        }
        if (rd(GPMUCM3SysReset) != 1 || rd(GPMUReady) || rd(GPMUError)) { fail(rec, -21, 4); return false; }
        wr(GPMUVoltage, 0x80000000); wr(GPMUPwrThreshold, 0x80000000 | 6000);
        const unsigned msg = 0x9800 + 7, payload = msg + 5;
        for (unsigned i = 0; i < 36; ++i) wr((payload + i) * 4, 0);
        wr(msg * 4, 0x80000001); wr((msg + 1) * 4, 1);
        wr(payload * 4, 5448); wr((payload + 1) * 4, 1);
        wr((payload + 2) * 4, microvolts / 1000); wr((payload + 3) * 4, frequency / 1000000);
        wr((payload + 34) * 4, (1u << 24) | (1u << 17) | (2u << 8));
        wr((payload + 35) * 4, ~0x303u); wr((msg + 3) * 4, 36 * 4);
        wr((0x9800 + 5) * 4, 0xbabeface);
        wr(GPMUPwrColInterFrameCtrl, 0x7f); wr(GPMUPwrColBinningCtrl, 0);
        wr(GPMUPwrColInterFrameHyst, 0xa0080); wr(GPMUPwrColStaggerDelay, 0x600040);
        unsigned c = ringCursor;
        ringPtr[c++] = packet7(0x5f, 1); ringPtr[c++] = 0;
        ringPtr[c++] = packet7(0x3f, 3); ringPtr[c++] = uint32_t(commandsVA);
        ringPtr[c++] = uint32_t(commandsVA >> 32); ringPtr[c++] = words;
        ringPtr[c++] = packet7(0x5f, 1); ringPtr[c++] = 1;
        r.clean((volatile void *)ringPtr, c * 4);
        ringCursor = c;
        r.mark("gpmukick"); r.publish(); wr(RBWrite, c);
        if (kickTo(rec, c)) return false;
        wr(GPMUCM3SysReset, 0);
        for (unsigned elapsed = 0; elapsed < 100000; elapsed += 10) {
            if ((rd(InterruptStatus) & ErrorIRQs) || (rd(CPInterruptStatus) & 0x3f) || rd(GPMUError)) {
                fail(rec, -22, 4); return false;
            }
            if (rd(GPMUReady) == 0xbabeface) { rec.stage = 5; rec.rc = 0; r.mark("gpmudone"); return true; }
            r.delay(10);
        }
        fail(rec, -23, 4); return false;
    }

    // One user indirect buffer: GPU address of the command stream and its
    // length in dwords. The buffer must already be mapped in this context.
    struct Ib { uint64_t va; uint32_t dwords; };
    // 0x66(2) + transition wait(1) + 0x26(1) + 0x46(5)
    static constexpr unsigned SubmitFenceWords = 9;

    // Append IB packets plus the non-secure fence to the ring, kick once and
    // wait for the CP to write the sequence. Every call waits for its own
    // completion, so the 32 KiB ring is idle when it wraps. This is the
    // submission primitive the user client calls; fence() below is the
    // zero-buffer case.
    int submit(const Ib *ibs, unsigned count, uint64_t fenceVA, uint32_t sequence,
               volatile uint32_t *fenceCPU, Record &rec, unsigned ringDwords) {
        if (!runningFlag || abandonedFlag || !ringPtr || ringDwords != RingDwords) {
            fail(rec, -40, 7); return rec.rc;
        }
        if (!fenceVA || (fenceVA & 4095) || !fenceCPU || count > 16 ||
            (count && !ibs)) {
            fail(rec, -41, 7); return rec.rc;
        }
        for (unsigned i = 0; i < count; ++i)
            if (!ibs[i].va || (ibs[i].va & 3) || !ibs[i].dwords || ibs[i].dwords > 0x3fff) {
                fail(rec, -42, 7); return rec.rc;
            }
        // The hardware write pointer is authoritative: the probe's own path and
        // this producer must agree, and the CP has consumed everything before
        // a submit starts. Adopt it instead of trusting a stale local cursor,
        // then refuse to write while the CP has not reached it.
        const uint32_t hw = rd(RBWrite);
        if (hw && hw <= ringDwords) ringCursor = hw;
        for (unsigned i = 0; i < 1000 && rd(RBRead) != hw; ++i) r.delay(10);
        if (rd(RBRead) != hw) { fail(rec, -45, 7); return rec.rc; }
        const unsigned need = SubmitFenceWords + count * 4;
        if (need > ringDwords) { fail(rec, -43, 7); return rec.rc; }
        if (ringCursor + need >= ringDwords) {
            // NOP-fill the tail instead of letting a packet cross the wrap.
            for (unsigned i = ringCursor; i < ringDwords; ++i) ringPtr[i] = packet7(0x10, 0);
            ringCursor = 0;
        }
        unsigned c = ringCursor;
        // Leave secure mode before any user IB: the secure stream has no
        // mapping for the user window, so an IB fetched first would fault.
        // Wait for that transition before the fetch, as sagit does; issuing
        // the IB immediately after the switch stalled this AHB.
        ringPtr[c++] = packet7(0x66, 1); ringPtr[c++] = 0;          // CP_SET_SECURE_MODE(0)
        ringPtr[c++] = packet7(0x26, 0);                            // wait for the transition
        for (unsigned i = 0; i < count; ++i) {
            ringPtr[c++] = packet7(0x3f, 3);                        // CP_INDIRECT_BUFFER
            ringPtr[c++] = uint32_t(ibs[i].va);
            ringPtr[c++] = uint32_t(ibs[i].va >> 32);
            ringPtr[c++] = ibs[i].dwords;
        }
        ringPtr[c++] = packet7(0x26, 0);                            // CP_WAIT_MEM_WRITES
        ringPtr[c++] = packet7(0x46, 4); ringPtr[c++] = 4 | (1u << 31); // CACHE_FLUSH_TS + IRQ
        ringPtr[c++] = uint32_t(fenceVA); ringPtr[c++] = uint32_t(fenceVA >> 32);
        ringPtr[c++] = sequence;
        fenceCPU[0] = 0; r.clean((volatile void *)fenceCPU, 4);
        r.clean((volatile void *)ringPtr, ringDwords * 4);
        ringCursor = c;
        // Sample the latches before this doorbell. sagit clears the owned RBBM
        // set before publishing a stream, so a completion bit left by the
        // probe's own fence cannot fail this kick. CP_INTERRUPT_STATUS has no
        // safe write-only ack here: an already latched CP fault refuses the
        // submit instead of kicking into a faulted engine.
        rec.preIrq = rd(InterruptStatus); rec.preCpIrq = rd(CPInterruptStatus);
        if (rec.preIrq == 0xffffffffu || rec.preCpIrq == 0xffffffffu) {
            rec.stage = 3; return fail(rec, -6, 3);
        }
        if (rec.preCpIrq & 0x3f) {
            rec.stage = 3; return fail(rec, -7, 3);
        }
        wr(InterruptClear, OwnedIRQs);
        r.mark("submit"); r.publish(); wr(RBWrite, c);
        if (kickTo(rec, c)) {
            // The fence page is CPU memory, not the possibly stalled bus: it
            // still tells whether the ring ran despite the latched error.
            r.clean((volatile void *)fenceCPU, 4);
            if (fenceCPU[0] == sequence) rec.fenced = true;
            return rec.rc;
        }
        for (unsigned elapsed = 0; elapsed < 100000; elapsed += 10) {
            r.clean((volatile void *)fenceCPU, 4);
            if (fenceCPU[0] == sequence) { rec.stage = 7; rec.rc = 0; r.mark("submitdone"); return 0; }
            r.delay(10);
        }
        fail(rec, -44, 7); return rec.rc;
    }

    // Minimal CP fence: switch to non-secure mode, wait for writes, then ask
    // the CP to write a sequence into the mapped fence page. CPU flush/read is
    // the evidence; a ring pointer alone is not a completed GPU DMA.
    bool fence(uint64_t fenceVA, uint32_t sequence, volatile uint32_t *fenceCPU, Record &rec) {
        const int rc = submit(nullptr, 0, fenceVA, sequence, fenceCPU, rec, RingDwords);
        if (rc != 0) return false;
        rec.stage = 6; r.mark("fencedone");
        return true;
    }
};

}  // namespace CheeseburgerGpu
