// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NativeMetal540Compute.h"
// Runtime admission: four groups of64, identical immutable global-ID ISA.
// Actual GLSL430 -> NIR -> A540 IR3 (not AIR/metallib/Metal device).
// Fixed frontend supports430, requested450 failed; no silent rewrite.
// Source reflection composed as sagit Compiler.cpp::compileEntries.
// Actual A5xx stgb cat6, NOT the historical A6xx stib.b probe.
namespace Native540ProbeFixture {
inline constexpr uint32_t code[]={0x200600c0u,0x46d00004u,0x27fc1060u,0x42100003u,0x20041060u,0x42934000u,0x00000000u,0x00000000u,0x00000004u,0x42100004u,0x00000000u,0x00000200u,0x20020004u,0x46d00005u,0x00000000u,0x00000200u,0x00050003u,0x42934001u,0x00000000u,0x00000200u,0x00010000u,0x43800000u,0x00000000u,0x00000200u,0x20000000u,0x42a500f8u,0x00000000u,0x00000500u,0x00000005u,0x00900000u,0xdeadbeefu,0x204cc007u,0x00000000u,0x204cc006u,0x00000000u,0x00000500u,0x0400060fu,0xc7260005u,0x00000000u,0x0b000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u};
inline constexpr uint32_t immediates[]={0x00000000u};
inline constexpr NativeMetal540::Program program={{0x00000040u,0x00000002u,0x0000001cu,0x00000002u,0x00000001u,0x00000001u,0x00000000u,0x00000040u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x000000c0u,0x00000001u,0x00000001u,0x04000008u,0x00000000u,0x00000000u,{0x00000004u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u}},code,immediates};
inline constexpr char codeSHA256[]="e836dfd6496b140abbaf969400d3dfd63f1e5f4defc5436e66cc5391dd8863f2";
inline constexpr char sourceSHA256[]="0f1c32ef4aaf2f518cb33889657de4dd8b80294c2062c255ca35b86ae3931b3c";
inline constexpr char producer[]="qgpu_compile_glsl; GLSL430-NIR-A540-IR3; NOT Metalcompiler";
inline constexpr uint32_t outputWords=256, expected=0xdeadbeefu, sentinel=0x13579bdfu;
static_assert(sizeof(code)/sizeof(code[0])==program.info.codeWords,"exact compiler code");
}
