// SPDX-License-Identifier: GPL-2.0-only
// One physically contiguous run of a buffer as it is handed to the SMMU
// batch mapper. IOVA and PA are page aligned; pages is the run length in 4 KiB
// units. Runs of one buffer are submitted in ascending IOVA order.
#pragma once
#include <stdint.h>
namespace CheeseburgerGpu {
struct MapRange { uint64_t iova; uint64_t pa; uint32_t pages; };
}
