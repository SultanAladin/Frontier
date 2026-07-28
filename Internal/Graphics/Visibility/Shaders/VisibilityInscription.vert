#version 450

// 🧩 Fullscreen-triangle vertex stage for the visibility inscription. No vertex buffer: the three clip-space corners of a
//    single oversized triangle are synthesized from gl_VertexIndex (the standard Bikker/Karis trick), covering the whole
//    viewport so the fragment stage runs once per pixel. The [0,2] texture coordinate is handed down; the fragment stage
//    scales it to sample the visibility buffer at pixel centres.

layout(location = 0) out vec2 FragTexCoord;

void main()
{
    // (0,0) (2,0) (0,2) in UV → clip corners (-1,-1) (3,-1) (-1,3): one triangle that overdraws the screen.
    FragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position  = vec4(FragTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
