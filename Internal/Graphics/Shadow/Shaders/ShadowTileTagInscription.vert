/*==============================================================================================================================================
                                                       SHADOWTILETAGINSCRIPTION.VERT
==============================================================================================================================================*/
// 🧩 S2 vertex stage — caster-driven staleness tagging. Rasterizes each moving caster's light-space footprint into the tilemap so the fragment
//    stage can raise `Update` on every tile that caster's depth touches. Where S1 answers "which pages must exist", this answers "which of them
//    hold WRONG depth"; a page rasterizes only on the conjunction of the two.
//
// 🔴 THIS RASTERIZES A SPHERE PROXY, NOT AN ORIENTED BOX, AND THAT IS A DIVERGENCE FROM PLAN-SunShadowClipmap.md's "instanced OBB" WORDING.
//    The only bounds producer that exists is InstanceCull's PartitionCullRecord, which carries a world-space bounding SPHERE (SphereXYZ +
//    SphereRadius) and a normal cone — there is no OBB anywhere in the pipeline to instance. A sphere has no orientation, so an OBB path would need
//    an extra producer that has not been built. The substitution is conservative in the right direction: a sphere's light-space silhouette is a
//    circle of exactly SphereRadius regardless of sun angle, so the quad below never UNDER-covers the caster. It over-covers a thin or elongated
//    caster, which costs redundant redraws, never a missed update (which would be a stale shadow).
//
// 🔴 THE Z PANCAKE IS LOAD-BEARING. OutPosition.z is crushed toward 0 because this pass wants pure 2D COVERAGE: which tiles does the footprint
//    overlap. A caster's real light-space depth is irrelevant to that question, and letting it through means anything outside [0,1] gets clipped —
//    a caster behind the light's near plane, or beyond its far plane, would silently fail to tag tiles it genuinely covers. Depth test and depth
//    write must both be OFF in the pipeline for the same reason.
//
// ⚠️ Two sub-draws per image, PREVIOUS then CURRENT bounds: a caster that moved makes its old tiles stale (nothing occludes there any more) AND its
//    new ones (something does now). Tagging only the current bounds leaves a shadow frozen at the old position.
//    🔴 NO DATA SOURCE FOR THE PREVIOUS BOUNDS EXISTS YET — SuzanneSceneInstance carries no previous transform and the cull upload runs only in the
//    scene-load branch. Until that lands, the recorder can only issue the CURRENT sub-draw, so a caster that MOVES will not invalidate the page it
//    left behind. Tracked in EngineDocs/Backlog.md; this shader is already correct for both sub-draws and needs no change when the data arrives.

#version 450

#extension GL_GOOGLE_include_directive : require
#include "ShadowTileStore.glsl"

// 📝 The per-partition cull records, the same SSBO the P3 cull compute reads — 32 bytes each, two vec4s. Read as vec4 pairs rather than a mirrored
//    struct so the std430 layout cannot drift from PartitionCullRecord's field order.
// ⚠️ Bound at binding 4 to stay clear of the marking chain's shared bindings (0 = tile table, 3 = level origins).
layout(set = 0, binding = 4, std430) readonly restrict buffer CasterBoundsBuffer
{
    vec4 BoundsWords[];   // [2 * i + 0] = (SphereX, SphereY, SphereZ, SphereRadius); [2 * i + 1] = normal cone, unused here
} CasterBounds;

layout(push_constant) uniform InscriptionConstants
{
    vec4  LightRightAxis;     // [-]  - light-space +X in world (w unused)
    vec4  LightUpAxis;        // [-]  - light-space +Y in world (w unused)
    vec2  WindowCentreTile;   // [-]  - light-tile coordinate at the centre of this level's window, for the NDC mapping below
    float BaseTileMetres;     // [m]  - LOD 0 tile edge; doubles per level
    uint  Level;              // [-]  - the LOD this draw targets; one draw per level
    float EdgeExpandTexels;   // [-]  - conservative-raster expansion, in tilemap texels (~1.0)
} Constants;

layout(location = 0) flat out uint OutLevel;

void main()
{
    // One quad per caster: four corners from gl_VertexIndex, drawn as a triangle strip.
    const vec4  Sphere       = CasterBounds.BoundsWords[2 * gl_InstanceIndex + 0];
    const float TileMetres   = Constants.BaseTileMetres * float(1u << Constants.Level);

    // Project the sphere CENTRE onto the two lateral light axes. The forward axis is depth, which the atlas stores per texel rather than addressing
    // tiles by, so it plays no part in a coverage question.
    const float CentreX = dot(Sphere.xyz, Constants.LightRightAxis.xyz);
    const float CentreY = dot(Sphere.xyz, Constants.LightUpAxis.xyz);

    // 🔴 The silhouette radius is the sphere radius UNCHANGED — no cosine term, no axis-dependent scaling. A sphere projects to a circle of the same
    //    radius under every orthographic direction, which is the entire reason it is a safe proxy for an unknown orientation.
    const float RadiusMetres = Sphere.w;

    // The quad corner, in units of the sphere's radius: (-1,-1), (1,-1), (-1,1), (1,1) for a strip.
    const vec2 CornerSign = vec2(float((gl_VertexIndex & 1) << 1) - 1.0,
                                 float((gl_VertexIndex & 2)) - 1.0);

    // Light-space position of this corner, in metres, then in tiles of this level.
    const vec2 CornerMetres = vec2(CentreX, CentreY) + CornerSign * RadiusMetres;
    const vec2 CornerTile   = CornerMetres / TileMetres;

    // Map light tiles to NDC across the 32-tile window centred on WindowCentreTile. The tilemap render target is ShadowTilemapResolution texels
    // square — one texel per tile — so one tile spans 2.0 / 32 in NDC.
    const float NdcPerTile = 2.0 / float(ShadowTilemapResolution);
    vec2        Ndc        = (CornerTile - Constants.WindowCentreTile) * NdcPerTile;

    // 🔴 CONSERVATIVE RASTERIZATION, EMULATED. VK_EXT_conservative_rasterization is not core and is absent on much of the target hardware, so the quad
    //    is expanded outward by ~one tilemap texel instead. Without this, a footprint that grazes a tile by less than half a texel covers no sample
    //    point and the tile is never tagged — the caster moves, its shadow's edge tile keeps last image's depth, and the shadow edge lags the object.
    //    Expanding OUTWARD only ever over-tags (a redundant redraw), which is why the sign follows the corner rather than being a uniform inflation.
    Ndc += CornerSign * (Constants.EdgeExpandTexels * NdcPerTile);

    OutLevel = Constants.Level;

    // 🔴 z = 0, not the caster's light-space depth — see the pancake note in the banner. w = 1 keeps the mapping affine, so no perspective divide
    //    reintroduces a depth dependence.
    gl_Position = vec4(Ndc, 0.0, 1.0);
}
