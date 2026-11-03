// SPDX-License-Identifier: GPL-2.0-only
// Bounded automatic return: after cheeseburger-auto-reset=<seconds> from kernel
// start the main worker issues PSCI SYSTEM_RESET (SMC64 0x84000009) without
// touching IOLog, the persistent log or UFS, so a blocked log writer cannot
// keep the device from returning. A guard thread makes exactly one backup
// attempt at deadline+10 s. The record fields below carry creation/firing state;
// they are not proof that a reset happened, only the device's later location and
// a new boot id are.
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/sched_prim.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include "CheeseburgerAutoResetPolicy.h"
extern "C" {
// 0 started, 1 requested seconds, 2 kernel elapsed at start, 3 main fired,
// 4 main PSCI return, 5 main thread creation, 6 guard thread creation,
// 7 guard fired, 8 guard PSCI return, 9 main held by a panic screen,
// 10 guard held by a panic screen.
volatile uint64_t cb_reset[12];
// Seeded by the board's console mapping; the panic screen (patched into the
// prelinked kernel) draws its text through this vc_info and leaves a marker in
// the last row. Both are board/kernel ABI, not a private guess.
struct CbVcInfo { unsigned int height, width, depth, rowbytes; unsigned long baseaddr; };
extern struct CbVcInfo boot_progress_panic_vinfo __attribute__((weak_import));
}
namespace {
static volatile bool gMainStarted, gMainFired, gMainReturned;
static volatile bool gGuardStarted, gGuardFired;
static bool gResetStarted;

// The panic screen writes these two words into the first pixels of the last
// row after it has drawn and before it parks; nothing else uses that pair.
static bool panicScreenHolds(void) {
    if (!&boot_progress_panic_vinfo) return false;
    const struct CbVcInfo v = boot_progress_panic_vinfo;
    if (!v.baseaddr || !v.height || !v.rowbytes) return false;
    const volatile uint32_t *mark =
        (const volatile uint32_t *)(v.baseaddr + (unsigned long)(v.height - 1) * v.rowbytes);
    return mark[0] == 0xC0FFEE11u && mark[1] == 0x5CEEDA7Au;
}
// Default on: a panic must stay readable. cheeseburger-panic-hold=0 restores
// the unconditional bound for boots that need the old behaviour.
static bool panicHoldEnabled(void) {
    uint32_t hold = 1;
    if (!PE_parse_boot_argn("cheeseburger-panic-hold", &hold, sizeof hold)) return true;
    return hold != 0;
}

static CheeseburgerAutoReset::State resetSnapshot(void) {
    CheeseburgerAutoReset::State s;
    s.main_started = gMainStarted; s.main_fired = gMainFired; s.main_returned = gMainReturned;
    s.guard_started = gGuardStarted; s.guard_fired = gGuardFired;
    return s;
}
static uint64_t psciReset(void) {
    register uint64_t x0 __asm__("x0") = 0x84000009ULL;
    register uint64_t x1 __asm__("x1") = 0;
    register uint64_t x2 __asm__("x2") = 0;
    register uint64_t x3 __asm__("x3") = 0;
    __asm__ volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) :
        "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    return x0;
}
static unsigned resetSeconds(void) {
    uint32_t seconds = 0;
    if (!PE_parse_boot_argn("cheeseburger-auto-reset", &seconds, sizeof seconds) || !seconds) return 0;
    return seconds > 3600 ? 3600 : seconds;
}
static void waitUntil(uint64_t target) {
    uint64_t now = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &now);
    while (now < target) {
        uint64_t left = target - now;
        if (left > 1000000000ULL) left = 1000000000ULL;
        IOSleep((unsigned)((left + 999999ULL) / 1000000ULL));
        absolutetime_to_nanoseconds(mach_absolute_time(), &now);
    }
}
static void resetWorker(void *, wait_result_t) {
    const unsigned seconds = resetSeconds();
    if (!seconds) { thread_terminate(current_thread()); return; }
    waitUntil(uint64_t(seconds) * 1000000000ULL);
    CheeseburgerAutoReset::State state = resetSnapshot();
    state.panic = panicHoldEnabled() && panicScreenHolds();
    if (!CheeseburgerAutoReset::main_should_fire(state, seconds, seconds)) {
        if (state.panic) cb_reset[9] = 1;
        thread_terminate(current_thread());
        return;
    }
    gMainFired = true;
    cb_reset[3] = 1;
    // The reset path must not wait on the log/console/UFS. Nothing is written
    // here before PSCI; the last heartbeat tick is the evidence of intent.
    const uint64_t returned = psciReset();
    gMainReturned = true;
    cb_reset[4] = returned;
    // Best effort, after the state and the SMC: a blocked log cannot prevent
    // the guard from retrying.
    IOLog("CB auto reset: main PSCI returned %llx\n", (unsigned long long)returned);
    thread_terminate(current_thread());
}
static void resetGuard(void *, wait_result_t) {
    const unsigned seconds = resetSeconds();
    if (!seconds) { thread_terminate(current_thread()); return; }
    waitUntil(uint64_t(seconds + 10) * 1000000000ULL);
    CheeseburgerAutoReset::State state = resetSnapshot();
    state.panic = panicHoldEnabled() && panicScreenHolds();
    if (!CheeseburgerAutoReset::guard_should_fire(state, seconds + 10, seconds, 10)) {
        if (state.panic) cb_reset[10] = 1;
        thread_terminate(current_thread());
        return;
    }
    gGuardFired = true;
    cb_reset[7] = 1;
    const uint64_t returned = psciReset();
    cb_reset[8] = returned;
    IOLog("CB auto reset: guard PSCI returned %llx\n", (unsigned long long)returned);
    thread_terminate(current_thread());
}
}  // namespace
extern "C" void cheeseburger_auto_reset_start(void) {
    const unsigned seconds = resetSeconds();
    if (!seconds || gResetStarted) return;
    gResetStarted = true;
    cb_reset[0] = 1;
    cb_reset[1] = seconds;
    uint64_t now = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &now);
    cb_reset[2] = now / 1000000000ULL;
    thread_t t = nullptr;
    gMainStarted = kernel_thread_start(resetWorker, nullptr, &t) == KERN_SUCCESS;
    if (gMainStarted) thread_deallocate(t);
    cb_reset[5] = gMainStarted ? 1 : ~0ull;
    thread_t g = nullptr;
    gGuardStarted = kernel_thread_start(resetGuard, nullptr, &g) == KERN_SUCCESS;
    if (gGuardStarted) thread_deallocate(g);
    cb_reset[6] = gGuardStarted ? 1 : ~0ull;
    // Creation record only; it runs at start, not on the reset path.
    IOLog("CB auto reset: requested=%u s main=%d guard=%d\n", seconds, (int)gMainStarted, (int)gGuardStarted);
}
