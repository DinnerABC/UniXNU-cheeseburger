// SPDX-License-Identifier: GPL-2.0-only
// Host register model for CheeseburgerGpu::Startup. It validates the failure
// cleanup contract offline: every failure must leave MEControl/VBIFHalt halted
// and must never submit, while the success path leaves the CP running.
#include "../package/BoardRuntime/CheeseburgerGpuStart.h"
#include "../package/BoardRuntime/CheeseburgerGpuWalk.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using namespace CheeseburgerGpu;

struct Mock {
    // ErrorLate models a latched bit while the CP still completed the ring and
    // wrote the fence; ErrorIrq faults before the fence is visible.
    enum Fail { None, VbifAck, Idle, MeNotHalt, SwReset, Cm3, Kick, ErrorIrq, ErrorLate, MmioGone };
    Fail fail = None;
    std::vector<uint32_t> reg = std::vector<uint32_t>(0x40000 / 4, 0);
    bool running = false;
    bool failureSeen = false, stallMarks = false;
    // RBBM latches and the fault arm: injected errors only appear after the
    // doorbell, so a pre-kick sample sees the healthy bus.
    bool armed = false;
    uint32_t irqLatch = 0;
    unsigned readsAfterFailure = 0;
    unsigned unhaltWrites = 0, rbWriteWrites = 0, submitWrites = 0, rbControlWrites = 0, delays = 0, posted = 0;
    char marks[64][16] = {};
    unsigned markCount = 0;
    // CP_CACHE_FLUSH_TS simulation: clean() sees the ring before the doorbell,
    // publish() then writes the sequence to the host fence page. The test
    // registers the page, so submit() can observe its own completion.
    volatile uint32_t *hostFence = nullptr;
    uint32_t fenceSeq = 0, lastRbWrite = 0, execCursor = 0;
    bool wrapped = false;
    const uint32_t *ringView = nullptr;
    unsigned ringWords = 0;

    Mock() { reset(); }
    void reset() {
        std::fill(reg.begin(), reg.end(), 0);
        running = false;
        unhaltWrites = rbWriteWrites = submitWrites = rbControlWrites = delays = posted = 0;
        failureSeen = false; stallMarks = false; readsAfterFailure = 0;
        armed = false; irqLatch = 0;
        markCount = 0;
        std::memset(marks, 0, sizeof marks);
        reg[MEControl / 4] = Halt;       // cold CP is halted
        reg[TrustControl / 4] = 1;       // secure mode on this SoC
    }
    uint32_t read(uint32_t o) {
        // The error observation is the InterruptStatus/CPInterruptStatus pair;
        // only reads beyond that pair count as post-failure bus access.
        if (failureSeen && o != CPInterruptStatus) ++readsAfterFailure;
        // Faults arm at the doorbell. The inaccessible window is observed at
        // the poll's interrupt read; setup reads before the kick are healthy.
        if (armed && fail == MmioGone) {
            if (o == InterruptStatus) failureSeen = true;
            return 0xffffffffu;
        }
        if (armed && (fail == ErrorIrq || fail == ErrorLate)) {
            if (o == InterruptStatus) { failureSeen = true; return ErrorIRQs; }
            if (o == CPInterruptStatus) return 0;
        }
        if (o == VBIFHalted) return fail == VbifAck ? 0 : 0xf000f;
        if (o == Status) return fail == Idle ? 0xc00003c1u : (running ? 0x80000101u : 0x800001c1u);
        if (o == Status3) return 0;
        if (o == MEControl) return fail == MeNotHalt ? 0 : reg[o / 4];
        if (o == RBBMSwReset) return fail == SwReset ? 1 : 0;
        if (o == GPMUCM3SysReset) return fail == Cm3 ? 0 : 1;
        if (o == GPMUReady || o == GPMUError) return reg[o / 4];
        if (o == InterruptStatus) return irqLatch;
        return reg[o / 4];
    }
    void write(uint32_t o, uint32_t value) {
        reg[o / 4] = value;
        if (o == InterruptClear) irqLatch &= ~value;
        if (o == MEControl && value == 0) ++unhaltWrites;
        if (o == RBControl) ++rbControlWrites;
        if (o == RBWrite) {
            ++rbWriteWrites;
            if (fail == ErrorIrq || fail == ErrorLate || fail == MmioGone) armed = true;
            if (value == 9) ++submitWrites;
            if (value < lastRbWrite) wrapped = true;
            // Execute the ring like the CP: walk the doorbell range
            // circularly and take the last CP_CACHE_FLUSH_TS packet.
            const uint32_t *w = ringView;
            const unsigned total = ringWords;
            auto scan = [&](unsigned a, unsigned b) {
                if (!w) return;
                for (unsigned i = a; i + 5 <= b; ++i)
                    if (w[i] == packet7(0x46, 4)) fenceSeq = w[i + 4];
            };
            if (w && total) {
                if (value >= execCursor) scan(execCursor, value);
                else { scan(execCursor, total); scan(0, value); }
            }
            execCursor = value;
            lastRbWrite = value;
            // ErrorIrq/MmioGone fault before the fence write is visible;
            // ErrorLate completes the ring, so the CPU page is updated.
            if (hostFence && fenceSeq && fail != ErrorIrq && fail != MmioGone)
                hostFence[0] = fenceSeq;
            if (fail != Kick) {
                running = true;
                reg[RBRead / 4] = value;
            }
        }
        if (o == GPMUCM3SysReset && value == 0) reg[GPMUReady / 4] = 0xbabeface;
    }
    void writePosted(uint32_t o, uint32_t value) { ++posted; write(o, value); }
    void delay(unsigned) { ++delays; }
    void mark(const char *tag) {
        if (stallMarks) return;
        if (markCount < 64) {
            unsigned i = 0;
            for (; tag[i] && i < 15; ++i) marks[markCount][i] = tag[i];
            marks[markCount][i] = 0;
            ++markCount;
        }
    }
    void publish() {}
    void clean(volatile void *p, unsigned n) {
        ringView = const_cast<const uint32_t *>(static_cast<const volatile uint32_t *>(p));
        ringWords = n / 4;
    }
};

static void check(bool ok, const char *name);

struct HostTables {
    std::vector<std::vector<uint64_t>> t; // 0 invalid, 1 root, 2.. tables
    std::map<uint64_t, unsigned> bypa;
    HostTables() { t.emplace_back(512, 0); t.emplace_back(512, 0); }
    unsigned add(uint64_t pa) { t.emplace_back(512, 0); bypa[pa] = unsigned(t.size()) - 1; return unsigned(t.size()) - 1; }
    unsigned root() { return 1; }
    uint64_t entry(unsigned id, unsigned i) { return id < t.size() ? t[id][i] : 0; }
    unsigned next(uint64_t pa) { auto it = bypa.find(pa); return it == bypa.end() ? 0 : it->second; }
};

static void walker_cases() {
    HostTables h;
    const unsigned l1 = h.add(0x1000), l2 = h.add(0x2000), l3 = h.add(0x3000);
    h.t[1][0] = 0x1000 | 3;       // root[0] -> L1
    h.t[l1][0] = 0x2000 | 3;      // L1[0] -> L2
    h.t[l2][16] = 0x3000 | 3;     // L2[16] -> L3
    h.t[l3][0] = 0x400000 | 0x6c3;
    uint64_t pte = 0;
    CheeseburgerGpu::lookupLeaf(h, 0x2000000, pte);
    check(pte == (0x400000 | 0x6c3), "walk/leaf");
    h.t[l3][1] = 0x500000 | 0x6c3;
    CheeseburgerGpu::lookupLeaf(h, 0x2001000, pte);
    check(pte == (0x500000 | 0x6c3), "walk/boundary");
    h.t[1][0] = 0;
    check(CheeseburgerGpu::lookupLeaf(h, 0x2000000, pte) == -2, "walk/missing");
    h.t[1][0] = 5;
    check(CheeseburgerGpu::lookupLeaf(h, 0x2000000, pte) == -4, "walk/invalid-desc");
    h.t[1][0] = 0x9000 | 3;
    check(CheeseburgerGpu::lookupLeaf(h, 0x2000000, pte) == -5, "walk/broken-link");
    h.t[1][0] = 0x1000 | 3;
    h.t[l3][0] = 1;
    check(CheeseburgerGpu::lookupLeaf(h, 0x2000000, pte) == -3, "walk/invalid-leaf");
    // 36-bit IAS (T0SZ=28): the root is a level-1 table and the walk uses two
    // descents before the L3 leaf.
    HostTables h1;
    const unsigned m2 = h1.add(0x7000), m3 = h1.add(0x8000);
    h1.t[1][0] = 0x7000 | 3;
    h1.t[m2][16] = 0x8000 | 3;
    h1.t[m3][0] = 0x400000 | 0x6c3;
    uint64_t pte1 = 0;
    check(CheeseburgerGpu::lookupLeaf(h1, 0x2000000, pte1, 1) == 0 &&
          pte1 == (0x400000 | 0x6c3), "walk/level1-36bit");
}

static unsigned failures = 0;
static void check(bool ok, const char *name) {
    if (!ok) { std::printf("FAIL %s\n", name); ++failures; }
}
static bool marks_are(Mock &m, const char *const *want, unsigned n) {
    if (m.markCount != n) return false;
    for (unsigned i = 0; i < n; ++i)
        if (std::strcmp(m.marks[i], want[i])) return false;
    return true;
}

static void halted_after_failure(Mock &m, const char *name) {
    check(m.reg[MEControl / 4] == Halt, name);
    check(m.reg[VBIFHalt / 4] == 15, name);
}

static void case_reset_failure(Mock::Fail fail, int expected, const char *name) {
    Mock m; m.reset(); m.fail = fail;
    Startup<Mock> engine(m);
    Record rec;
    int rc = engine.reset(rec);
    check(rc == expected, name);
    check(rec.abandoned, name);
    check(engine.abandoned() && !engine.running(), name);
    halted_after_failure(m, name);
    check(m.unhaltWrites == 0, name);
    check(m.rbControlWrites == 0 && m.rbWriteWrites == 0, name);
    check(rec.converged, name);
    check(std::strcmp(m.marks[0], "halt") == 0, name);
}

int main() {
    // GPMU upload follows ME_INIT and must reach ready before ZAP.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        check(engine.reset(rec) == 0, "gpmu/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "gpmu/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "gpmu/success");
        check(rec.stage == 5 && engine.running(), "gpmu/flags");
        check(m.reg[GPMUCM3SysReset / 4] == 0 && m.reg[GPMUReady / 4] == 0xbabeface, "gpmu/ready");
        check(ring[9] == packet7(0x5f, 1) && ring[10] == 0 && ring[11] == packet7(0x3f, 3) &&
              ring[12] == 0x2012000 && ring[13] == 0 && ring[14] == 1000, "gpmu/commands");
    }

    // User submission: CP_INDIRECT_BUFFER then the non-secure fence, and the
    // CPU observes the CP-written sequence.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "submit/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "submit/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "submit/gpmu");
        const Startup<Mock>::Ib ib = {0x4000000, 12};
        const int rc = engine.submit(&ib, 1, 0x2015000, 2, fence, rec, RingDwords);
        check(rc == 0, "submit/rc");
        check(rec.stage == 7 && engine.running(), "submit/stage");
        check(ring[17] == packet7(0x66, 1) && ring[18] == 0 &&
              ring[19] == packet7(0x26, 0) &&
              ring[20] == packet7(0x3f, 3) && ring[21] == 0x4000000 && ring[22] == 0 &&
              ring[23] == 12, "submit/ib-packet");
        check(ring[24] == packet7(0x26, 0) && ring[25] == packet7(0x46, 4) &&
              ring[26] == (4 | (1u << 31)) && ring[27] == 0x2015000 &&
              ring[28] == 0 && ring[29] == 2, "submit/fence-packet");
        check(fence[0] == 2, "submit/fence-value");
    }

    // The ring is 32 KiB and every submit waits for its own completion, so a
    // packet must never cross the wrap: the tail is NOP-filled and the cursor
    // restarts at zero.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "wrap/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "wrap/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "wrap/gpmu");
        const Startup<Mock>::Ib ib = {0x4000000, 12};
        bool all_ok = true;
        unsigned fail_at = 0;
        for (unsigned i = 0; i < 700; ++i) {
            const int rc = engine.submit(&ib, 1, 0x2015000, 3 + i, fence, rec, RingDwords);
            if (rc != 0) { all_ok = false; fail_at = i + 1; std::printf("WRAPFAIL i=%u rc=%d read=%u write=%u\n", i, rc, rec.read, rec.write); break; }
        }
        (void)fail_at;
        check(all_ok, "wrap/700-submits");
        check(m.wrapped, "wrap/happened");
        check(m.lastRbWrite <= RingDwords, "wrap/target-bounded");
        check(ring[RingDwords - 1] == packet7(0x10, 0), "wrap/tail-nop");
    }

    // Malformed buffers and a failed kick must fail closed: halted, abandoned,
    // and no further submission is possible.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "badib/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "badib/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "badib/gpmu");
        const Startup<Mock>::Ib bad = {0x4000002, 12};
        check(engine.submit(&bad, 1, 0x2015000, 2, fence, rec, RingDwords) != 0, "badib/reject");
        check(rec.abandoned && engine.abandoned() && !engine.running(), "badib/abandoned");
        halted_after_failure(m, "badib/halted");
    }
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "kickfail/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "kickfail/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "kickfail/gpmu");
        m.fail = Mock::Kick;
        const Startup<Mock>::Ib ib = {0x4000000, 12};
        check(engine.submit(&ib, 1, 0x2015000, 2, fence, rec, RingDwords) != 0, "kickfail/reject");
        check(rec.rc == -4 && rec.abandoned && engine.abandoned(), "kickfail/abandoned");
        halted_after_failure(m, "kickfail/halted");
    }

    // A kick timeout must not retry the doorbell or read the bus afterwards;
    // unsampled registers are marked, and a second submission is refused.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "timeout/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "timeout/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "timeout/gpmu");
        m.fail = Mock::Kick;
        const unsigned writes = m.rbWriteWrites;
        const Startup<Mock>::Ib ib = {0x4000000, 12};
        check(engine.submit(&ib, 1, 0x2015000, 2, fence, rec, RingDwords) != 0, "timeout/reject");
        check(rec.rc == -4 && rec.stage == 3, "timeout/rc-stage");
        check(m.rbWriteWrites == writes + 1, "timeout/no-retry");
        check(rec.write == 0xffffffffu && rec.me == 0xffffffffu && rec.vbif == 0xffffffffu, "timeout/unread-marked");
        check(!rec.postRead && rec.converged, "timeout/state");
        check(engine.submit(&ib, 1, 0x2015000, 3, fence, rec, RingDwords) < 0, "timeout/resubmit-refused");
        check(m.rbWriteWrites == writes + 1, "timeout/resubmit-no-write");
    }

    // An error IRQ is captured once; the error path performs no further bus
    // read (the mock counts any read after the first error observation).
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "errirq/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "errirq/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "errirq/gpmu");
        m.fail = Mock::ErrorIrq;
        const Startup<Mock>::Ib ib = {0x4000000, 12};
        check(engine.submit(&ib, 1, 0x2015000, 2, fence, rec, RingDwords) != 0, "errirq/reject");
        check(rec.rc == -3 && rec.irq == ErrorIRQs && rec.cpIrq == 0, "errirq/first-irq");
        check(!rec.fenced, "errirq/no-fence");
        check(m.readsAfterFailure == 0, "errirq/no-bus-read");
        check(!rec.postRead && rec.abandoned && rec.converged, "errirq/state");
        check(engine.abandoned() && !engine.running(), "errirq/abandoned");
        halted_after_failure(m, "errirq/halted");
    }

    // An inaccessible MMIO window behaves like an error IRQ, not an infinite
    // poll: no post-failure reads and a marked, abandoned engine.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "mmiogone/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "mmiogone/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "mmiogone/gpmu");
        m.fail = Mock::MmioGone;
        const Startup<Mock>::Ib ib = {0x4000000, 12};
        check(engine.submit(&ib, 1, 0x2015000, 2, fence, rec, RingDwords) != 0, "mmiogone/reject");
        check(rec.rc == -3 && rec.irq == 0xffffffffu, "mmiogone/irq");
        check(m.readsAfterFailure == 0, "mmiogone/no-bus-read");
        check(rec.abandoned && rec.converged, "mmiogone/state");
    }

    // A completion bit left latched by the probe's own fence must not fail the
    // user kick: the owned RBBM set is cleared before the doorbell, like sagit.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "stale/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "stale/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "stale/gpmu");
        m.irqLatch = ErrorIRQs | (1u << 20);
        const Startup<Mock>::Ib ib = {0x4000000, 12};
        check(engine.submit(&ib, 1, 0x2015000, 2, fence, rec, RingDwords) == 0, "stale/rc");
        check(rec.preIrq == (ErrorIRQs | (1u << 20)) && rec.irq == 0, "stale/pre");
        check(m.irqLatch == 0 && fence[0] == 2, "stale/cleared");
    }

    // A CP fault that is already latched cannot be acknowledged safely, so the
    // submission is refused before the doorbell instead of kicked into it.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "stalecp/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "stalecp/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "stalecp/gpmu");
        m.reg[CPInterruptStatus / 4] = 4;
        const unsigned writes = m.rbWriteWrites;
        const Startup<Mock>::Ib ib = {0x4000000, 12};
        check(engine.submit(&ib, 1, 0x2015000, 2, fence, rec, RingDwords) == -7, "stalecp/rc");
        check(rec.preCpIrq == 4 && rec.stage == 3 && rec.abandoned, "stalecp/record");
        check(m.rbWriteWrites == writes, "stalecp/no-doorbell");
        halted_after_failure(m, "stalecp/halted");
    }

    // The error latch stops the register poll, but the fence page is CPU memory
    // and still shows that the ring completed (ErrorLate models this).
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "late/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "late/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "late/gpmu");
        m.fail = Mock::ErrorLate;
        const Startup<Mock>::Ib ib = {0x4000000, 12};
        check(engine.submit(&ib, 1, 0x2015000, 2, fence, rec, RingDwords) == -3, "late/rc");
        check(rec.fenced && rec.irq == ErrorIRQs, "late/fenced");
        check(m.readsAfterFailure == 0, "late/no-bus-read");
    }

    // A stalled log/heartbeat cannot prevent the failure state from being set.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        volatile uint32_t fence[1] = {0};
        m.hostFence = fence;
        check(engine.reset(rec) == 0, "logstall/reset");
        check(engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec) == 0, "logstall/start");
        check(engine.gpmu(0x2012000, 1000, 800000, 257000000, rec), "logstall/gpmu");
        m.stallMarks = true;
        m.fail = Mock::ErrorIrq;
        const Startup<Mock>::Ib ib = {0x4000000, 12};
        check(engine.submit(&ib, 1, 0x2015000, 2, fence, rec, RingDwords) != 0, "logstall/reject");
        check(rec.abandoned && engine.abandoned() && rec.converged, "logstall/state");
    }

    // Success path.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        check(engine.reset(rec) == 0, "success/reset");
        check(rec.trust == 1 && rec.cpControl == 0, "success/trust-readback");
        volatile uint32_t ring[RingDwords] = {0};
        int rc = engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec);
        check(rc == 0, "success/start");
        check(engine.running() && !engine.abandoned(), "success/flags");
        check(rec.read == 9 && rec.write == 9 && rec.stage == 4, "success/ring-pointers");
        check(m.reg[MEControl / 4] == 0 && m.reg[VBIFHalt / 4] == 0, "success/unhalted");
        check(ring[0] == packet7(0x48, 8) && ring[1] == 0x2f && ring[2] == 3 &&
              ring[3] == 0x20000000, "success/me-init");
        check(m.reg[CPProtectCntl / 4] == 7 && m.reg[CPAddrModeCntl / 4] == 1, "success/configure");
        check(m.reg[RBControl / 4] == RingControl, "success/rb-control");
        check(m.reg[RBReadAddr / 4] == 0x2015000 && m.reg[RBReadAddrHi / 4] == 0, "success/rptr-scratch");
        static const char *const want[] = {"halt", "ack", "idle", "swrst", "cm3", "ready",
                                           "cfg", "addr", "kick", "done"};
        check(marks_are(m, want, 10), "success/marks");
    }
    // Explicit secure-mode clear is opt-in and marks the write/read separately.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        check(engine.reset(rec, true) == 0, "trust/reset");
        static const char *const want[] = {"halt", "ack", "idle", "swrst", "cm3",
                                           "trustw", "trustwr", "trust", "ready"};
        check(marks_are(m, want, 9), "trust/marks");
        check(rec.trust == 0, "trust/readback");
    }
    // Every reset failure keeps CP/VBIF halted and never submits.
    case_reset_failure(Mock::VbifAck, -1, "vbif-ack");
    case_reset_failure(Mock::Idle, -2, "idle");
    case_reset_failure(Mock::MeNotHalt, -3, "me-not-halt");
    case_reset_failure(Mock::SwReset, -5, "sw-reset");
    case_reset_failure(Mock::Cm3, -6, "cm3");
    // Kick timeout re-halts after the one submission.
    {
        Mock m; m.fail = Mock::Kick;
        Startup<Mock> engine(m);
        Record rec;
        check(engine.reset(rec) == 0, "kick/reset");
        volatile uint32_t ring[RingDwords] = {0};
        int rc = engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec);
        check(rc == -4, "kick/timeout");
        check(rec.abandoned && engine.abandoned(), "kick/abandoned");
        halted_after_failure(m, "kick/halted");
        // No retry: a timeout stops at the first doorbell and does not touch
        // the bus again; the record carries the unread fields.
        check(m.submitWrites == 1 && m.unhaltWrites == 1, "kick/single-doorbell");
        check(m.posted == 2, "kick/posted-halt");
    }
    // An error interrupt fails the kick immediately; cleanup must not block.
    {
        Mock m; m.fail = Mock::ErrorIrq;
        Startup<Mock> engine(m);
        Record rec;
        check(engine.reset(rec) == 0, "errirq/reset");
        volatile uint32_t ring[RingDwords] = {0};
        int rc = engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec);
        check(rc == -3, "errirq/rc");
        check(rec.abandoned && (rec.irq & ErrorIRQs), "errirq/record");
        check(m.posted == 2, "errirq/posted-halt");
        halted_after_failure(m, "errirq/halted");
    }
    // start() without reset must not touch the ring registers.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        volatile uint32_t ring[RingDwords] = {0};
        int rc = engine.start(ring, RingDwords, 0x2000000, 0x2008000, 0x200d000, 0x2015000, rec);
        check(rc == -11, "no-reset/rc");
        check(m.rbControlWrites == 0 && m.rbWriteWrites == 0, "no-reset/no-submit");
        halted_after_failure(m, "no-reset/halted");
    }
    walker_cases();
    // Idempotent cleanup must not unhalt.
    {
        Mock m;
        Startup<Mock> engine(m);
        Record rec;
        engine.abandon(rec);
        engine.abandon(rec);
        check(m.unhaltWrites == 0 && m.reg[MEControl / 4] == Halt, "abandon/twice");
    }
    if (failures) { std::printf("RESULT FAIL %u\n", failures); return 1; }
    std::printf("RESULT PASS\n");
    return 0;
}
