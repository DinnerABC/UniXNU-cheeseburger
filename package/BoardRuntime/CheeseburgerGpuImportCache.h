// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace CheeseburgerGpu {
struct ImportedCacheState {
    bool pending=false,failed=false;
    uint32_t fence=0;
};

template <class Publish>
inline int publishImportedBuffer(ImportedCacheState &state, unsigned holds, Publish publish) {
    if(state.pending||state.failed||holds)return -1;
    const int rc=publish();
    if(rc){state.failed=true;return rc;}
    state.pending=true;state.fence=0;
    return 0;
}

template <class Acquire>
inline int acquireImportedBuffer(ImportedCacheState &state, uint32_t fence, Acquire acquire) {
    if(state.failed||!state.pending||!fence||state.fence!=fence)return -1;
    const int rc=acquire();
    if(rc){state.failed=true;return rc;}
    state.pending=false;state.fence=0;
    return 0;
}

template <class Segment, class Maintain>
inline int maintainImportedPages(uint64_t bytes, uint64_t ramBase, uint64_t ramBytes,
                                 Segment segment, Maintain maintain) {
    if (!bytes || (bytes&4095) || !ramBytes || ramBytes>UINT64_MAX-ramBase) return -1;
    // Validate every page before any cache operation can change ownership.
    for (uint64_t offset=0;offset<bytes;offset+=4096) {
        uint64_t length=0,physical=segment(offset,length);
        if (!physical || (physical&4095) || length<4096 || physical<ramBase ||
            physical-ramBase>=ramBytes || 4096>ramBytes-(physical-ramBase)) return -1;
    }
    for (uint64_t offset=0;offset<bytes;offset+=4096) {
        uint64_t length=0,physical=segment(offset,length);
        if (!physical || (physical&4095) || length<4096 || physical<ramBase ||
            physical-ramBase>=ramBytes || 4096>ramBytes-(physical-ramBase)) return -1;
        const int status=maintain(physical,4096);
        if (status) return status;
    }
    return 0;
}
}
