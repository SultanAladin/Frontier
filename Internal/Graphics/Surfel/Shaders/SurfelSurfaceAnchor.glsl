/*==============================================================================================================================================
                                                         SURFELSURFACEANCHOR.GLSL
==============================================================================================================================================*/
// 🧩 A surfel's tie to the surface it sits on, and the fetch that turns that tie back into a position and a normal. This is the Frontier substitute for
//    Falcor's `gScene.getVertexData(TriangleHit)` — plan §5's one 🔴 "must build" item, and the thing that keeps a surfel GLUED to a body instead of
//    hanging in the air where the body used to be. Upstream stores a packed TriangleHit per surfel and re-derives position and normal EVERY frame from
//    it; this file is both halves of that, over the streams the software BVH already walks.
//
// 🔴 THIS MODULE DECLARES NO BINDINGS AND MUST BE INCLUDED AFTER TwoLevelTrace.glsl's INCLUDER CONTRACT IS IN SCOPE. It reads exactly the names that
//    contract already requires — Instances[] (Model @0 and NormalBasis[3] @64), Slices[], MeshIndices[], PositionForVertex() — plus ONE name the trace
//    itself does not need: NormalForVertex(). A consumer that only traces rays never needs vertex normals, so adding it to TwoLevelTrace.glsl's contract
//    would tax every consumer for this one; declaring it here keeps the cost where it is paid. ⚠️ The includer supplies it, reading the same vertex
//    stream PositionForVertex reads.
//
// 🔴 THE INTERPOLATED POSITION IS TRANSFORMED BY Model, THE NORMAL BY NormalBasis — NOT BOTH BY Model. NormalBasis is the inverse-transpose rows the
//    raster already uses; running a normal through Model instead is correct only for a rigid transform and shears the normal off the surface under any
//    non-uniform scale. The failure is a surfel whose disc faces slightly wrong, which reads as soft light leaking around an edge rather than as an
//    error. SurfaceShade.frag and the retired integrate both spell the same pair.
//
// 📝 The normal comes back GEOMETRIC-ish: interpolated from the three vertex normals when they are present, falling back to the triangle's face normal
//    when they are degenerate. There is no normal mapping — Frontier's tracer has no material system to sample one from (plan §5) — and for a diffuse
//    irradiance cache that is acceptable. Noted rather than hidden.

#ifndef FRONTIER_SURFEL_SURFACE_ANCHOR_GLSL
#define FRONTIER_SURFEL_SURFACE_ANCHOR_GLSL

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE ANCHOR WORD
//------------------------------------------------------------------------------------------------------------------------

// 🔴 THE PACK FORMAT OF HitLocatorStorage.Anchors[], AND IT IS FOUR FULL WORDS ON PURPOSE. Upstream squeezes a TriangleHit into a uint4 by packing the
//    two barycentrics as 16-bit fixed point; that saves nothing here (the buffer is already uvec4) and costs precision exactly where it hurts — a
//    barycentric quantized to 1/65535 moves the reconstructed position by a fraction of a triangle, which for a large floor triangle is millimetres of
//    per-frame jitter in a surfel that is supposed to be still. So each field gets its own word and the barycentrics keep full float precision.
//       .x = instance index          .y = primitive index (mesh-local triangle)
//       .z = first barycentric bits  .w = second barycentric bits   (floatBitsToUint of the (u, v) TraceHit reports)
// ⚠️ .x == SurfelAnchorAbsent marks a surfel with NO anchor — one seeded directly by a probe or a debug spawn rather than by a ray hit. Such a surfel
//    keeps its stored position and normal forever, which is right: there is no surface to re-derive it from.
const uint SurfelAnchorAbsent = 0xFFFFFFFFu;

uvec4 ComposeSurfaceAnchor(uint InstanceIndex, uint PrimitiveIndex, vec2 Barycentric)
{
    return uvec4(InstanceIndex, PrimitiveIndex, floatBitsToUint(Barycentric.x), floatBitsToUint(Barycentric.y));
}

uvec4 EmptySurfaceAnchor()
{
    return uvec4(SurfelAnchorAbsent, SurfelAnchorAbsent, 0u, 0u);
}

bool SurfaceAnchorPresent(uvec4 Anchor)
{
    return Anchor.x != SurfelAnchorAbsent;
}

vec2 FetchSurfaceAnchorBarycentric(uvec4 Anchor)
{
    return vec2(uintBitsToFloat(Anchor.z), uintBitsToFloat(Anchor.w));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE VERTEX FETCH
//------------------------------------------------------------------------------------------------------------------------

// The three corner ordinals of a mesh-local triangle, resolved through the slice table into the shared streams. Returns false when the anchor names an
// instance or a mesh outside the bound tables — the same drop test every acceleration pass applies, repeated here because this is the pass that actually
// dereferences it.
bool ResolveTriangleCorners(uint InstanceIndex, uint PrimitiveIndex, out uvec3 Corners, out uint MeshOrdinal)
{
    Corners     = uvec3(0u);
    MeshOrdinal = 0u;

    if (InstanceIndex >= TraceInstanceCount)
        return false;

    MeshOrdinal = Instances[InstanceIndex].MeshOrdinal;
    if (MeshOrdinal >= TraceSliceCount)
        return false;

    const uint IndexBase = Slices[MeshOrdinal].IndexOffset + PrimitiveIndex * 3u;
    const uint VertexBase = Slices[MeshOrdinal].VertexOffset;

    Corners = uvec3(VertexBase + MeshIndices[IndexBase + 0u],
                    VertexBase + MeshIndices[IndexBase + 1u],
                    VertexBase + MeshIndices[IndexBase + 2u]);
    return true;
}

// 🧩 THE FETCH. Interpolate the struck triangle at its barycentric and hand back a WORLD position and a WORLD unit normal. Returns false, leaving both
//    arguments untouched, when the anchor is absent or out of range — so a caller can keep the surfel's stored values rather than moving it to the origin.
//
// 📝 The barycentric convention matches IntersectRayTriangle's (u, v): the point is A + u(B-A) + v(C-A), so the three weights are (1-u-v, u, v) in
//    CORNER order. ⚠️ Swapping u and v gives a point that is still inside the triangle and still moves with the body — it is simply the wrong point, and
//    it looks like a surfel sitting a little off where it should.
bool ResolveAnchoredSurfacePoint(uvec4 Anchor, out vec3 WorldPosition, out vec3 WorldNormal)
{
    if (!SurfaceAnchorPresent(Anchor))
        return false;

    uvec3 Corners;
    uint  MeshOrdinal;
    if (!ResolveTriangleCorners(Anchor.x, Anchor.y, Corners, MeshOrdinal))
        return false;

    const vec2  Barycentric = FetchSurfaceAnchorBarycentric(Anchor);
    const float WeightA = 1.0 - Barycentric.x - Barycentric.y;
    const float WeightB = Barycentric.x;
    const float WeightC = Barycentric.y;

    const vec3 LocalA = PositionForVertex(Corners.x);
    const vec3 LocalB = PositionForVertex(Corners.y);
    const vec3 LocalC = PositionForVertex(Corners.z);

    const vec3 LocalPosition = LocalA * WeightA + LocalB * WeightB + LocalC * WeightC;

    // 🔴 Position through Model, normal through NormalBasis. See the file header.
    WorldPosition = (Instances[Anchor.x].Model * vec4(LocalPosition, 1.0)).xyz;

    vec3 LocalNormal = NormalForVertex(Corners.x) * WeightA + NormalForVertex(Corners.y) * WeightB + NormalForVertex(Corners.z) * WeightC;

    // ⚠️ A mesh with unauthored (zero) vertex normals interpolates to zero, and normalize(0) is a NaN that would ride into the surfel's stored normal and
    //    from there into every dot product the gather takes. The face normal is the honest fallback and costs three subtractions on a path that already
    //    read all three corners.
    if (dot(LocalNormal, LocalNormal) < 1e-16)
        LocalNormal = cross(LocalB - LocalA, LocalC - LocalA);

    const mat3 NormalBasis = mat3(Instances[Anchor.x].NormalBasis[0].xyz,
                                  Instances[Anchor.x].NormalBasis[1].xyz,
                                  Instances[Anchor.x].NormalBasis[2].xyz);

    const vec3 RotatedNormal = NormalBasis * LocalNormal;
    if (dot(RotatedNormal, RotatedNormal) < 1e-16)
        return false;   // a fully degenerate triangle on a degenerate instance; keep the stored normal rather than emit a NaN

    WorldNormal = normalize(RotatedNormal);
    return true;
}

// The FACE normal alone, for a hit the trace has just reported and has no stored normal for. Same transform pair, no vertex-normal interpolation — a
// bounce ray's shading only needs the facet it struck, and the retired integrate's SurfelReconstructHitNormal did exactly this.
// ⚠️ Returns vec3(0.0) on a dropped instance or a degenerate triangle. Callers test it; a zero normal makes every NdotL zero, so an untested one reads
//    as a black patch rather than as a NaN.
vec3 ResolveHitFaceNormal(uint InstanceIndex, uint PrimitiveIndex)
{
    uvec3 Corners;
    uint  MeshOrdinal;
    if (!ResolveTriangleCorners(InstanceIndex, PrimitiveIndex, Corners, MeshOrdinal))
        return vec3(0.0);

    const vec3 LocalA = PositionForVertex(Corners.x);
    const vec3 LocalB = PositionForVertex(Corners.y);
    const vec3 LocalC = PositionForVertex(Corners.z);

    const mat3 NormalBasis = mat3(Instances[InstanceIndex].NormalBasis[0].xyz,
                                  Instances[InstanceIndex].NormalBasis[1].xyz,
                                  Instances[InstanceIndex].NormalBasis[2].xyz);

    const vec3 RotatedNormal = NormalBasis * cross(LocalB - LocalA, LocalC - LocalA);
    if (dot(RotatedNormal, RotatedNormal) < 1e-16)
        return vec3(0.0);
    return normalize(RotatedNormal);
}

#endif
