// SPDX-License-Identifier: GPL-2.0-only
// Host model for CheeseburgerGpu::Engine540. It parses exactly the packets the
// engine emits (type-7 and type-4), so the model checks the stream the CP will
// read: barrier, indirect buffers, the +4 completion timestamp, NOP fill on a
// wrap, refusal without enough space, and the fail-closed timeout.
#include "../package/BoardRuntime/CheeseburgerGpuEngine540.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace CheeseburgerGpu;

namespace {

constexpr uint32_t RingWords = 8192;
constexpr uint64_t RingBase = 0x2000000;
constexpr uint64_t FenceBase = 0x2015000;

struct Mock {
    std::vector<uint32_t> ring = std::vector<uint32_t>(RingWords, 0xffffffff);
    std::vector<uint32_t> reg = std::vector<uint32_t>(0x40000 / 4, 0);
    volatile uint32_t fence[1024] = {};
    uint32_t wptr = 0;
    bool completeFence = true;   // false: the CP consumes the ring but never fences
    bool wedged = false;         // false: reads are fine; true after a fault
    unsigned kicks = 0, delays = 0;
    uint32_t lastOpcode = 0, seenIbs = 0;
    bool sawBarrierBeforeIb = false;

    uint32_t read(uint32_t o) {
        if (wedged) return 0xffffffffu;
        return reg[o / 4];
    }
    void write(uint32_t o, uint32_t v) {
        if (wedged) return;
        reg[o / 4] = v;
        if (o == RBWrite) run(v);
    }
    void delay(unsigned) { ++delays; }
    void clean(volatile void *, unsigned) {}
    void publish() {}

    // Executes the stream between wptr and the new write pointer.
    void run(uint32_t next) {
        ++kicks;
        uint32_t at = wptr;
        while (at != next) {
            const uint32_t w = ring[at];
            if (w == 0xffffffffu) { wedged = true; return; }  // unfilled tail
            const uint32_t type = w >> 28;
            uint32_t len = 1;
            if (type == 7) {
                const uint32_t count = w & 0x3fff, op = (w >> 16) & 0x7f;
                lastOpcode = op;
                if (op == 0x46 && count == 4) {
                    const uint64_t addr = uint64_t(ring[(at + 2) & (RingWords - 1)]) |
                        (uint64_t(ring[(at + 3) & (RingWords - 1)]) << 32);
                    if (completeFence && addr >= FenceBase && addr < FenceBase + 4096)
                        fence[(addr - FenceBase) / 4] = ring[(at + 4) & (RingWords - 1)];
                } else if (op == 0x12) {
                    if (seenIbs == 0) sawBarrierBeforeIb = true;
                } else if (op == 0x3f) {
                    ++seenIbs;
                }
                len = 1 + count;
            } else if (type == 4) {
                len = 1 + (w & 0x3f);
            } else if (type == 0) {
                len = 1;  // CP_NOP
            } else {
                wedged = true; return;
            }
            at = (at + len) & (RingWords - 1);
        }
        wptr = next;
        reg[RBRead / 4] = next;
    }
};

int failures = 0;
void check(bool ok, const char *what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

}  // namespace

int main() {
    // 1. One user IB: barrier, 0x3f, barrier, +4 timestamp; fence completes.
    {
        Mock m;
        m.fence[1] = 0;
        Engine540<Mock> e(m);
        check(e.adopt(m.ring.data(), RingWords, RingBase, m.fence, FenceBase, 2), "adopt");
        const Ib540 ib{0x4000000, 4};
        SubmitRecord r;
        const int rc = e.submit(&ib, 1, 3, 1000, r);
        check(rc == 0, "submit success");
        check(r.fenced && e.lastCompleted() == 3, "fence recorded");
        check(m.seenIbs == 1, "one indirect buffer");
        check(m.sawBarrierBeforeIb, "barrier before the IB");
        check(r.words == 2 + 2 + 4 + 2 + 2 + 5, "stream words");
        check(m.fence[0] == 0 && m.fence[1] == 3, "timestamp at +4 only");
        check(e.ready(), "engine still ready");
        // 2. A second stream is allowed once the first completed.
        const Ib540 two{0x4001000, 8};
        SubmitRecord r2;
        check(e.submit(&two, 1, 4, 1000, r2) == 0, "second submit");
        check(e.lastCompleted() == 4, "second fence");
    }
    // 3. Ring wrap: NOP fill to the end, then the stream starts at zero.
    {
        Mock m;
        m.wptr = RingWords - 4;
        m.reg[RBWrite / 4] = m.wptr;
        m.reg[RBRead / 4] = m.wptr;
        Engine540<Mock> e(m);
        check(e.adopt(m.ring.data(), RingWords, RingBase, m.fence, FenceBase, 0), "adopt wrap");
        const Ib540 ib{0x4000000, 4};
        SubmitRecord r;
        check(e.submit(&ib, 1, 1, 1000, r) == 0, "wrapped submit");
        check(r.wrapped, "wrap reported");
        check(m.ring[RingWords - 1] == packet7(0x10, 0), "tail NOP filled");
        check(m.ring[0] == packet7(0x12, 0), "stream restarts at zero");
        check(e.lastCompleted() == 1, "wrapped fence");
    }
    // 4. A write cursor outside the ring is not adopted.
    {
        Mock m;
        m.reg[RBWrite / 4] = RingWords;
        Engine540<Mock> e(m);
        check(!e.adopt(m.ring.data(), RingWords, RingBase, m.fence, FenceBase, 0), "bad cursor refused");
    }
    // 5. A CP that consumes but never fences: timeout, engine broken, no retry.
    {
        Mock m;
        m.completeFence = false;
        Engine540<Mock> e(m);
        check(e.adopt(m.ring.data(), RingWords, RingBase, m.fence, FenceBase, 0), "adopt timeout");
        const Ib540 ib{0x4000000, 4};
        SubmitRecord r;
        check(e.submit(&ib, 1, 1, 50, r) == -4, "timeout rc");
        check(r.timedOut && !r.fenced && r.stage == 5, "timeout record");
        check(!e.ready() && e.halted(), "engine broken after timeout");
        SubmitRecord again;
        check(e.submit(&ib, 1, 2, 50, again) == -1, "no submit after timeout");
    }
    // 6. A cursor the producer did not leave breaks the engine before a kick.
    {
        Mock m;
        Engine540<Mock> e(m);
        check(e.adopt(m.ring.data(), RingWords, RingBase, m.fence, FenceBase, 0), "adopt cursor");
        m.reg[RBRead / 4] = 64;  // CP claims progress the engine never made
        const Ib540 ib{0x4000000, 4};
        SubmitRecord r;
        check(e.submit(&ib, 1, 1, 1000, r) == -2, "cursor mismatch rc");
        check(r.broken && !e.ready(), "cursor mismatch breaks");
    }
    // 7. Encoder packets match sagit's encodings.
    {
        uint32_t w[64];
        Encoder540 e(w, 64);
        check(e.memToMem(0x1000, 0x2000), "mem2mem");
        check(w[0] == packet7(0x73, 5) && w[1] == (1u << 30) && w[4] == 0x2000, "mem2mem words");
        const uint32_t sent[2] = {0x11, 0x22};
        Encoder540 m2(w, 64);
        check(m2.memWrite(0x3000, sent, 2), "memwrite");
        check(w[0] == packet7(0x3d, 4) && w[1] == 0x3000 && w[3] == 0x11, "memwrite words");
        Encoder540 e3(w, 64);
        check(e3.fence(FenceBase, 9), "fence encode");
        check(w[1] == (4u | (1u << 31)) && w[2] == uint32_t(FenceBase + 4) && w[4] == 9, "fence words");
        Encoder540 tiny(w, 2);
        check(!tiny.fence(FenceBase, 9), "short buffer refused");
    }
    if (failures) { std::printf("ENGINE540_MODEL_FAIL %d\n", failures); return 1; }
    std::printf("ENGINE540_MODEL_PASS\n");
    return 0;
}
