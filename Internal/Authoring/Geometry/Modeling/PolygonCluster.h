/*============================================================================================================================================
                                                               POLYGONCLUSTER.H
============================================================================================================================================*/
// ðŸ§© The CPU authoring polygon container and its derived GPU presentation. The authoring side (VertexField + a face list that
//    preserves triangles / quads / N-gons + per-corner texture coordinates) is the single source of truth, in double precision.
//    The GPU presentation (RenderVertexStream: interleaved float vertices + a triangle index buffer) is DERIVED by a one-way,
//    re-runnable function and is never read back to recover authoring topology (anti-fragility Â§3.3). Float conversion happens
//    only here, at the presentation boundary, exactly as the Math library and Camera intend.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_MODELING_POLYGONCLUSTER_H
#define FRONTIER_AUTHORING_GEOMETRY_MODELING_POLYGONCLUSTER_H

#include "LinearAlgebra_Float64.h"
#include "VertexField.h"
#include "PolygonDescriptor.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         AUTHORING STRUCT
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Authoring polygon. Faces are stored as a flat index stream plus a per-face corner count, so a triangle (3), a quad (4),
//    and an N-gon (n) coexist with no special cases and the original topology survives a round trip (Phase 2 requirement).
//    FaceVertexIndices concatenates every face's vertex indices in winding order; FaceVertexCounts[f] is face f's corner
//    count, and the running sum locates face f's slice. Indices address into Attributes (the shared VertexField).
//
// ðŸ“ FaceCornerTexture is the persisted home of PER-CORNER UVs: one Vector2d per entry of FaceVertexIndices (same length, same
//    order), so a seam vertex shared by two faces holds two distinct corner UVs â€” the truth painting / UV editing later read.
//    It is empty when the source exports no UVs (the reference body derives UVs at unwrap time, so it imports empty here);
//    ConstructRenderVertexStream falls back to the per-vertex VertexField UV, then to zero, so an empty array is not a failure.
struct PolygonCluster
{
    VertexField           Attributes         = {};   // [-] - shared SoA vertex attributes (positions mandatory)
    std::vector<uint32_t> FaceVertexIndices  = {};   // [-] - concatenated per-face vertex indices, winding order
    std::vector<uint32_t> FaceVertexCounts   = {};   // [-] - corner count per face (3 = tri, 4 = quad, n = N-gon)
    std::vector<Vector2d> FaceCornerTexture  = {};   // [-] - per-corner UV parallel to FaceVertexIndices (empty = none)
    std::vector<uint32_t> FaceMaterialIndex  = {};   // [-] - per-face material identity (empty = every face is material 0)
    std::vector<Vector3d> FaceCornerColour   = {};   // [-] - per-corner linear RGB parallel to FaceVertexIndices (empty = none)
    PolygonDescriptor     Descriptor         = {};   // [-] - cheap summary; refresh via EvaluatePolygonDescriptor
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    GPU PRESENTATION CONTRACT
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ One interleaved GPU vertex. Float, tightly packed, and laid out exactly as a future Vulkan vertex-input binding will
//    declare it (position @0, normal @12, texcoord @24; stride 32). This is the engine's single polygon presentation contract
//    â€” CAD tessellation and primitive generators must all emit THIS type so the renderer never branches on producer (Â§6).
struct RenderVertex
{
    float Position[3]          = { 0.0f, 0.0f, 0.0f };   // [cm] - world authoring position, cast to float at this boundary
    float Normal[3]            = { 0.0f, 0.0f, 0.0f };   // [-]  - unit normal (zeroed when the authoring field has none)
    float TextureCoordinate[2] = { 0.0f, 0.0f };         // [-]  - UV (zeroed when neither corner nor vertex UV exists)
};

// ðŸ“ The derived, triangulated GPU stream: a deduplication-free expansion for now (one render vertex per authoring corner
//    reference). Indices are triangle list (3 per triangle). A later Topology/Display brick adds loop-split dedup and
//    ear-clipping; this struct and the function below already express the whole-polygon aggregate shape they will reuse.
// ðŸ“ CornerColour + CornerMaterialIndex are BAKER-ONLY parallel arrays (one entry per Vertices entry): the renderer's
//    stride-32 vertex-input contract never reads them, so they carry authoring colour / material identity into the bake
//    pipeline without widening RenderVertex. Both stay empty (the default) for producers that emit no colour / material â€”
//    ConstructTriangleRayVolume then substitutes white / material 0 â€” so all non-bake callers are unaffected.
struct RenderVertexStream
{
    std::vector<RenderVertex> Vertices           = {};   // [-] - interleaved float vertices
    std::vector<uint32_t>     Indices            = {};   // [-] - triangle-list indices into Vertices
    std::vector<float>        CornerColour       = {};   // [-] - baker-only linear RGB, 3 floats per Vertices entry (empty = none)
    std::vector<uint32_t>     CornerMaterialIndex = {};   // [-] - baker-only material identity, 1 per Vertices entry (empty = all 0)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Refresh and return the polygon's descriptor: vertex / face / triangle counts, bounds, and attribute-presence booleans.
// Triangle count is the fan count sum over faces (corner count - 2 per face). Also writes the result into Source.Descriptor.
PolygonDescriptor EvaluatePolygonDescriptor(PolygonCluster& Source);

// Derive the GPU presentation for the WHOLE polygon in one aggregate pass (anti-fragility Â§3.1 â€” no per-face public entry).
// Each face is fan-triangulated (convex baseline; concave N-gon ear-clipping is deferred to the Topology/Display brick,
// and the signature already accommodates a richer body). Authoring doubles are cast to float here and only here. Each corner's
// UV comes from FaceCornerTexture when present, else the per-vertex VertexField UV, else zero. Returns false without partial
// writes on degenerate input (a face index out of range, a face with fewer than 3 corners, or a truncated corner-UV array).
bool ConstructRenderVertexStream(const PolygonCluster& Source, RenderVertexStream& Result);

// Release authoring attributes, face arrays, and reset the descriptor to its default (identity preserved is NOT assumed â€”
// the descriptor is fully reset; assign a fresh identity afterwards).
void ResetPolygonCluster(PolygonCluster& Target);

} // namespace Frontier

#endif
