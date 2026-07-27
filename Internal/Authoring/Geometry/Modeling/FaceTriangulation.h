/*============================================================================================================================================
                                                             FACETRIANGULATION.H
============================================================================================================================================*/
// ðŸ§© One shared face-to-triangle policy for every cluster derivation. A triangle or quad is fanned (fast, always correct for
//    those); an N-gon (five+ corners, which a fan mis-fills whenever the face is concave) is projected onto its best-fit plane
//    by a Newell normal and triangulated with Earcut. The result is a flat run of LOCAL corner indices (0..CornerCount-1) in
//    winding order â€” the caller maps each local index onto its own flat corner cursor, vertex layout, and provenance record,
//    so this unit never touches the GPU stream or TriangleOrigin directly.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_MODELING_FACETRIANGULATION_H
#define FRONTIER_AUTHORING_GEOMETRY_MODELING_FACETRIANGULATION_H

#include <cstdint>
#include <vector>
#include "LinearAlgebra_Float64.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        FREE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Triangulate one face from its corner positions (world cm, in face order). Returns a flat triplet run of LOCAL corner
//    indices [0, CornerPositions.size()) â€” length is a multiple of three, (Count - 2) triangles for a well-formed face.
//    Fewer than three corners yields an empty run. Triangles and quads fan from corner 0; five-plus corners route through the
//    Newell-plane Earcut path so a concave N-gon fills correctly rather than spilling across its reflex corners.
std::vector<uint32_t> TriangulateFaceCorners(const std::vector<Vector3d>& CornerPositions);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_GEOMETRY_MODELING_FACETRIANGULATION_H
