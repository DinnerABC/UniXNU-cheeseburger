// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
// Read-only runtime signatures; no private initializer or GPU action is called.
unsigned NativeMetal540CaptureABI(char (*rows)[64],unsigned capacity);
