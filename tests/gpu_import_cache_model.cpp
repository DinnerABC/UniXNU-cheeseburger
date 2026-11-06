// SPDX-License-Identifier: MIT
#include "../package/BoardRuntime/CheeseburgerGpuImportCache.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace CheeseburgerGpu;

int main() {
    const uint64_t ram=0x100000000ull;
    std::vector<uint64_t> pages{ram+0x3000,ram,ram+0x7000,ram+0x1000};
    unsigned reads=0,ops=0;
    auto segment=[&](uint64_t off,uint64_t &n){++reads;n=4096;return pages.at(off/4096);};
    auto maintain=[&](uint64_t pa,uint64_t n){assert(reads>=4&&pa>=ram&&n==4096);++ops;return 0;};
    assert(maintainImportedPages(16384,ram,0x10000,segment,maintain)==0&&ops==4);
    for(unsigned i=0;i<4;++i) {
        for(uint64_t bad:{uint64_t(0),ram-4096,ram+1,ram+0x10000}) {
            auto p=pages;pages[i]=bad;reads=ops=0;
            assert(maintainImportedPages(16384,ram,0x10000,segment,maintain)!=0&&ops==0);
            pages=p;
        }
    }
    ops=0;
    assert(maintainImportedPages(16384,ram,0x10000,
        [&](uint64_t off,uint64_t &n){n=off==12288?4095:4096;return pages[off/4096];},maintain)!=0&&ops==0);
    assert(maintainImportedPages(16384,ram,0xffff,segment,
        [&](uint64_t,uint64_t){++ops;return -7;})==-7&&ops==1);
    for(uint64_t bytes:{uint64_t(0),uint64_t(1),uint64_t(4095)})
        assert(maintainImportedPages(bytes,ram,0x10000,segment,maintain)!=0);
    assert(maintainImportedPages(4096,UINT64_MAX-4095,4096,segment,maintain)!=0);
    assert(maintainImportedPages(4096,ram,4095,segment,maintain)!=0);
    reads=ops=0;
    assert(maintainImportedPages(4096,ram,0x10000,
        [&](uint64_t,uint64_t &n){n=4096;return ++reads==1?ram:ram+1;},maintain)!=0&&ops==0);
    {
        ImportedCacheState state;
        std::vector<unsigned> order;
        assert(publishImportedBuffer(state,1,[&]{order.push_back(1);return 0;})!=0&&order.empty());
        assert(publishImportedBuffer(state,0,[&]{order.push_back(1);return 0;})==0);
        assert(state.pending&&!state.failed);
        order.push_back(2); // submit, then timeout: no acquire or release.
        assert(publishImportedBuffer(state,0,[&]{order.push_back(9);return 0;})!=0);
        assert(acquireImportedBuffer(state,14,[&]{order.push_back(9);return 0;})!=0);
        state.fence=14;order.push_back(3); // genuine matching fence completion.
        assert(acquireImportedBuffer(state,13,[&]{order.push_back(9);return 0;})!=0);
        assert(acquireImportedBuffer(state,14,[&]{order.push_back(4);return 0;})==0);
        assert(!state.pending&&!state.failed&&state.fence==0);
        assert((order==std::vector<unsigned>{1,2,3,4}));
    }
    for(bool acquire:{false,true}) {
        ImportedCacheState state;
        assert(publishImportedBuffer(state,0,[&]{return acquire?0:-11;})==(acquire?0:-11));
        if(acquire){state.fence=14;assert(acquireImportedBuffer(state,14,[]{return -12;})==-12);}
        assert(state.failed);
        unsigned calls=0;
        assert(publishImportedBuffer(state,0,[&]{++calls;return 0;})!=0);
        assert(acquireImportedBuffer(state,14,[&]{++calls;return 0;})!=0&&calls==0);
    }
    // Physically tagged CPU alias model: clean+invalidate before DMA, only
    // invalidate after DMA. A post-clean of a dirty stale alias corrupts DMA.
    {
        uint32_t ramWord=0,cpuWord=0x13579bdf;bool dirty=true,valid=true;
        ImportedCacheState state;
        assert(publishImportedBuffer(state,0,[&]{if(dirty)ramWord=cpuWord;dirty=false;valid=false;return 0;})==0);
        assert(ramWord==0x13579bdf&&!valid);
        ramWord=0xff0000ff;state.fence=14;
        assert(acquireImportedBuffer(state,14,[&]{valid=false;return 0;})==0);
        if(!valid){cpuWord=ramWord;valid=true;}
        assert(cpuWord==0xff0000ff);
        cpuWord=0x13579bdf;dirty=true; // forbidden CPU write during DMA ownership.
        if(dirty)ramWord=cpuWord;
        assert(ramWord!=0xff0000ff);
    }
    std::puts("RESULT PASS import cache pages/order/failure/alias model (not hardware)");
}
