// SPDX-License-Identifier: GPL-2.0-only
// Fence polling with one explicit time unit. The kernel's IODelay() takes
// microseconds while the wait budget is nanoseconds; keeping the conversion in
// one place prevents the previous mismatch where a 100 us sleep was accounted
// as 100 ns.
#pragma once
#include <stdint.h>

namespace CheeseburgerGpu {

// read() returns the current fence word; delay() sleeps one poll interval.
// poll_ns is the number of nanoseconds one delay() call represents. Returns 0
// when the fence reaches sequence, -2 on timeout. A zero poll interval polls
// once and then fails unless the fence is already there.
template <class Read, class Delay>
inline int waitFenceValue(Read read, Delay delay, uint32_t sequence,
                          uint64_t timeout_ns, uint64_t poll_ns) {
    uint64_t waited = 0;
    for (;;) {
        if (read() >= sequence) return 0;
        if (waited >= timeout_ns || !poll_ns) return -2;
        delay();
        waited += poll_ns;
    }
}

}  // namespace CheeseburgerGpu
