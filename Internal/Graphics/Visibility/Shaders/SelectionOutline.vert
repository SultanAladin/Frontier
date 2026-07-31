// ============================================================================================================================================
//                                                          SELECTIONOUTLINE.VERT
// ============================================================================================================================================
// 🧩 Fullscreen-triangle vertex stage for the selection outline. No vertex buffer: the three clip-space corners of one oversized triangle are
//    synthesized from gl_VertexIndex, covering the whole viewport so the fragment stage runs once per pixel. The [0,2] texture coordinate is
//    handed down and scaled against textureSize in the fragment stage. Mirrors VisibilityInscription.vert exactly — same trick, own module so the
//    outline target stays self-contained.
#version 450

layout(location = 0) out vec2 FragTexCoord;

void main()
{
    // (0,0) (2,0) (0,2) in UV → clip corners (-1,-1) (3,-1) (-1,3): one triangle that overdraws the screen.
    FragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position  = vec4(FragTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
