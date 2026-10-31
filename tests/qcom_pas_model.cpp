// SPDX-License-Identifier: GPL-2.0-only
// Host model for the SIP/PAS transport and ZAP state machine. No hardware.
#include "../package/BoardRuntime/QcomPAS.h"

#include <cstdio>

using namespace QcomSecure;

static unsigned failures = 0;
static void check(bool ok, const char *name) {
    if (!ok) {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

struct Backend {
    int probeMode = 0;  // 0: 64-bit ok, 1: 64-bit unknown then 32-bit, 2: unsupported
    int pasDeny = 0, pasAmbig = 0, pasBusy = 0, supportedValue = 1;
    unsigned sleeps = 0, calls = 0;
    void smc(Registers &r) {
        ++calls;
        const uint32_t fn = uint32_t(r.x[0]), svc = (fn >> 8) & 0xff, cmd = fn & 0xff;
        if (svc == 6) {
            if (probeMode == 2) { r = {{uint64_t(-4), 0, 0, 0, 0, 0, 0, 0}}; return; }
            if (probeMode == 1 && (fn & 0x40000000u)) { r = {{uint64_t(-1), 0, 0, 0, 0, 0, 0, 0}}; return; }
            r = {{0, 1, 0, 0, 0, 0, 0, 0}};
            return;
        }
        if (svc == 2) {
            if (cmd == 7) { r = {{0, uint64_t(supportedValue), 0, 0, 0, 0, 0, 0}}; return; }
            if (pasBusy) { --pasBusy; r = {{uint64_t(-12), 0, 0, 0, 0, 0, 0, 0}}; return; }
            if (pasAmbig) { r = {{uint64_t(-99), 0, 0, 0, 0, 0, 0, 0}}; return; }
            if (pasDeny) { r = {{0, 1, 0, 0, 0, 0, 0, 0}}; return; }
            r = {{0, 0, 0, 0, 0, 0, 0, 0}};
            return;
        }
        if (svc == 1) { r = {{0, 0, 0, 0, 0, 0, 0, 0}}; return; }
        r = {{uint64_t(-3), 0, 0, 0, 0, 0, 0, 0}};
    }
    void sleep(unsigned) { ++sleeps; }
};

int main() {
    {
        Backend b;
        Transport<Backend> t(b);
        check(t.probe() == Status::OK && t.is64(), "probe/64");
        check(t.available(2, 1).status == Status::OK && t.available(2, 1).value == 1, "available/pas");
        check(t.supported().status == Status::OK && t.supported().value == 1, "supported");
        PAS<Transport<Backend>> p(t);
        check(p.init(0x100000) == Status::OK && p.phase() == Phase::Metadata, "flow/init");
        check(p.setup(0x200000, 0x1000) == Status::OK && p.phase() == Phase::Region, "flow/setup");
        check(p.authenticate() == Status::OK && p.phase() == Phase::Authenticated, "flow/auth");
        check(p.shutdown() == Status::OK && p.phase() == Phase::Empty, "flow/shutdown");
        check(p.resume() == Status::OK && p.phase() == Phase::Warm, "flow/resume");
        check(p.release() && p.phase() == Phase::Empty, "flow/release");
    }
    {
        Backend b; b.probeMode = 1;
        Transport<Backend> t(b);
        check(t.probe() == Status::OK && !t.is64(), "probe/fallback32");
        check(t.pas(1, 0x100000000ULL).status == Status::Invalid, "pas32/address-limit");
    }
    {
        Backend b; b.probeMode = 2;
        Transport<Backend> t(b);
        check(t.probe() == Status::Unsupported, "probe/unsupported");
    }
    {
        Backend b; b.pasDeny = 1;
        Transport<Backend> t(b); check(t.probe() == Status::OK, "deny/probe");
        PAS<Transport<Backend>> p(t);
        check(p.init(0x100000) == Status::Denied && p.phase() == Phase::Empty, "deny/init");
        b.pasDeny = 0;
        check(p.init(0x100000) == Status::OK, "deny/retry");
    }
    {
        Backend b; b.pasAmbig = 1;
        Transport<Backend> t(b); check(t.probe() == Status::OK, "ambig/probe");
        PAS<Transport<Backend>> p(t);
        check(p.init(0x100000) == Status::Ambiguous && p.phase() == Phase::Faulted, "ambig/init");
        check(p.init(0x100000) == Status::Invalid, "ambig/locked");
    }
    {
        Backend b; b.pasBusy = 1;
        Transport<Backend> t(b); check(t.probe() == Status::OK, "busy/probe");
        PAS<Transport<Backend>> p(t);
        check(p.init(0x100000) == Status::OK && b.sleeps == 1, "busy/retry");
    }
    {
        Backend b;
        Transport<Backend> t(b); check(t.probe() == Status::OK, "order/probe");
        PAS<Transport<Backend>> p(t);
        check(p.authenticate() == Status::Invalid, "order/auth-first");
        check(p.setup(0x200000, 0x1000) == Status::Invalid, "order/setup-first");
    }
    if (failures) {
        std::printf("RESULT FAIL %u\n", failures);
        return 1;
    }
    std::printf("RESULT PASS\n");
    return 0;
}
