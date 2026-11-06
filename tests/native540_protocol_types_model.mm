#import "NativeMetal540ProtocolAudit.h"
#include <cassert>
#include <cstdio>
int main(){assert(NativeMetal540ProtocolTypesMatch("Q","Q"));assert(!NativeMetal540ProtocolTypesMatch("q","Q"));
assert(NativeMetal540ProtocolTypesMatch("^{MTLCompilerConnectionManager=}","^{MTLCompilerConnectionManager=^^?Bi}"));
assert(!NativeMetal540ProtocolTypesMatch("^{Other=}","^{MTLCompilerConnectionManager=^^?Bi}"));
assert(!NativeMetal540ProtocolTypesMatch("^^{MTLCompilerConnectionManager=}","^{MTLCompilerConnectionManager=^^?Bi}"));
assert(!NativeMetal540ProtocolTypesMatch("r^{MTLCompilerConnectionManager=}","^{MTLCompilerConnectionManager=^^?Bi}"));
assert(!NativeMetal540ProtocolTypesMatch("{Size=QQ}","{Size=Q}"));
assert(!NativeMetal540ProtocolTypesMatch(nullptr,"Q"));puts("PASS exact scalar/pointer/opaque struct type policy");}
