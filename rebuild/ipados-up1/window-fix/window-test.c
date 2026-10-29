#include <assert.h>
#include "WindowRange.h"
int main(void) {
 DARWIN_WINDOW_RANGE r={0x100000000ULL,0xbd8c0000ULL}; unsigned long long out=0;
 assert(DarwinWindowRangeValid(r.Base,r.Bytes));
 assert(DarwinWindowCandidate(0x100000000ULL,0x1bd8c0000ULL,0xfffffff007004000ULL,0x80000000ULL,&r,&out));
 assert((out&0x1fffff)==(0xfffffff007004000ULL&0x1fffff));
 assert(!DarwinWindowCandidate(0x100000000ULL,0x170000000ULL,0xfffffff007004000ULL,0x80000000ULL,&r,&out));
 assert(!DarwinWindowCandidate(0x80000000ULL,0x100000000ULL,0xfffffff007004000ULL,0x80000000ULL,&r,&out));
 assert(!DarwinWindowRangeValid(~0ULL-0xfff,0x2000));
}
