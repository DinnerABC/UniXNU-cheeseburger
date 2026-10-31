// SPDX-License-Identifier: GPL-2.0-only
// Vendor arm-smmu micro-MMU control (IMPL_DEF1, base+0x6000; DTB
// qcom,micro-mmu-control on the kgsl SMMU). LOCAL_HALT_REQ (bit 2) requests a
// halt; IDLE (bit 3) is the acknowledgement/idle status. arm_smmu_resume()
// clears the request and does not wait for IDLE to drop (an idle pipeline has
// no traffic to report), so success means the request bit reads back as 0.
// The register is only a security-protected write without qcom,enable-static-cb;
// this board's DTB does not set that property, so a surviving request bit is a
// real failure and the caller must not start the CP.
#pragma once
#include <stdint.h>

namespace CheeseburgerGpu {

enum class MicroResult : int32_t {
    Intact = 0,   // request bit was already clear
    Resumed = 1,  // request bit was set and cleared successfully
    Timeout = -1, // write did not clear the request bit
    Invalid = -2, // unreadable register (0xffffffff)
};

struct MicroState { MicroResult result; uint32_t before, after; };

template <class IO> MicroState resumeMicroMmu(IO &io, unsigned attempts) {
    const uint32_t before = io.read(0x6000);
    if (before == 0xffffffffu) return {MicroResult::Invalid, before, before};
    if (!(before & (1u << 2))) return {MicroResult::Intact, before, before};
    io.mark("smresume");
    io.write(0x6000, before & ~(1u << 2));
    uint32_t now = before;
    for (unsigned i = 0; i < attempts; ++i) {
        now = io.read(0x6000);
        if (now == 0xffffffffu) return {MicroResult::Invalid, before, now};
        if (!(now & (1u << 2))) return {MicroResult::Resumed, before, now};
        io.delay(10);
    }
    return {MicroResult::Timeout, before, now};
}

}  // namespace CheeseburgerGpu
