/*============================================================================================================================================
                                                            SUBDIVIDESIMPLE.H
============================================================================================================================================*/
// 🧩 One level of SIMPLE (linear) subdivision on an indexed-face polygon: every face of n corners is split into n quads using its
//    edge midpoints and its centroid, WITHOUT moving any point (unlike Catmull-Clark, which also smooths). It is the plainest
//    subdivision scheme because it needs no limit-surface math — pure connectivity refinement on the flat (Positions,
//    FaceVertexIndices, FaceVertexCounts) arrays the modifier stack extracts from the base PolygonCluster.
// 📝 Midpoints are shared between adjacent faces (deduplicated on the unordered endpoint-slot pair), so the refined polygon stays
//    watertight — two faces sharing an edge reference the same midpoint vertex, exactly as the unsubdivided edge was shared.

#pragma once
#ifndef FRONTIER_AUTHORING_MODELING_MODIFIER_SUBDIVIDESIMPLE_H
#define FRONTIER_AUTHORING_MODELING_MODIFIER_SUBDIVIDESIMPLE_H

#include "LinearAlgebra_Float64.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Apply ONE level of simple subdivision to an indexed-face polygon. The inputs are the flat format the display + topology
// consume: Positions seeds one point each, FaceVertexCounts[Face] is that face's corner count, FaceVertexIndices concatenates
// each face's corner indices in winding order. The outputs are the refined polygon in the same format — original points first,
// then one deduplicated edge midpoint per edge, then one centroid per face; every face becomes its n corner quads. Outputs are
// cleared first. A face with fewer than three corners is passed through unsplit (its corners copied verbatim).
void SubdivideSimpleLevel(const std::vector<Vector3d>& Positions,
                          const std::vector<uint32_t>& FaceVertexIndices,
                          const std::vector<uint32_t>& FaceVertexCounts,
                          std::vector<Vector3d>&       OutPositions,
                          std::vector<uint32_t>&       OutFaceVertexIndices,
                          std::vector<uint32_t>&       OutFaceVertexCounts);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_MODELING_MODIFIER_SUBDIVIDESIMPLE_H
