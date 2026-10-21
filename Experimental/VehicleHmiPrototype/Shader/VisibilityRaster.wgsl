//============================================================================================================================================
//                                                          VISIBILITYRASTER.WGSL
//============================================================================================================================================
// 🧩 Phase-3 visibility raster: writes cluster-triangle identity + barycentrics only (no material work).
//    Opaque draws write depth; transmissive draws depth-test against it without writing (glass is never an
//    occluder). A second entry depth-peels the glass back layer against the front layer's depth texture.

//! include "FrameDescriptor.wgsl"

@group(0) @binding(1) var<storage, read> Occupants : array<OccupantRecord>;

struct VisibilityVarying
{
    @builtin(position) Position          : vec4f,
    @location(0) @interpolate(flat) TriangleFirst : u32,   // global expanded-vertex index of the triangle's first corner
    @location(1) Barycentric             : vec3f,
    @location(2) @interpolate(flat) Occupant : u32,
}

@vertex
fn VisibilityVertex(@builtin(vertex_index) VertexIndex : u32,
                    @location(0) LocalPosition : vec3f, @location(1) Family : u32, @location(2) Normal : vec3f,
                    @location(3) OccupantOffset : u32, @location(4) OccupantIndex : u32) -> VisibilityVarying
{
    let Index = OccupantIndex + OccupantOffset;
    var Varying : VisibilityVarying;
    Varying.Position = Frame.ViewProjection * Occupants[Index].LocalToWorld * vec4f(LocalPosition, 1.0);
    Varying.TriangleFirst = VertexIndex - (VertexIndex % 3u);
    let Corner = VertexIndex % 3u;
    Varying.Barycentric = vec3f(select(0.0, 1.0, Corner == 0u), select(0.0, 1.0, Corner == 1u), select(0.0, 1.0, Corner == 2u));
    Varying.Occupant = Index;
    return Varying;
}

struct VisibilityTargets
{
    @location(0) OpaqueVis  : vec2u,
    @location(1) OpaqueBary : vec2f,
    @location(2) GlassVis   : vec2u,
    @location(3) GlassBary  : vec2f,
}

// 📝 One entry, two pipelines: the opaque pipeline enables depth write, the glass pipeline disables it, and
//    the host draws opaque commands first so glass depth-tests against opaque geometry. Class routing uses
//    the per-draw flag the host encodes in a uniform-free way: glass triangles carry the transmissive bit in
//    their family stream, resolved by the resolve program; here the host's glass pipeline renders only
//    transmissive records (draw lists are class-split on the CPU side).
@fragment
fn VisibilityFragment(Varying : VisibilityVarying) -> VisibilityTargets
{
    var Targets : VisibilityTargets;
    Targets.OpaqueVis = vec2u(Varying.TriangleFirst + 1u, Varying.Occupant);
    Targets.OpaqueBary = Varying.Barycentric.yz;
    Targets.GlassVis = vec2u(Varying.TriangleFirst + 1u, Varying.Occupant);
    Targets.GlassBary = Varying.Barycentric.yz;
    return Targets;
}

@group(1) @binding(0) var FrontGlassDepth : texture_depth_2d;

struct GlassBackTargets
{
    @location(0) GlassBackVis  : vec2u,
    @location(1) GlassBackBary : vec2f,
}

// 📝 Depth-peel pass: keep only glass fragments farther than the front layer at this pixel (reversed-Z:
//    farther = smaller depth); the pass's own depth buffer then keeps the nearest of those.
@fragment
fn VisibilityGlassBackFragment(Varying : VisibilityVarying) -> GlassBackTargets
{
    let Coord = vec2i(Varying.Position.xy);
    let FrontDepth = textureLoad(FrontGlassDepth, Coord, 0);
    if (Varying.Position.z >= FrontDepth - 1.0e-5) { discard; }
    var Targets : GlassBackTargets;
    Targets.GlassBackVis = vec2u(Varying.TriangleFirst + 1u, Varying.Occupant);
    Targets.GlassBackBary = Varying.Barycentric.yz;
    return Targets;
}
