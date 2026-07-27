/*============================================================================================================================================
                                                              ADJACENCYINDEX.H
============================================================================================================================================*/
// ðŸ§© The lightweight derived adjacency cache the overlays + CPU picker read â€” the engine-named port of the reference
//    prototype's halfedge.js TOPO. Rebuilt from a PolygonCluster (never a rival truth; a cache), it carries per-face loops +
//    fan triangulation + Newell normal + centroid, per-vertex incidence + one-ring, and the unordered loop-edge adjacency
//    (which faces meet at each edge â€” 1 = boundary, 2 = interior). Faces stay ORIGINAL polygons (tris / quads / N-gons); edges
//    are the N loop edges (consecutive corner pairs, wrap-around) â€” no fan diagonals, so a quad reports four real edges.
// ðŸ“ Edges are keyed by a PACKED uint64 ordered pair (min corner in the high 32 bits, max in the low 32) rather than the
//    prototype's "min_max" string â€” no per-edge allocation, and the key is a plain map lookup. EdgeVertices recovers the two
//    endpoints from a key; EdgeFaces lists the incident faces; FaceEdges[f] lists face f's loop-edge keys in winding order.
//    A half-edge structure (TopologyStructure) is a SEPARATE, heavier edit cache built only in edit mode â€” this index needs no
//    half-edges to answer visualization + picking queries, exactly as the prototype's TOPO did.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_ADJACENCY_ADJACENCYINDEX_H
#define FRONTIER_AUTHORING_GEOMETRY_ADJACENCY_ADJACENCYINDEX_H

#include "LinearAlgebra_Float64.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Frontier
{

struct PolygonCluster;   // ðŸ“ forward-declared; the .cpp reads its face stream + positions (Rendering-free, cheap include)

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ One fan triangle mapped back to its incident face â€” the flat raster/pick list the prototype called TOPO.tris. The three
//    corner-vertex indices are original cluster vertices (fan apex + two consecutive loop corners); IncidentFace is the
//    originating face ordinal, so a triangle hit resolves to the true polygon, never a fan-local fragment.
struct AdjacencyTriangle
{
    uint32_t VertexIndex[3] = { 0, 0, 0 };   // [-] - fan triangle corners (original cluster vertex indices)
    uint32_t IncidentFace   = 0;             // [-] - originating face ordinal this triangle belongs to
};

// ðŸ“ The two endpoint vertex indices of one edge, recovered from its packed key. A plain movable value type (a raw C-array
//    member cannot be an unordered_map value â€” it is neither assignable nor movable), so the edge maps store this. The key
//    packs the lower index high and the higher index low, so the endpoints land in a stable order by construction.
struct EdgeEndpoints
{
    uint32_t LowerVertex  = 0;   // [-] - endpoint with the smaller vertex index
    uint32_t HigherVertex = 0;   // [-] - endpoint with the larger vertex index
};

// ðŸ“ The whole derived adjacency cache for one cluster. Parallel arrays indexed by face ordinal (FaceVertexLoops â€¦
//    FaceEdgeKeys) or by vertex index (VertexFaces, VertexAdjacency); the edge maps are keyed by the packed uint64 edge key.
//    Every field mirrors a TOPO member so the port is 1:1 and the overlay / picker code reads the same shapes.
struct AdjacencyIndex
{
    uint32_t                                            FaceCount = 0;   // [-] - number of source faces

    std::vector<std::vector<uint32_t>>                  FaceVertexLoops;     // [-] - face ordinal -> loop of corner vertex indices (arity N)
    std::vector<std::vector<AdjacencyTriangle>>         FaceTriangleFan;     // [-] - face ordinal -> its (N-2) fan triangles
    std::vector<AdjacencyTriangle>                      TriangleStream;      // [-] - flat fan-triangle list across every face (raster/pick)
    std::vector<Vector3d>                               FaceNormal;          // [-] - face ordinal -> geometric Newell normal (unit)
    std::vector<Vector3d>                               FaceCenter;          // [cm]- face ordinal -> loop centroid

    std::vector<std::vector<uint32_t>>                  VertexFaces;         // [-] - vertex index -> incident face ordinals
    std::vector<std::vector<uint32_t>>                  VertexAdjacency;     // [-] - vertex index -> one-ring adjacent vertex indices (loop edges)

    std::vector<uint64_t>                               EdgeKeys;            // [-] - every unique packed edge key
    std::unordered_map<uint64_t, EdgeEndpoints>         EdgeVertices;        // [-] - edge key -> its two endpoint vertex indices
    std::unordered_map<uint64_t, std::vector<uint32_t>> EdgeFaces;           // [-] - edge key -> incident face ordinals (1 boundary, 2 interior)
    std::vector<std::vector<uint64_t>>                  FaceEdgeKeys;        // [-] - face ordinal -> its loop-edge keys, winding order
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Pack an unordered vertex pair into a stable uint64 edge key: min index in the high 32 bits, max in the low 32. The engine
// equivalent of the prototype's edgeKey("min_max") â€” order-independent, allocation-free.
uint64_t EncodeEdgeKey(uint32_t FirstVertex, uint32_t SecondVertex);

// Rebuild the whole adjacency cache from a cluster (the port of buildTopology). Faces stay original polygons; edges are the
// loop edges only. Clears Result first, then fills every field. Returns false (leaving Result empty) on a malformed cluster
// (a face with fewer than 3 corners, a truncated index stream, or a corner index out of range).
bool ResolveAdjacencyIndex(const PolygonCluster& Source, AdjacencyIndex& Result);

// Faces sharing a loop edge with face FaceOrdinal (the port of faceNeighbors). Returns the distinct adjacent face ordinals.
std::vector<uint32_t> EvaluateFaceAdjacency(const AdjacencyIndex& Index, uint32_t FaceOrdinal);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_GEOMETRY_ADJACENCY_ADJACENCYINDEX_H
