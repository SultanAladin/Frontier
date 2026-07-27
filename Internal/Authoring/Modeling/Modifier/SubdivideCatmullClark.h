/*============================================================================================================================================
                                                         SUBDIVIDECATMULLCLARK.H
============================================================================================================================================*/
// 🧩 One level of CATMULL-CLARK subdivision on an indexed-face polygon — the smooth quad scheme (Catmull & Clark 1978). Unlike
//    SubdivideSimple (connectivity only), it also REPOSITIONS points toward the limit surface: each face contributes a face
//    point (centroid), each edge an edge point, and every original vertex is pulled toward the average of its incident face and
//    edge points by valence weighting. Operates on the same flat (Positions, FaceVertexIndices, FaceVertexCounts) arrays the
//    modifier stack extracts from the base PolygonCluster, so it composes level-on-level with no half-edge structure.
// 📝 Robustness beyond a plain implementation (the "more than Blender" target): (1) OPEN BOUNDARIES follow the boundary curve —
//    a boundary edge point is its midpoint and a boundary vertex uses the (Prev + 6·P + Next)/8 crease rule, so open surfaces do
//    not shrink inward. (2) SEMI-SHARP CREASES — a per-edge continuous EdgeSharpness in [0,1] linearly blends each edge point and
//    each incident-vertex update between the smooth rule (0) and the hard rule (1 = held midpoint / held vertex). A vertex with
//    two sharp edges creases along them; three or more pins it as a corner. Blender exposes only an integer crease; the
//    continuous weight here gives finer control of edge softness.

#pragma once
#ifndef FRONTIER_AUTHORING_MODELING_MODIFIER_SUBDIVIDECATMULLCLARK_H
#define FRONTIER_AUTHORING_MODELING_MODIFIER_SUBDIVIDECATMULLCLARK_H

#include "LinearAlgebra_Float64.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Apply ONE level of Catmull-Clark subdivision. Inputs are the flat indexed-face arrays (see SubdivideSimple.h for the format).
// EdgeSharpness maps a canonical undirected edge key (lower vertex index in the high 32 bits, higher in the low 32 — matching
// AdjacencyIndex::EncodeEdgeKey) to a sharpness in [0,1]; an absent edge is fully smooth (0). BoundarySmoothEnabled selects
// whether open-boundary vertices relax along the boundary curve (true, the smooth boundary rule) or are held in place (false,
// a hard border). Outputs are the refined polygon in the same flat format — original vertices first (repositioned), then one
// deduplicated edge point per edge, then one face point per face; every n-gon becomes its n corner quads. Outputs are cleared
// first. A face with fewer than three corners is passed through unsplit (its corners copied verbatim, positions unchanged).
void SubdivideCatmullClarkLevel(const std::vector<Vector3d>&               Positions,
                                const std::vector<uint32_t>&               FaceVertexIndices,
                                const std::vector<uint32_t>&               FaceVertexCounts,
                                const std::unordered_map<uint64_t, float>& EdgeSharpness,
                                bool                                       BoundarySmoothEnabled,
                                std::vector<Vector3d>&                     OutPositions,
                                std::vector<uint32_t>&                     OutFaceVertexIndices,
                                std::vector<uint32_t>&                     OutFaceVertexCounts);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_MODELING_MODIFIER_SUBDIVIDECATMULLCLARK_H
