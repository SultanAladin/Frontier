// ============================================================================================================================================
//                                                          VISIBILITYRASTER.VERT
// ============================================================================================================================================
// 🧩 Vertex stage of the hardware visibility raster. Draws one RenderVertex (stride-32: position @0, normal @12, texcoord @24 — normal/texcoord
//    unused here, the visibility buffer stores only identity) of an instanced Suzanne scene. Each instance's column-major world matrix + its
//    partition identity live in a storage buffer indexed by gl_InstanceIndex; the world position transforms through the push-constant
//    ViewProjection into clip space. The partition ordinal is passed flat to the fragment stage, which packs it with gl_PrimitiveID.
#version 450

// One placed head: column-major model matrix, a normal basis (unused at this phase), a tint (unused here), the partition identity, the material the
// shade pass resolves through, and std140 tail pad — laid out to match SuzanneSceneInstance so the CPU list uploads straight into this buffer.
// MaterialId is not read HERE (this stage only writes identity); it is read by SurfaceShade.comp off the same buffer, so it must still occupy the
// right slot or every member after it shifts.
// 🔴 MIRRORS SuzanneSceneInstance (Graphics/Scene/SuzanneScene.h) FIELD FOR FIELD — 208 bytes. Three other shaders carry the same hand-written copy
//    (SurfaceShade.frag, SoftwareRasterization.comp, ComponentOverlay.frag). A copy that falls behind the C++ struct still compiles and still
//    validates; it just strides by the wrong size, so instance N reads the tail of instance N-1. The host static_assert is what catches it.
struct SceneInstance
{
    mat4 Model;          // column-major world transform
    vec4 NormalBasis[3]; // rotation-only basis (3x vec3 padded to vec4) — unused at this phase
    vec4 Tint;           // linear RGB (+pad) — unused here
    uint PartitionId;    // instance identity
    uint MaterialId;     // index into the SurfacePresetTable — unused here, read by the shade pass
    uint MeshOrdinal;    // bottom-level tree slice — ray tracing only, unused here
    uint Pad0;
    mat4 InverseModel;   // world -> local, for the ray trace — unused here
};

layout(std140, set = 0, binding = 0) readonly buffer InstanceBlock
{
    SceneInstance Instances[];
};

// set 0, binding 1 — the GPU cull's survivor list (one instance index per survivor). When culling is active the indirect draw issues
// gl_InstanceIndex 0 .. instanceCount, i.e. a survivor SLOT, so the real instance index is Survivors[gl_InstanceIndex]. When culling is off
// this buffer is bound (to keep one pipeline / set layout) but never read — gl_InstanceIndex indexes Instances directly.
layout(std430, set = 0, binding = 1) readonly buffer SurvivorBlock
{
    uint Survivors[];
};

layout(push_constant) uniform RasterConstants
{
    mat4 ViewProjection;   // world -> clip (orbit camera)
    uint CullActive;       // 0 = gl_InstanceIndex is the instance (plain draw); 1 = it is a survivor slot to remap through Survivors[]
    uint Pad0;
    uint Pad1;
    uint Pad2;
} Constants;

layout(location = 0) in vec3 InPosition;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InTexCoord;

layout(location = 0) flat out uint FragPartitionId;

void main()
{
    uint InstanceIndex = (Constants.CullActive != 0u) ? Survivors[gl_InstanceIndex] : uint(gl_InstanceIndex);
    SceneInstance Instance = Instances[InstanceIndex];
    vec4 WorldPosition = Instance.Model * vec4(InPosition, 1.0);
    FragPartitionId = Instance.PartitionId;
    gl_Position = Constants.ViewProjection * WorldPosition;
}
