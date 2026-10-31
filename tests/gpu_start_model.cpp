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
    enum Fail { None, VbifAck, Idle, MeNotHalt, SwReset, Cm3, Kick, ErrorIrq };
    Fail fail = None;
    std::vector<uint32_t> reg = std::vector<uint32_t>(0x40000 / 4, 0);
    bool running = false;
    unsigned unhaltWrites = 0, rbWriteWrites = 0, submitWrites = 0, rbControlWrites = 0, delays = 0, posted = 0;
    char marks[64][16] = {};
    unsigned markCount = 0;

    Mock() { reset(); }
    void reset() {
        std::fill(reg.begin(), reg.end(), 0);
        running = false;
        unhaltWrites = rbWriteWrites = submitWrites = rbControlWrites = delays = posted = 0;
        markCount = 0;
        std::memset(marks, 0, sizeof marks);
        reg[MEControl / 4] = Halt;       // cold CP is halted
        reg[TrustControl / 4] = 1;       // secure mode on this SoC
    }
    uint32_t read(uint32_t o) {
        if (o == VBIFHalted) return fail == VbifAck ? 0 : 0xf000f;
        if (o == Status) return fail == Idle ? 0xc00003c1u : (running ? 0 : 0x800001c1u);
        if (o == Status3) return 0;
        if (o == MEControl) return fail == MeNotHalt ? 0 : reg[o / 4];
        if (o == RBBMSwReset) return fail == SwReset ? 1 : 0;
        if (o == GPMUCM3SysReset) return fail == Cm3 ? 0 : 1;
        if (o == GPMUReady || o == GPMUError) return 0;
        if (o == InterruptStatus && fail == ErrorIrq) return ErrorIRQs;
        return reg[o / 4];
    }
    void write(uint32_t o, uint32_t value) {
        reg[o / 4] = value;
        if (o == MEControl && value == 0) ++unhaltWrites;
        if (o == RBControl) ++rbControlWrites;
        if (o == RBWrite) {
            ++rbWriteWrites;
            if (value == 9) ++submitWrites;
            if (value == 9 && fail != Kick) {
                running = true;
                reg[RBRead / 4] = 9;
            }
        }
    }
    void writePosted(uint32_t o, uint32_t value) { ++posted; write(o, value); }
    void delay(unsigned) { ++delays; }
    void mark(const char *tag) {
        if (markCount < 64) {
            unsigned i = 0;
            for (; tag[i] && i < 15; ++i) marks[markCount][i] = tag[i];
            marks[markCount][i] = 0;
            ++markCount;
        }
    }
    void publish() {}
    void clean(volatile void *, unsigned) {}
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
    check(m.markCount && std::strcmp(m.marks[m.markCount - 1], "fail") == 0, name);
    check(std::strcmp(m.marks[0], "halt") == 0, name);
}

int main() {
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
        // The first doorbell may be lost while the CP leaves reset; the retry is bounded.
        check(m.submitWrites == 2 && m.unhaltWrites == 1, "kick/bounded-retry");
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
