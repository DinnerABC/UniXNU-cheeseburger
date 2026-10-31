// SPDX-License-Identifier: GPL-2.0-only
// Shared AArch64 stage-1 walk for diagnostics. The kernel supplies live tables
// through a register-free accessor; host tests supply a synthetic table set so
// level selection, missing entries, invalid descriptors and boundaries are
// covered without touching hardware.
#pragma once
#include <stdint.h>

namespace CheeseburgerGpu {

// A must provide:
//   root()                 -> nonzero table token (kernel: gRootVA pointer)
//   entry(token, index)    -> raw 64-bit descriptor
//   next(pa)               -> token for the next-level table or 0
// Levels startLevel..2 are walked, then the leaf lives in the L3 table at
// shift 12. A 36-bit IAS starts at level 1 (T0SZ=28); a 40-bit IAS at 0.
template <class A> int lookupLeaf(A &a, uint64_t iova, uint64_t &pte, unsigned startLevel = 0) {
    pte = 0;
    auto t = a.root();
    if (!t) return -1;
    for (unsigned level = startLevel; level < 3; ++level) {
        const uint64_t e = a.entry(t, unsigned((iova >> (39 - 9 * level)) & 511));
        if (!e) return -2;
        if ((e & 0xfffULL) != 3) return -4;
        t = a.next(e & ~0xfffULL);
        if (!t) return -5;
    }
    pte = a.entry(t, unsigned((iova >> 12) & 511));
    if (!pte) return -2;
    return (pte & 3) == 3 ? 0 : -3;
}

}  // namespace CheeseburgerGpu
