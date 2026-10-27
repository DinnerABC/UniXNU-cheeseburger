// SPDX-License-Identifier: BSD-2-Clause-Patent
// Select the original ApplePMGR ABI by explicit build config.
// Unset keeps the 21H461 layout every existing iPadOS 17.7.11 build uses;
// only a build that asks for 20H392 gets the iOS 16.7.16 slot list/size.
#pragma once
#if defined(UNIXNU_BOARD_ABI_20H392) && UNIXNU_BOARD_ABI_20H392
#include "OriginalPMGRABI_20H392.h"
#elif defined(UNIXNU_BOARD_ABI_21H461) && UNIXNU_BOARD_ABI_21H461
#include "OriginalPMGRABI_21H461.h"
#elif defined(UNIXNU_BOARD_ABI) && UNIXNU_BOARD_ABI
#error "UNIXNU_BOARD_ABI names an OS version with no original PMGR ABI"
#else
#include "OriginalPMGRABI_21H461.h"
#endif
