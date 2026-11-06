// SPDX-License-Identifier: MIT
#pragma once
// Exact public inputs. No buffers, varyings, attributes, textures or depth.
namespace Native540RenderSource {
inline constexpr char vertex[] =
    "#version 430\n"
    "void main() {\n"
    "  vec2 p = gl_VertexID == 0 ? vec2(-0.75,-0.75) :\n"
    "           gl_VertexID == 1 ? vec2(0.75,-0.75) : vec2(0.0,0.75);\n"
    "  gl_Position = vec4(p,0.0,1.0);\n"
    "}\n";
inline constexpr char fragment[] =
    "#version 430\n"
    "layout(location=0) out vec4 color;\n"
    "void main() { color = vec4(1.0,0.0,0.0,1.0); }\n";
}
