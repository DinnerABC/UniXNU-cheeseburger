// SPDX-License-Identifier: GPL-2.0-only
// Host model for the fence wait helper. It pins the unit conversion: one poll
// interval is accounted in nanoseconds, so a 200 us budget with 50 us polls
// ends after four polls instead of treating the sleep as 50 ns.
#include "../package/BoardRuntime/CheeseburgerGpuWait.h"

#include <cstdio>
#include <cstdint>

using namespace CheeseburgerGpu;

static unsigned failures;

static void check(bool ok,const char *name){
    if(!ok){std::printf("FAIL %s\n",name);++failures;}
}

struct Fence {
    uint32_t value;
    unsigned reads;
    uint32_t succeed_after;   // number of reads before the value appears
    uint32_t read(){++reads;return reads>succeed_after?value:0;}
};
struct Delay { unsigned calls; void operator()(){++calls;} };

int main(){
    {
        Fence f{7,0,0};Delay d{};
        check(waitFenceValue([&]{return f.read();},[&]{d();},7,1000000000ull,50000ull)==0,"wait/immediate");
        check(f.reads==1&&d.calls==0,"wait/immediate-counts");
    }
    {
        Fence f{9,0,3};Delay d{};
        check(waitFenceValue([&]{return f.read();},[&]{d();},9,1000000000ull,50000ull)==0,"wait/after-polls");
        check(f.reads==4&&d.calls==3,"wait/after-polls-counts");
    }
    {
        Fence f{1,0,1000};Delay d{};
        check(waitFenceValue([&]{return f.read();},[&]{d();},2,200000ull,50000ull)==-2,"wait/timeout");
        check(d.calls==4,"wait/timeout-four-polls");
    }
    {
        Fence f{2,0,0};Delay d{};
        check(waitFenceValue([&]{return f.read();},[&]{d();},2,0,0)==0,"wait/zero-poll-ready");
    }
    {
        Fence f{1,0,0};Delay d{};
        check(waitFenceValue([&]{return f.read();},[&]{d();},2,0,0)==-2,"wait/zero-poll-timeout");
    }
    std::printf("RESULT %s %u\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
