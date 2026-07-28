// ============================================================================================================================================
//                                                          VISIBILITYRASTER.VERT
// ============================================================================================================================================
// 🧩 Vertex stage of the hardware visibility raster. Draws one RenderVertex (stride-32: position @0, normal @12, texcoord @24 — normal/texcoord
//    unused here, the visibility buffer stores only identity) of an instanced Suzanne scene. Each instance's column-major world matrix + its
//    partition identity live in a storage buffer indexed by gl_InstanceIndex; the world position transforms through the push-constant
//    ViewProjection into clip space. The partition ordinal is passed flat to the fragment stage, which packs it with gl_PrimitiveID.
#version 450

// One placed head: column-major model matrix, a normal basis (unused at this phase), a tint (unused here), the partition identity, and std140 tail
// pad — laid out to match SuzanneSceneInstance so the CPU list uploads straight into this buffer.
struct SceneInstance
{
    mat4 Model;          // column-major world transform
    vec4 NormalBasis[3]; // rotation-only basis (3x vec3 padded to vec4) — unused at this phase
    vec4 Tint;           // linear RGB (+pad) — unused here
    uint PartitionId;    // instance identity
    uint Pad0;
    uint Pad1;
    uint Pad2;
};

layout(std140, set = 0, binding = 0) readonly buffer InstanceBlock
{
    SceneInstance Instances[];
};

layout(push_constant) uniform RasterConstants
{
    mat4 ViewProjection;   // world -> clip (orbit camera)
} Constants;

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InTexCoord;

layout(location = 0) flat out uint FragPartitionId;

void main()
{
    SceneInstance Instance = Instances[gl_InstanceIndex];
    vec4 WorldPosition = Instance.Model * vec4(InPosition, 1.0);
    FragPartitionId = Instance.PartitionId;
    gl_Position = Constants.ViewProjection * WorldPosition;
}
