// SPDX-License-Identifier: BSD-2-Clause-Patent
// Slot guards for the ApplePMGR subclass, selected with the PMGR ABI above.
#pragma once
#if defined(UNIXNU_BOARD_ABI_20H392) && UNIXNU_BOARD_ABI_20H392
#include "GuardedPMGRSlots_20H392.inc"
#else
#include "GuardedPMGRSlots_21H461.inc"
#endif
