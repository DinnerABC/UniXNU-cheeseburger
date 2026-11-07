// SPDX-License-Identifier: MIT
#pragma once
#import <Metal/Metal.h>
#include "AIRNIR.h"
#include "FunctionConstants.h"
#include <vector>
// Original Metal owns the value objects. Only a qualified copy is read; no
// private ivar projection and no mutation of caller function constants.
bool CBMetalSnapshotConstants(MTLFunctionConstantValues *values,std::vector<GPU::Compiler::FunctionConstantValue> &out,NSError **error);
