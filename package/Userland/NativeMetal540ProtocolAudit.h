// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#import <Foundation/Foundation.h>
struct Native540ProtocolAudit {unsigned methods=0,missing=0,mismatched=0;char first[64]={};};
// Read-only method/signature audit. No adoption or fake conformsToProtocol.
bool NativeMetal540ProtocolTypesMatch(const char *expected,const char *actual);
Native540ProtocolAudit NativeMetal540AuditDeviceProtocols(id device);
