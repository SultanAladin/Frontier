/*==============================================================================================================================================
                                                    SURFELVISIBILITYRECONSTRUCT.GLSL
==============================================================================================================================================*/
// 🧩 The surfel spawn pass has no G-buffer to read. webgiya's FindMissing sampled a depth texture + a normal texture per pixel
//    (surfelFindMissingPass.ts:295-304) to recover world position + normal; Frontier is a VISIBILITY-buffer deferred renderer, so the
//    spawn pass must reconstruct the SAME way SurfaceShade.frag does — unpack the packed (partition, primitive) identity, fetch the
//    triangle's three world corners, intersect this pixel's view ray against it for the barycentric world position, and take the
//    triangle's own PLANE normal. This module is that reconstruction, factored out so the spawn shader and SurfaceShade.frag can never
//    drift on the identity math (the single largest translation decision in Phase 1, PLAN §"Two facts that shape the work" ①).
//
//    🔴 THIS IS A 1:1 MIRROR OF SurfaceShade.frag:320-416 — same identity pack (20 primitive bits), same floor rebase at
//       FloorPartitionBase, same FloorIndexBase remap of the draw-local ordinal into the merged index buffer, same Möller-Trumbore
//       barycentric solve, same flat cross-product normal with the two-sided view flip. If SurfaceShade.frag's reconstruction changes,
//       this must change with it or spawned surfels land on a different surface than the shaded pixel.
//
//    🔴 THIS IS AN #include MODULE — NO main. The consumer declares the visibility sampler, the six mesh/instance SSBOs, and the push
//       block (all mirroring SurfaceShade.frag's set 0 bindings b0-b7 + ShadeConstants) at whatever bindings it needs; this only
//       provides the record type and the reconstruct function so the layout + math live in exactly one place.

#ifndef FRONTIER_SURFEL_VISIBILITYRECONSTRUCT_GLSL
#define FRONTIER_SURFEL_VISIBILITYRECONSTRUCT_GLSL

// ---- Identity pack (must match SurfaceShade.frag / VisibilityRaster.frag / SoftwareRasterization.comp) ----
const uint SurfelReconstructPrimitiveBits = 20u;
const uint SurfelReconstructPrimitiveMask = (1u << SurfelReconstructPrimitiveBits) - 1u;
const uint SurfelReconstructSentinel      = 0xFFFFFFFFu;

// One RenderVertex, stride-32: position @0, normal @12, texcoord @24. Only POSITION is read (the normal is derived from the triangle
// plane), but the full stride is declared so std430 indexing stays correct. Mirrors SurfaceShade.frag's RenderVertex.
struct SurfelReconstructVertex
{
    float PositionX; float PositionY; float PositionZ; // @0
    float NormalX;   float NormalY;   float NormalZ;   // @12
    float TexU;      float TexV;                        // @24
};

// Mirrors SurfaceShade.frag's SceneInstance (std140, 208 B). Only Model is read here; the rest is declared so the block strides right.
struct SurfelReconstructInstance
{
    mat4 Model;
    vec4 NormalBasis[3];
    vec4 Tint;
    uint PartitionId;
    uint MaterialId;
    uint MeshOrdinal;
    uint Pad0;
    mat4 InverseModel;
};

// The reconstruction result: a valid surface hit with world position + flat normal, or Valid == false for a sky / sentinel pixel.
struct SurfelSurfaceHit
{
    vec3 WorldPosition;
    vec3 Normal;
    bool Valid;
};

// Barycentric weights of the point where the ray meets the triangle — Möller-Trumbore, identical to SurfaceShade.frag:185-200.
// Degenerate / parallel cases fall back to the first corner (reads as that corner's position, not a NaN).
vec3 SurfelRayTriangleBarycentrics(vec3 Origin, vec3 Direction, vec3 Corner0, vec3 Corner1, vec3 Corner2)
{
    vec3  Edge1 = Corner1 - Corner0;
    vec3  Edge2 = Corner2 - Corner0;
    vec3  PVec  = cross(Direction, Edge2);
    float Determinant = dot(Edge1, PVec);
    if (abs(Determinant) < 1e-12)
        return vec3(1.0, 0.0, 0.0);

    float InverseDeterminant = 1.0 / Determinant;
    vec3  TVec  = Origin - Corner0;
    float Bary1 = dot(TVec, PVec) * InverseDeterminant;
    vec3  QVec  = cross(TVec, Edge1);
    float Bary2 = dot(Direction, QVec) * InverseDeterminant;
    return vec3(1.0 - Bary1 - Bary2, Bary1, Bary2);
}

#endif
