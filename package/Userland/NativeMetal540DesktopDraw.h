// SPDX-License-Identifier: MIT
#pragma once
#include "NativeMetal540DesktopSession.h"
#include <GPUJITProtocol.h>
#include <GPUShaderProtocol.h>
#include <vector>

// Matching sagit shader vocabulary is an internal typed encoder interface,
// never an IOKit selector/ABI replacement. The caller owns immutable images.
namespace NativeMetal540Desktop {
struct Binding { uint64_t resource=0;uint32_t offset=0,bytes=0; };
struct Draw {
    GPU::Shader::Draw state{};
    Binding buffers[QGPU_GRAPHICS_BUFFER_COUNT]{};
    uint64_t target=0;
    uint64_t colors[QGPU_COLOR_TARGET_COUNT-1]{};
    uint64_t vertexTextures[QGPU_STAGE_TEXTURE_COUNT]{};
    uint64_t fragmentTextures[QGPU_STAGE_TEXTURE_COUNT]{};
};
int encodeClear(NativeMetal540DesktopSession &session,uint64_t ib,uint64_t scratch,
    uint64_t target,uint32_t width,uint32_t height,uint32_t pitch,uint32_t color,uint32_t &words);
int encodeDraw(NativeMetal540DesktopSession &session,uint64_t ib,uint64_t scratch,
    const QGPUProgramImage &vertex,const QGPUProgramImage &fragment,const Draw &draw,
    uint32_t &words);
}
