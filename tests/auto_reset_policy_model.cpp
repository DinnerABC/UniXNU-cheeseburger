// SPDX-License-Identifier: GPL-2.0-only
// Host model for CheeseburgerAutoResetPolicy. It drives the same predicates the
// kernel threads use and proves: the main fires at the deadline without any log
// input, the guard fires at most once and only as a backup, and creation
// failures cannot leave the device without a reset attempt.
#include "../package/BoardRuntime/CheeseburgerAutoResetPolicy.h"

#include <cstdio>
#include <cstdlib>

using namespace CheeseburgerAutoReset;

static int failures;
static void check(bool ok, const char *what) {
    if (!ok) { std::printf("FAIL %s\n", what); ++failures; }
}

struct Timeline {
    State s;
    unsigned main_fire = 0, guard_fire = 0;
    unsigned deadline, grace;

    Timeline(unsigned d, unsigned g) : deadline(d), grace(g) {}

    // Advance second by second; a main call that resets the device stops the
    // timeline (there is no CPU left to run the guard).
    void run(unsigned until, bool main_resets, bool main_stuck = false) {
        for (unsigned t = 1; t <= until; ++t) {
            if (main_should_fire(s, t, deadline)) {
                s.main_fired = true; ++main_fire;
                if (main_resets) return;           // device reset: no guard
                if (!main_stuck) s.main_returned = true;
            }
            if (guard_should_fire(s, t, deadline, grace)) {
                s.guard_fired = true; ++guard_fire;
                return;
            }
        }
    }
};

int main() {
    // Normal expiry: one main attempt, no guard.
    {
        Timeline t(400, 10);
        t.s.main_started = t.s.guard_started = true;
        t.run(420, true);
        check(t.main_fire == 1 && t.guard_fire == 0, "normal/main-only");
    }
    // Main creation failed: the guard performs the only attempt.
    {
        Timeline t(400, 10);
        t.s.guard_started = true;
        t.run(420, false);
        check(t.main_fire == 0 && t.guard_fire == 1, "main-create-fail/guard");
    }
    // Main PSCI returned without resetting: the guard retries exactly once.
    {
        Timeline t(400, 10);
        t.s.main_started = t.s.guard_started = true;
        t.run(420, false, true);
        check(t.main_fire == 1 && t.guard_fire == 1, "main-returned/guard-once");
        for (unsigned k = 0; k < 20; ++k)
            check(!guard_should_fire(t.s, 500 + k, 400, 10), "guard/no-repeat");
    }
    // Main fired but hung: the guard still rescues at deadline+grace.
    {
        Timeline t(400, 10);
        t.s.main_started = t.s.guard_started = true;
        t.run(420, false, true);
        check(t.main_fire == 1 && t.guard_fire == 1, "main-stuck/guard");
    }
    // Guard creation failed: the main attempt is the only one.
    {
        Timeline t(400, 10);
        t.s.main_started = true;
        t.run(420, true);
        check(t.main_fire == 1 && t.guard_fire == 0, "guard-create-fail/main-only");
    }
    // A blocked log is not an input: decisions depend only on state and time.
    {
        State s; s.main_started = s.guard_started = true;
        check(main_should_fire(s, 399, 400) == false, "log-irrelevant/early");
        check(main_should_fire(s, 400, 400) == true, "log-irrelevant/due");
    }
    // Before its deadline the guard never fires, even when nothing started.
    {
        Timeline t(400, 10);
        t.s.guard_started = true;
        t.run(409, false);
        check(t.guard_fire == 0, "guard/waits-grace");
    }
    // A panic screen is terminal: neither worker may reset the device, so the
    // panic text stays readable; the normal deadline is unaffected.
    {
        Timeline t(400, 10);
        t.s.main_started = t.s.guard_started = true;
        t.s.panic = true;
        t.run(500, true);
        check(t.main_fire == 0 && t.guard_fire == 0, "panic/hold");
        State s; s.main_started = s.guard_started = true; s.panic = true;
        check(!main_should_fire(s, 400, 400) && !guard_should_fire(s, 410, 400, 10), "panic/predicates");
    }
    if (failures) std::printf("auto_reset_policy_model: FAIL %d\n", failures);
    else std::printf("auto_reset_policy_model: PASS\n");
    return failures ? 1 : 0;
}
