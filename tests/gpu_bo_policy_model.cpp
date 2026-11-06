// SPDX-License-Identifier: GPL-2.0-only
// Host model for the GPU service teardown policy: one live client, a closing
// client cannot clear a newer gate generation, and a mapped buffer is never
// released while the engine is running or after a failed SMMU unmap.
#include "../package/BoardRuntime/CheeseburgerGpuBoPolicy.h"

#include <cstdio>
#include <cstdint>

using namespace CheeseburgerGpu;

static unsigned failures;
static void check(bool ok,const char *name){
    if(!ok){std::printf("FAIL %s\n",name);++failures;}
}

int main(){
    {
        int idles=0,unmaps=0;
        bool ok=releaseMappedBuffer(false,[&]{++idles;return 0;},[&]{++unmaps;return 0;});
        check(ok&&idles==0&&unmaps==0,"release/unmapped-no-calls");
    }
    {
        int idles=0,unmaps=0;
        bool ok=releaseMappedBuffer(true,[&]{++idles;return 0;},[&]{++unmaps;return 0;});
        check(ok&&idles==1&&unmaps==1,"release/idle-success");
    }
    {
        int unmaps=0;
        bool ok=releaseMappedBuffer(true,[&]{return -2;},[&]{++unmaps;return 0;});
        check(!ok&&unmaps==0,"release/running-keeps-buffer");
    }
    {
        int unmaps=0;
        bool ok=releaseMappedBuffer(true,[&]{return 1;},[&]{++unmaps;return 0;});
        check(ok&&unmaps==1,"release/confirmed-stop-allows-unmap");
    }
    {
        int unmaps=0;
        bool ok=releaseMappedBuffer(true,[&]{return -1;},[&]{++unmaps;return 0;});
        check(!ok&&unmaps==0,"release/unknown-keeps-buffer");
    }
    {
        int unmaps=0;
        bool ok=releaseMappedBuffer(true,[&]{return -3;},[&]{++unmaps;return 0;});
        check(!ok&&unmaps==0,"release/unexpected-state-keeps-buffer");
    }
    {
        bool ok=releaseMappedBuffer(true,[&]{return 0;},[&]{return -4;});
        check(!ok,"release/unmap-failure-keeps-buffer");
    }
    {
        bool failed=false;int calls=0;
        check(completeImportedBuffer(false,failed,[&]{++calls;return 0;})&&calls==0,"complete/owned-no-call");
        check(completeImportedBuffer(true,failed,[&]{++calls;return 0;})&&calls==1&&!failed,"complete/import-success");
        check(!completeImportedBuffer(true,failed,[&]{++calls;return -7;})&&failed&&calls==2,"complete/failure-keeps-descriptor");
        check(!completeImportedBuffer(true,failed,[&]{++calls;return 0;})&&calls==2,"complete/no-retry-after-failure");
    }
    {
        ClientGate gate;uint64_t a=0,b=0,c=0,d=0;
        check(gate.acquire(a)&&a==1,"gate/acquire-a");
        check(!gate.acquire(b),"gate/second-refused");
        gate.release(0);
        check(gate.acquire(d)==false,"gate/bad-generation-ignored");
        gate.release(a);
        check(gate.acquire(b)&&b==2,"gate/acquire-b");
        gate.release(a); // late free of the old client
        check(!gate.acquire(c),"gate/old-release-does-not-clear-new");
        gate.release(b);
        check(gate.acquire(c)&&c==3,"gate/acquire-c");
        gate.release(c);
        check(gate.acquire(d)&&d==4,"gate/reacquire");
    }
    std::printf("RESULT %s %u\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
