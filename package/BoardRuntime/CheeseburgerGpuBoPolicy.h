// SPDX-License-Identifier: GPL-2.0-only
// Host-testable teardown policy shared with the GPU service.
#pragma once
#include <stdint.h>

namespace CheeseburgerGpu {

// A mapped buffer may be released only when the engine is stopped or idle and
// the SMMU unmap (including its TLB sync) succeeded. Failure keeps the object:
// freeing pages that a CP could still reference is worse than leaking them.
template <class Idle, class Unmap>
inline bool releaseMappedBuffer(bool mapped, Idle idle, Unmap unmap) {
    if (!mapped) return true;
    // idle() returns 0 idle, -2 still running; -1 means the engine is halted
    // or abandoned, which is stopped and therefore safe.
    if (idle() == -2) return false;
    if (unmap() != 0) return false;
    return true;
}

// One live connection at a time. The generation prevents a closing client from
// clearing the gate of a newer connection: release() only acts for the
// generation it acquired.
struct ClientGate {
    bool open = false;
    uint64_t generation = 0;

    bool acquire(uint64_t &mine) {
        if (open) return false;
        open = true;
        mine = ++generation;
        return true;
    }
    void release(uint64_t mine) {
        if (open && mine == generation) open = false;
    }
};

}  // namespace CheeseburgerGpu
