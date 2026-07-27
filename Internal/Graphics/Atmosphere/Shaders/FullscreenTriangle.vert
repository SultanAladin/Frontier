#version 450 core

// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                   FULLSCREEN TRIANGLE 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// The standard vertex-buffer-free fullscreen triangle: three vertices cover the whole clip rect, and the interpolated Uv (0..1) drops out of the
// clip position. Shared by the sky-view LUT bake and the per-frame sky dome (both are fullscreen passes). Draw with vkCmdDraw(cmd, 3, 1, 0, 0).

layout(location = 0) out vec2 OutUv;

void main()
{
    vec2 Uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    OutUv = Uv;
    gl_Position = vec4(Uv * 2.0 - 1.0, 0.0, 1.0);
}
