// SPDX-License-Identifier: BSD-2-Clause-Patent
// Select the original IOMobileFramebufferLegacy ABI by explicit build config.
// The default is the 21H461 (iPadOS 17.7.11) layout every existing build uses;
// only a build that asks for 20H392 gets the iOS 16.7.16 layout. Unknown
// versions are a build error rather than a silent fallback to old offsets.
#pragma once
#if defined(UNIXNU_BOARD_ABI_20H392) && UNIXNU_BOARD_ABI_20H392
#include "OriginalLegacyFramebufferABI_20H392.h"
#elif defined(UNIXNU_BOARD_ABI_21H461) && UNIXNU_BOARD_ABI_21H461
#include "OriginalLegacyFramebufferABI_21H461.h"
#elif defined(UNIXNU_BOARD_ABI) && UNIXNU_BOARD_ABI
#error "UNIXNU_BOARD_ABI names an OS version with no original display ABI"
#else
// Unset keeps the historical default for every pre-existing build.
#include "OriginalLegacyFramebufferABI_21H461.h"
#endif
