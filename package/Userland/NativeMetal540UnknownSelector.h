// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>
// Bounded unknown-selector recorder shared by the desktop objects and the
// dynamic 21H461 adapter. Unknown selectors are never thrown and never fake
// success; they are recorded so the next round can implement what the system
// actually calls. Pure C++ taking the selector name, so pure-C++ link targets
// (session/draw models) need no objc runtime.
void CBMetalRecordUnknownSelectorName(const char *name);
using CBMetalReportSend = void (*)(const char *text);
void CBMetalSetReportSend(CBMetalReportSend send, void *connection);
unsigned CBMetalUnknownSelectorCount(void);
std::vector<std::string> CBMetalUnknownSelectorList(void);
