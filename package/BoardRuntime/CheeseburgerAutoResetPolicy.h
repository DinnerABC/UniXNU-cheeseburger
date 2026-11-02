// SPDX-License-Identifier: GPL-2.0-only
// Deterministic decisions for the bounded auto-reset. The kernel path and the
// host model share these predicates; they never depend on IOLog, the persistent
// log or UFS, so a blocked log writer cannot decide whether the reset happens.
//
// The main worker fires once at the deadline. The guard is a single backup: it
// fires at deadline+grace only if the main worker never ran (creation failed or
// it never reached its fire point) or if its PSCI call returned without a reset.
// A main call that succeeds resets the device, so the guard cannot run; a guard
// that already fired is never repeated.
#pragma once

namespace CheeseburgerAutoReset {

struct State {
    bool main_started = false;   // the main worker thread was created
    bool main_fired = false;     // the main worker issued PSCI
    bool main_returned = false;  // PSCI returned without resetting
    bool guard_started = false;  // the guard thread was created
    bool guard_fired = false;    // the guard issued PSCI
};

inline bool main_should_fire(const State &s, unsigned elapsed, unsigned deadline) {
    return s.main_started && !s.main_fired && elapsed >= deadline;
}

inline bool guard_should_fire(const State &s, unsigned elapsed, unsigned deadline, unsigned grace) {
    // Exactly one backup attempt. A main PSCI that reset the device means this
    // thread never runs again; on a live CPU at deadline+grace the main either
    // never ran, returned without resetting, or is stuck, so the guard fires.
    return s.guard_started && !s.guard_fired && elapsed >= deadline + grace;
}

}  // namespace CheeseburgerAutoReset
