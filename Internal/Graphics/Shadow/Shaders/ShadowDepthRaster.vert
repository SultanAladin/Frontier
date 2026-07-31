/*==============================================================================================================================================
                                                          SHADOWDEPTHRASTER.VERT
==============================================================================================================================================*/
// 🧩 S7 vertex stage — the caster depth raster. Transforms each instanced caster vertex into the sun's ORTHOGRAPHIC light space for one clipmap
//    level, and hands the fragment stage the light-space position it needs to resolve both its atlas texel and the depth it writes there. One draw
//    per level; the fragment stage does the atomic-min resolve into the page atlas.
//
// 🔴 THIS PASS HAS NO DEPTH ATTACHMENT AND NO HARDWARE DEPTH TEST, and that is a consequence of the atlas format rather than a shortcut.
//    ShadowPageAtlas is R32_UINT with STORAGE | SAMPLED usage — it CANNOT be a depth attachment, so there is no hardware depth buffer to test
//    against. Occlusion is resolved by `imageAtomicMin` in the fragment stage instead: the nearest-to-the-sun caster wins because the smallest
//    encoded depth wins. Enabling a depth test here would be testing against a target that does not exist.
//
// 🔴 THE ATLAS IS NOT THE RENDER TARGET, AND THE VIEWPORT IS NOT THE ATLAS. A single draw covers tiles whose pages are scattered anywhere across
//    the 16x16 page grid, in whatever order the allocator handed them out — there is no affine map from a clip position to an atlas texel, so the
//    write address CANNOT come from gl_FragCoord against an attachment. The viewport is instead this level's 32-tile LIGHT-SPACE WINDOW, and the
//    fragment stage resolves its own atlas texel through the mapping SSBO per fragment. This is why OutLightPosition is passed down rather than
//    recomputed: the fragment stage needs the light-space point, not a screen coordinate.
//
// 🔴 DEPTH GOES DOWN THE INTERPOLATOR, NOT THROUGH gl_Position.z. gl_Position.z is consumed by clipping and (absent a depth attachment) nothing
//    else, and forcing the real light-space depth through it would clip every caster outside [0,1] — silently deleting exactly the casters that are
//    nearest the sun and therefore the ones that matter most. z is pancaked to 0 as in S2, and the true depth travels in OutLightPosition.z where
//    clipping cannot touch it.
//
// ⚠️ Lateral clipping is still WANTED here, unlike the S2 pancake. A caster outside this level's window genuinely has no tile at this level and must
//    not write; letting x/y clip is how that exclusion happens for free.

#version 450

// 🔴 DELIBERATELY INCLUDES NOTHING — see the matching note in ShadowDepthRaster.frag. ShadowTileStore.glsl declares the tile table at set 0 binding 0,
//    which is where this unit's fragment stage binds the atlas storage image, and the collision is emitted into the SPIR-V even when the block is
//    unused (verified with spirv-dis). This stage needs one constant, mirrored below.
// ⚠️ Mirrored from ShadowTileStore.glsl / ShadowTileStore.h; unchecked by any compiler.
#define ShadowTilemapResolution 32

// 📝 The mesh's vertex input, matching the stride-32 RenderVertex layout the visibility raster binds (position / normal / uv). Only the position is
//    read — a shadow caster's depth does not depend on its shading attributes — but the later locations must still be declared to match the
//    pipeline's vertex input description.
layout(location = 0) in vec3 InPosition;

// 📝 The per-instance transforms, the SAME SuzanneSceneInstance storage buffer the visibility raster reads, so a caster cannot be placed differently
//    for its shadow than for its shading. Read as vec4 columns rather than a mirrored struct so the std140 layout cannot drift from the C++ field
//    order.
//
// 🔴 THE STRIDE IS 9 vec4s AND IT MUST BE COUNTED FROM SuzanneSceneInstance IN FULL, INCLUDING THE MEMBERS THIS STAGE NEVER READS. The record is
//    Model[16] + NormalBasis[12] + Tint[4] + (PartitionId, MaterialId, Padding[2]) = 36 floats = 144 bytes = 9 vec4s. This said 8 until 2026-07-31,
//    counting only the members it consumes (4 model + 3 basis + 1 tint) and silently dropping the identity/material vec4 — so it strided 128 bytes
//    through a 144-byte array and every instance after the first read a matrix assembled from the WRONG RECORD's bytes.
//
// 🔴 THE FAILURE IS PROGRESSIVE, WHICH IS WHY IT LOOKED LIKE A PAGING BUG RATHER THAN A LAYOUT ONE. The drift is 16 bytes PER INSTANCE, so instance 0
//    is correct, instance 1 is off by one vec4, instance 8 reads instance 7's record entirely. On screen: exactly ONE caster casts a correct shadow,
//    its neighbours cast shadows STRETCHED ALONG ONE AXIS (NormalBasis rows land in model-matrix scale slots), and one lands at the WORLD ORIGIN
//    because Tint {1,1,1,1} and Padding {0,0} land in the translation column. Casters flung outside the window by garbage transforms then scribble
//    depth across unrelated tiles, which reads as the window tiling itself across the ground.
//
// ⚠️ NOTHING CATCHES THIS AT BUILD OR VALIDATION TIME. The buffer is an unsized vec4 array, so any stride is in-bounds and Vulkan validation is
//    silent; the visibility raster is immune because it declares the record as a std140 STRUCT and lets the compiler compute the stride. 🔴 IF
//    SuzanneSceneInstance GAINS OR LOSES A MEMBER, THIS CONSTANT MUST MOVE WITH IT — it is the one place the layout is transcribed by hand.
#define ShadowCasterInstanceStride 9u

// ⚠️ Bound at binding 2 to stay clear of the atlas image (0) and the mapping table (1).
layout(set = 0, binding = 2, std430) readonly restrict buffer ShadowCasterInstanceBuffer
{
    vec4 InstanceWords[];
} CasterInstances;

layout(push_constant) uniform ShadowDepthConstants
{
    vec4  LightRightAxis;      // [-] - light-space +X in world (w unused)
    vec4  LightUpAxis;         // [-] - light-space +Y in world (w unused)
    vec4  LightForwardAxis;    // [-] - light-space +Z in world, ALONG the light's travel (w unused)
    vec2  WindowCentreTile;    // [-] - light tile at the centre of this level's window
    ivec2 ToroidalOrigin;      // [-] - this level's origin, in ADD form (see ShadowTileStore.glsl)
    float BaseTileMetres;      // [m] - LOD 0 tile edge; doubles per level
    uint  Level;               // [-] - the LOD this draw targets
    float DepthRangeMetres;    // [m] - the light-space depth span the encoded uint spans
    float DepthOriginMetres;   // [m] - light-space depth mapped to encoded 0
    uint  PageResolution;      // [texel] - one page's square edge
    uint  AtlasPageEdge;       // [page]  - pages per atlas edge
    uint  ClearValue;          // [-] - the atomic-min identity; a caster may never encode to this
    uint  Padding;             // [-] - std430 push tail pad
} Constants;

// The light-space position in METRES: xy lateral (for the tile/texel resolve), z the depth the atomic-min compares.
layout(location = 0) out vec3 OutLightPosition;
layout(location = 1) flat out uint OutLevel;

void main()
{
    // Rebuild the model matrix from its four columns and place the vertex in the world.
    const uint InstanceBase = ShadowCasterInstanceStride * uint(gl_InstanceIndex);
    const vec4 ModelColumn0 = CasterInstances.InstanceWords[InstanceBase + 0u];
    const vec4 ModelColumn1 = CasterInstances.InstanceWords[InstanceBase + 1u];
    const vec4 ModelColumn2 = CasterInstances.InstanceWords[InstanceBase + 2u];
    const vec4 ModelColumn3 = CasterInstances.InstanceWords[InstanceBase + 3u];

    const vec4 WorldPosition = ModelColumn0 * InPosition.x
                             + ModelColumn1 * InPosition.y
                             + ModelColumn2 * InPosition.z
                             + ModelColumn3;

    // Project into the orthonormal light frame. 📝 An orthographic projection is exactly three dot products because the basis is orthonormal —
    //    no matrix, no perspective divide, and therefore no possibility of a w-related depth nonlinearity in the atlas.
    const float LateralX    = dot(WorldPosition.xyz, Constants.LightRightAxis.xyz);
    const float LateralY    = dot(WorldPosition.xyz, Constants.LightUpAxis.xyz);
    const float DepthMetres = dot(WorldPosition.xyz, Constants.LightForwardAxis.xyz);

    OutLightPosition = vec3(LateralX, LateralY, DepthMetres);
    OutLevel         = Constants.Level;

    // Map light-space metres onto this level's 32-tile window in NDC. One tile spans 2.0 / 32.
    const float TileMetres = Constants.BaseTileMetres * float(1u << Constants.Level);
    const vec2  LateralTile = vec2(LateralX, LateralY) / TileMetres;
    const vec2  Ndc         = (LateralTile - Constants.WindowCentreTile) * (2.0 / float(ShadowTilemapResolution));

    // 🔴 z = 0 and w = 1: the depth travels in OutLightPosition.z instead (see the banner). x/y are left to clip normally, which is what excludes a
    //    caster that lies outside this level's window.
    gl_Position = vec4(Ndc, 0.0, 1.0);
}
