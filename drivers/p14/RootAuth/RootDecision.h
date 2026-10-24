// P14 owner-root identity gate; no Apple return code is rewritten.
#pragma once
#include <stdint.h>
static inline int p14RootDecision(int readonly, uint32_t mounted, uint32_t selected,
                                 uint32_t nodev, unsigned matches, int verified) {
 return readonly && mounted != nodev && mounted == selected && matches == 1 && verified == 1;
}
