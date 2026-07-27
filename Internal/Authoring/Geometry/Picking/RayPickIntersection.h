/*============================================================================================================================================
                                                           RAYPICKINTERSECTION.H
============================================================================================================================================*/
// ðŸ§© The CPU single-click picker, cluster-native path: cast one object-space Ray against a DisplayPolygons' triangles, keep the
//    nearest hit, and resolve it â€” through the TriangleOrigin provenance + the AdjacencyIndex â€” back to a source face / nearest
//    corner vertex / nearest real loop edge. This is the mouse-selection path: the runtime builds the ray from the clicked
//    viewport's camera, hands it here with the specimen's display mirror + adjacency, and the returned PickOutcome names the
//    component to select. No half-edge (TopologyStructure) is needed â€” the non-edit path resolves entirely off the cluster.
// ðŸ“ Classification is barycentric (resolution-independent, enough for the authored tri/quad/N-gon cases): the largest corner
//    weight names the snapped vertex, the smallest weight names the opposite triangle side, and the face is always the hit
//    triangle's SourceFace. A fan diagonal (a triangle side whose two source-vertices are NOT a real loop edge in the adjacency)
//    resolves to an invalid edge rather than a fake one, so edge mode never selects a diagonal that is not a true polygon edge.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_PICKING_RAYPICKINTERSECTION_H
#define FRONTIER_AUTHORING_GEOMETRY_PICKING_RAYPICKINTERSECTION_H

#include "IntersectionSolver.h"
#include "TriangleOrigin.h"

#include <cstdint>

namespace Frontier
{

struct AdjacencyIndex;   // ðŸ“ forward-declared; resolves the loop-edge test for classification

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Sentinels for "no component resolved". The face ordinal / vertex index reuse InvalidCornerReference (0xFFFFFFFF); the edge
//    reuses the all-ones uint64 so an invalid edge key never collides with a real packed key (a real key's high 32 bits are a
//    vertex index < the vertex count, never 0xFFFFFFFF).
constexpr uint64_t InvalidEdgeKey = 0xFFFFFFFFFFFFFFFFull;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ The outcome of one component pick. HitEnabled is false for a ray that missed every triangle (all other fields are then
//    meaningless). On a hit: Distance is the parametric t along the object-space ray, HitPoint is the object-space hit, Face is
//    the resolved source face ordinal (always valid on a hit), Vertex is the nearest source corner vertex, and Edge is the
//    nearest real loop edge's packed key â€” InvalidEdgeKey on a fan diagonal.
struct PickOutcome
{
    bool     HitEnabled      = false;                   // [-]  - true iff the ray hit a triangle
    uint32_t TriangleOrdinal = InvalidCornerReference;  // [-]  - display triangle the ray hit
    double   Distance        = 0.0;                     // [cm] - parametric t along the object-space ray to the hit
    Vector3d HitPoint        = { 0.0, 0.0, 0.0 };       // [cm] - object-space hit position
    uint32_t Face            = InvalidCornerReference;  // [-]  - resolved source face ordinal (valid on a hit)
    uint32_t Vertex          = InvalidCornerReference;  // [-]  - nearest source corner vertex index
    uint64_t Edge            = InvalidEdgeKey;          // [-]  - nearest real loop-edge key (InvalidEdgeKey on a fan diagonal)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Cast ObjectRay (already in the display's object space) against every triangle of Display, returning the nearest hit resolved
// to its source vertex / edge / face through Display.TriangleOrigins + Adjacency. When BackfaceSelectEnabled is false, triangles
// facing away from the ray are skipped so the pick resolves against the visible front shell only; true accepts either facing
// (X-ray). Cluster supplies the corner-vertex positions the triangles are cast against. A miss returns HitEnabled false.
// O(triangles); a BVH refit is the later acceleration.
PickOutcome ResolveElementPick(const Ray&              ObjectRay,
                               const PolygonCluster&   Cluster,
                               const DisplayPolygons&  Display,
                               const AdjacencyIndex&   Adjacency,
                               bool                    BackfaceSelectEnabled);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_GEOMETRY_PICKING_RAYPICKINTERSECTION_H
