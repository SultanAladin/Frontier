// ============================================================================================================================================
//                                                       PARAMETRICSKETCHMATCAP.VERT
// ============================================================================================================================================
// 🧩 Vertex stage of the parametric-sketch chrome-matcap surface inscription. Transforms one RenderVertex (stride-32: position @0, normal @12,
//    texcoord @24 — the texcoord is unused here) into clip space with the per-frame camera block bound at set 0, and hands the fragment stage the
//    camera-space normal so the matcap disc lookup can key off it. ViewProjection = projection · view (world -> clip); ViewMatrix = world ->
//    camera (its 3x3 rotates the normal into view space). Column-major, no transpose — laid out to match ParametricSketchSurfaceCameraBlock.
#version 450

layout(set = 0, binding = 0) uniform CameraBlock
{
    mat4 ViewProjection;   // world -> clip (orbit camera)
    mat4 ViewMatrix;       // world -> camera (matcap normal-transform source)
} Camera;

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InTexCoord;

layout(location = 0) out vec3 FragViewNormal;

void main()
{
    FragViewNormal = mat3(Camera.ViewMatrix) * InNormal;
    gl_Position    = Camera.ViewProjection * vec4(InPosition, 1.0);
}
