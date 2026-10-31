// SPDX-License-Identifier: GPL-2.0-only
// Read-only SIP/PAS availability probe. It uses the same SMCCC transport as
// the planned ZAP loader but performs no PAS state change and touches no GPU
// register. The result tells us whether this EL1 kernel can reach the secure
// monitor's SCM/PAS services before any image is loaded.
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "QcomPAS.h"

extern "C" {
volatile uint64_t cb_pas[16];
}

namespace {
struct ScmBackend {
    void smc(QcomSecure::Registers &r) {
        register uint64_t x0 __asm__("x0") = r.x[0];
        register uint64_t x1 __asm__("x1") = r.x[1];
        register uint64_t x2 __asm__("x2") = r.x[2];
        register uint64_t x3 __asm__("x3") = r.x[3];
        register uint64_t x4 __asm__("x4") = r.x[4];
        register uint64_t x5 __asm__("x5") = r.x[5];
        register uint64_t x6 __asm__("x6") = r.x[6];
        register uint64_t x7 __asm__("x7") = r.x[7];
        __asm__ volatile("dsb sy\n\tsmc #0\n\tdsb sy\n\tisb"
                         : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
                           "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
                         :
                         : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
                           "x16", "x17", "cc", "memory");
        r = {{x0, x1, x2, x3, x4, x5, x6, x7}};
    }
    void sleep(unsigned ms) { IOSleep(ms); }
};

static uint64_t present(QcomSecure::Transport<ScmBackend> &t, unsigned service, unsigned command) {
    const auto r = t.available(service, command);
    return r.status == QcomSecure::Status::OK && r.value == 1 ? 1 : 0;
}
}  // namespace

extern "C" void cheeseburger_gpu_pas_probe(void) {
    for (unsigned i = 0; i < 16; ++i) cb_pas[i] = 0xdeadbeef;
    ScmBackend io;
    QcomSecure::Transport<ScmBackend> t(io);
    const auto probe = t.probe();
    cb_pas[0] = unsigned(probe);
    cb_pas[1] = t.is64() ? 1 : 0;
    cb_pas[2] = present(t, 6, 1);   // SCM availability service
    cb_pas[3] = present(t, 1, 10);  // PAS resume (remote warm state)
    cb_pas[4] = present(t, 2, 1);   // PAS init image
    cb_pas[5] = present(t, 2, 2);   // PAS mem setup
    cb_pas[6] = present(t, 2, 5);   // PAS auth and reset
    cb_pas[7] = present(t, 2, 6);   // PAS shutdown
    cb_pas[8] = present(t, 2, 7);   // PAS supported
    const auto sup = t.supported();
    cb_pas[9] = sup.status == QcomSecure::Status::OK ? sup.value : 0xdeadbeef;
    cb_pas[10] = t.is64() ? 1 : 0;
    IOLog("CB GPU PAS: st=%u smc64=%u scm=%llu resume=%llu init=%llu setup=%llu auth=%llu down=%llu sup7=%llu sup=%llx\n",
          unsigned(probe), unsigned(cb_pas[1]), cb_pas[2], cb_pas[3], cb_pas[4], cb_pas[5],
          cb_pas[6], cb_pas[7], cb_pas[8], cb_pas[9]);
}
