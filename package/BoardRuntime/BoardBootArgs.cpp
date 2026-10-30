// SPDX-License-Identifier: GPL-2.0-only
// Board boot arguments that are fixed for this device. Keeping them out of the
// payload boot string shortens the argument list (it is delivered through the
// device tree and stops being reliable past roughly 700 bytes) and makes the
// board behaviour independent of the launcher. A boot argument still wins, so a
// single value can be overridden without rebuilding; docs/P21_BAKED_BOOT_ARGS.md
// lists every entry with its reason.
#include <pexpert/pexpert.h>
#include "BoardBootArgs.h"
namespace {
struct CbBoardArg { const char *name; unsigned value; };
static const CbBoardArg kDefaults[] = {
 // Touch, HID and buttons.
 {"cheeseburger-touch",1},
 {"cheeseburger-buttons",1},
 {"cheeseburger-home-chord",1},
 {"sheng-touch-hid",1},
 {"offline-hid-input",1},
 {"offline-hid-home",1},
 // Security / keystore compatibility layer used by the offline boot.
 {"sheng-aks",1},
 {"sheng-aks-sls",3},
 {"offline-key-events",1},
 // Logging and observation.
 {"sheng-log",1},
 {"p20-observe",1},
 {"p21-iokit-trace",1},
 // Owner boot policy.
 {"cheeseburger-upstream-userland",1},
 {"cheeseburger-disable-watchdogd",1},
};
static bool same(const char *a,const char *b){
 if(!a||!b)return false;
 while(*a&&*b){if(*a++!=*b++)return false;}
 return *a==*b;
}
}
extern "C" int cb_board_arg(const char *name, void *value, unsigned size) {
 if (PE_parse_boot_argn(name, value, size)) return 1;
 if (!name || !value || size < sizeof(unsigned)) return 0;
 for (unsigned i = 0; i < sizeof kDefaults / sizeof kDefaults[0]; ++i) {
  if (!same(kDefaults[i].name, name)) continue;
  *(unsigned *)value = kDefaults[i].value;
  return 1;
 }
 return 0;
}
