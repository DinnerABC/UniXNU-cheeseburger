#pragma once
#include <stdint.h>
namespace CheeseburgerGpu {
// PAR[0] is the fault flag; bits [47:12] contain the translated page.
inline bool atosMatches(int status, uint64_t par, uint64_t expected) {
    constexpr uint64_t page = 0x0000fffffffff000ULL;
    return status == 0 && !(par & 1) && !(expected & 4095) &&
        !(expected & ~page) && (par & page) == expected;
}
}
