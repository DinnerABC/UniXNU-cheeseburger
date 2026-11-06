// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "NativeMetal540Compute.h"
// Actual GLSL430 -> NIR -> A540 IR3 (not AIR/metallib/Metal device).
// Fixed frontend supports430, requested450 failed; no silent rewrite.
// Source reflection composed as sagit Compiler.cpp::compileEntries.
// Actual A5xx stgb cat6, NOT the historical A6xx stib.b probe.
// Exact localID source; one group only, groupID absent (252).
namespace Native540ProbeFixture {
inline constexpr uint32_t code[]={0x27fc1060u,0x42100003u,0x20020000u,0x46d00004u,0x20041060u,0x42934001u,0x00000000u,0x00000100u,0x00040003u,0x42934000u,0x00000000u,0x00000200u,0x00000001u,0x43800001u,0x00000000u,0x00000200u,0x20000001u,0x42a500f8u,0x00000000u,0x00000500u,0x00000005u,0x00900000u,0xdeadbeefu,0x204cc006u,0x00000000u,0x204cc005u,0x00000000u,0x00000500u,0x0000060du,0xc7260004u,0x00000000u,0x0b000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u};
inline constexpr uint32_t immediates[]={0x00000000u};
inline constexpr NativeMetal540::Program program={{0x00000040u,0x00000001u,0x0000001cu,0x00000002u,0x00000001u,0x00000001u,0x00000000u,0x00000040u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x000000fcu,0x00000001u,0x00000001u,0x04000008u,0x00000000u,0x00000000u,{0x00000004u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u}},code,immediates};
inline constexpr char codeSHA256[]="9eefeefa2b94619209fad900a6a21f8bd21ae91d30be0c70aef76465f17e8b05";
inline constexpr char sourceSHA256[]="2376385e1869abae12286ef6a959af0ae9987852dd27500da2fa222e82389334";
inline constexpr char producer[]="qgpu_compile_glsl; GLSL430-NIR-A540-IR3; exact gl_LocalInvocationID.x; NOT Metalcompiler";
inline constexpr uint32_t outputWords=64, expected=0xdeadbeefu, sentinel=0x13579bdfu;
static_assert(sizeof(code)/sizeof(code[0])==program.info.codeWords,"exact compiler code");
}
