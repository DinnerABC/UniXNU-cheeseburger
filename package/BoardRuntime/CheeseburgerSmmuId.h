// SPDX-License-Identifier: GPL-2.0-only
// Qualcomm/ARM SMMU ID2 width decode. The vendor layout is IAS bits 0..3 and
// OAS bits 4..7 (UBS is bits 8..11). Encodings 0..6 map to the supported
// widths; 7..15 are reserved. The table must never be indexed unchecked: a
// reserved field previously read past the array.
#pragma once
#include <stdint.h>

namespace CheeseburgerGpu {

struct IdWidths {
    unsigned ias = 0, oas = 0;
};

inline bool decodeIdWidths(uint32_t id2, IdWidths &widths) {
    static const unsigned table[7] = {32, 36, 40, 42, 44, 48, 52};
    const unsigned ias = id2 & 0xf, oas = (id2 >> 4) & 0xf;
    if (ias >= sizeof table / sizeof table[0] || oas >= sizeof table / sizeof table[0]) return false;
    widths.ias = table[ias];
    widths.oas = table[oas];
    return true;
}

}  // namespace CheeseburgerGpu
