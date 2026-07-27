/*============================================================================================================================================
                                                              VERTEXFIELD.H
============================================================================================================================================*/
// ðŸ§© Structure-of-arrays CPU authoring vertex attributes (double precision). Position is mandatory; Normal / TextureCoordinate /
//    Color are optional and present only when their array is non-empty. Authoring stays double; the float conversion happens later,
//    only at the GPU-presentation boundary (see PolygonCluster.h / ConstructRenderVertexStream), mirroring the Math library's boundary rule.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_MODELING_VERTEXFIELD_H
#define FRONTIER_AUTHORING_GEOMETRY_MODELING_VERTEXFIELD_H

#include "LinearAlgebra_Float64.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Parallel attribute arrays indexed by a single vertex index. An empty optional array means that attribute is absent;
//    when present it is kept the same length as Position. SoA is deliberate so storage can migrate to GPU-resident or
//    interleaved layouts later without changing the authoring API (anti-fragility: identity/index stays stable).
struct VertexField
{
    std::vector<Vector3d> Position;            // [cm] - world-space authoring position (mandatory)
    std::vector<Vector3d> Normal;              // [-]  - unit surface normal (optional; empty = absent)
    std::vector<Vector2d> TextureCoordinate;   // [-]  - UV coordinate (optional; empty = absent)
    std::vector<Vector3d> Color;               // [-]  - linear RGB vertex color (optional; empty = absent)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Append one position and return the new vertex's index. Optional attribute arrays are not touched here; assign them by
// index through the Refresh* calls below once the slot exists.
uint32_t AccumulateVertexPosition(VertexField& Field, const Vector3d& Position);

// Set an optional attribute at an existing vertex index, growing the optional array to match Position length on first use.
// Returns false if VertexIndex is out of range. (Approved verb: Refresh â€” Assign/Set are forbidden.)
bool RefreshVertexNormal(VertexField& Field, uint32_t VertexIndex, const Vector3d& Normal);
bool RefreshVertexTexture(VertexField& Field, uint32_t VertexIndex, const Vector2d& TextureCoordinate);
bool RefreshVertexColor(VertexField& Field, uint32_t VertexIndex, const Vector3d& Color);

// Number of authoring vertices (the Position array length).
uint32_t EvaluateVertexCount(const VertexField& Field);

// Release every attribute array back to empty.
void ResetVertexField(VertexField& Field);

} // namespace Frontier

#endif
