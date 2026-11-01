// Host model for the vendor SMMU micro-MMU resume. It proves the request/ack
// handling offline: an already-clear request is left untouched, a set request
// is cleared, a write that does not stick is a timeout, and an unreadable
// register is invalid. Hardware success is still a separate observation.
#include "../package/BoardRuntime/CheeseburgerSmmuMicro.h"
#include "../package/BoardRuntime/CheeseburgerSmmuId.h"
#include "../package/BoardRuntime/CheeseburgerAtosGate.h"
#include <cstdio>

using namespace CheeseburgerGpu;

struct Mock {
    uint32_t reg[0x8000 / 4] = {};
    unsigned writes = 0, delays = 0, marks = 0;
    bool stick = true;
    uint32_t read(unsigned offset) { return reg[offset / 4]; }
    void write(unsigned offset, uint32_t value) {
        ++writes;
        if (stick) reg[offset / 4] = value;
    }
    void delay(unsigned) { ++delays; }
    void mark(const char *) { ++marks; }
};

static unsigned failures = 0;

static void check(bool ok, const char *name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

int main() {
    check(CheeseburgerGpu::atosMatches(0, 0x12099b000ULL, 0x12099b000ULL), "atos/match");
    check(!CheeseburgerGpu::atosMatches(-2, ~0ULL, 0x12099b000ULL), "atos/timeout");
    check(!CheeseburgerGpu::atosMatches(0, 0x12099b001ULL, 0x12099b000ULL), "atos/fault");
    check(!CheeseburgerGpu::atosMatches(0, 0x12099c000ULL, 0x12099b000ULL), "atos/wrong-page");
    check(!CheeseburgerGpu::atosMatches(0, 0x12099b000ULL, 0x12099b001ULL), "atos/unaligned");
    {
        CheeseburgerGpu::IdWidths w;
        check(CheeseburgerGpu::decodeIdWidths(0x7511, w) && w.ias == 36 && w.oas == 36,
              "id2/device-36-36");
        check(CheeseburgerGpu::decodeIdWidths(0x66, w) && w.ias == 52 && w.oas == 52,
              "id2/max-52-52");
        check(CheeseburgerGpu::decodeIdWidths(0x0, w) && w.ias == 32 && w.oas == 32,
              "id2/min-32-32");
        check(!CheeseburgerGpu::decodeIdWidths(0x7, w), "id2/reserved-ias-7");
        check(!CheeseburgerGpu::decodeIdWidths(0x70, w), "id2/reserved-oas-7");
        check(!CheeseburgerGpu::decodeIdWidths(0xff, w), "id2/reserved-all");
        check(!CheeseburgerGpu::decodeIdWidths(0xffffffffu, w), "id2/all-ones");
    }
    {
        Mock m; m.reg[0x6000 / 4] = 0x178;  // request clear, idle set
        const auto s = resumeMicroMmu(m, 3000);
        check(s.result == MicroResult::Intact && s.before == 0x178 && s.after == 0x178,
              "micro/intact");
        check(m.writes == 0 && m.marks == 0, "micro/intact-no-write");
    }
    {
        Mock m; m.reg[0x6000 / 4] = 0x17c;  // request set, idle set
        const auto s = resumeMicroMmu(m, 3000);
        check(s.result == MicroResult::Resumed && s.before == 0x17c && s.after == 0x178,
              "micro/resumed");
        check(m.writes == 1 && m.marks == 1, "micro/resumed-one-write");
        check(m.reg[0x6000 / 4] == 0x178, "micro/resumed-clears-only-bit2");
    }
    {
        Mock m; m.reg[0x6000 / 4] = 0x17c; m.stick = false;
        const auto s = resumeMicroMmu(m, 4);
        check(s.result == MicroResult::Timeout && s.before == 0x17c && s.after == 0x17c,
              "micro/timeout");
        check(m.writes == 1 && m.delays == 4, "micro/timeout-bounded");
    }
    {
        Mock m; m.reg[0x6000 / 4] = 0xffffffffu;
        const auto s = resumeMicroMmu(m, 4);
        check(s.result == MicroResult::Invalid && s.before == 0xffffffffu,
              "micro/invalid-before");
        check(m.writes == 0, "micro/invalid-no-write");
    }
    {
        struct Poison {
            uint32_t reg[0x8000 / 4] = {};
            unsigned writes = 0;
            uint32_t read(unsigned offset) { return reg[offset / 4]; }
            void write(unsigned offset, uint32_t) {
                ++writes;
                reg[offset / 4] = 0xffffffffu;
            }
            void delay(unsigned) {}
            void mark(const char *) {}
        } p;
        p.reg[0x6000 / 4] = 0x17c;
        const auto s = resumeMicroMmu(p, 4);
        check(s.result == MicroResult::Invalid && s.after == 0xffffffffu,
              "micro/invalid-after");
    }
    if (failures) {
        std::printf("SMMU_MICRO_FAIL %u\n", failures);
        return 1;
    }
    std::printf("SMMU_MICRO_PASS\n");
    return 0;
}
