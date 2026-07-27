/*============================================================================================================================================
                                                             POLYGONDESCRIPTOR.H
============================================================================================================================================*/
// ðŸ§© Lightweight metadata for a PolygonCluster: stable identity, element counts, the axis-aligned bounding extent, and which optional
//    vertex attributes are present. Carries no heavy geometry â€” it is the cheap-to-copy summary a Scene / Outliner reads.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_MODELING_POLYGONDESCRIPTOR_H
#define FRONTIER_AUTHORING_GEOMETRY_MODELING_POLYGONDESCRIPTOR_H

#include "LinearAlgebra_Float64.h"
#include "VertexField.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Opaque, stable polygon identity. A plain integer alias for now; this is the seam where a MicroUtils generational handle
//    (IdentifierAllocation) drops in later, so callers reference identity by type, never by raw array index (anti-fragility Â§3.2).
using PolygonIdentifier = uint64_t;

// ðŸ“ Sentinel for "no identity assigned yet".
constexpr PolygonIdentifier UnassignedPolygonIdentifier = 0;

// ðŸ“ Axis-aligned bounding extent in authoring space. An empty polygon yields MinimumCorner > MaximumCorner (degenerate),
//    which EvaluatePolygonBounds reports via the returned BoundsValidEnabled-style emptiness of the source field.
struct PolygonBounds
{
    Vector3d MinimumCorner = { 0.0, 0.0, 0.0 };   // [cm] - per-axis minimum of every position
    Vector3d MaximumCorner = { 0.0, 0.0, 0.0 };   // [cm] - per-axis maximum of every position
};

// ðŸ“ Cheap summary of a PolygonCluster. Counts and bounds are derived (EvaluatePolygonDescriptor in PolygonCluster.h refreshes them); the booleans
//    use the project's Enabled suffix instead of an is-prefix.
struct PolygonDescriptor
{
    PolygonIdentifier Identity        = UnassignedPolygonIdentifier;   // [-] - stable opaque identity
    uint32_t          VertexCount     = 0;                             // [-] - authoring vertex count
    uint32_t          FaceCount       = 0;                             // [-] - authoring face count (quads / N-gons preserved)
    uint32_t          TriangleCount   = 0;                             // [-] - triangles the GPU presentation derives to
    PolygonBounds     Bounds          = {};                            // [cm]- axis-aligned extent of the authoring positions
    bool              NormalEnabled   = false;                         // [-] - Normal attribute present
    bool              TextureEnabled  = false;                         // [-] - TextureCoordinate attribute present
    bool              ColorEnabled    = false;                         // [-] - Color attribute present
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Sweep every position in Field for its axis-aligned minimum / maximum corner. An empty field returns a zeroed PolygonBounds.
PolygonBounds EvaluatePolygonBounds(const VertexField& Field);

} // namespace Frontier

#endif
