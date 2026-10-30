/* SPDX-License-Identifier: GPL-2.0-only */
/* Board boot arguments with compiled-in defaults for this port. */
#ifndef CB_BOARD_BOOT_ARGS_H
#define CB_BOARD_BOOT_ARGS_H
#ifdef __cplusplus
extern "C" {
#endif
/* Boot argument wins; otherwise the compiled default for this board is used.
 * Returns 1 when a value was produced, 0 when the name is unknown. */
int cb_board_arg(const char *name, void *value, unsigned size);
#ifdef __cplusplus
}
#endif
#endif
