// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#import <Foundation/Foundation.h>
@class Native540Device;
// Explicit owned _MTLDevice initialization only. Does not call MTLAddDevice,
// register a system/default device, or claim full MTLDevice/SPI conformance.
id NativeMetal540CreateInitializedDevice(Native540Device *backend,uint32_t service,NSError **error);
