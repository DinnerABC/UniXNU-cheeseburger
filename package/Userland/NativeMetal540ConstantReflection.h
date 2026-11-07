// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
// Bounded C bridge for the pinned AIR inspector. LLVM objects never cross it.
struct CBMetalConstantDeclaration {uint32_t index,type,required;char name[256];};
using CBMetalInspectConstants=int (*)(const void *,size_t,const char *,CBMetalConstantDeclaration *,unsigned *,char *,unsigned);
extern "C" __attribute__((visibility("default"))) int cbmetal_inspect_air_constants(
    const void *,size_t,const char *,CBMetalConstantDeclaration *,unsigned *,char *,unsigned);
