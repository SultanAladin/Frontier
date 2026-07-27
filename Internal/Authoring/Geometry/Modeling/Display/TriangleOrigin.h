/*============================================================================================================================================
                                                             TRIANGLEORIGIN.H
============================================================================================================================================*/
// ðŸ§© The provenance map that survives triangulation: every display triangle records the editable face it came from and the
//    three source face-corner references it was triangulated across. This is what lets a CPU (or later GPU) pick â€” a triangle
//    ordinal plus a barycentric coordinate â€” resolve back to a face / edge / vertex without reverse-searching the topology.
//    The display polygon is a throwaway view; this map keeps the one-way link back to the truth. DisplayPolygons bundles the
//    GPU stream with the map; they grow in lockstep so TriangleOrigins[t] describes the triangle whose indices are
//    Stream.Indices[3t .. 3t+2].
//
// ðŸ“ This is the CLUSTER-native provenance: SourceFace is the PolygonCluster face ordinal (index into FaceVertexCounts) and
//    SourceCorner[0..2] are FLAT corner cursors into FaceVertexIndices / FaceCornerTexture (in the triangle's winding order).
//    A corner cursor resolves to both its vertex index (FaceVertexIndices[cursor]) and its per-corner UV, so a barycentric hit
//    interpolates straight back to the three real corners. Edit-mode (half-edge) provenance is a separate later concern.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_MODELING_DISPLAY_TRIANGLEORIGIN_H
#define FRONTIER_AUTHORING_GEOMETRY_MODELING_DISPLAY_TRIANGLEORIGIN_H

#include "PolygonCluster.h"

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Sentinel for "no source assigned" â€” a default-constructed TriangleOrigin (out-of-range query result) reads as this on
//    every field, so a caller branches on InvalidCornerReference rather than bounds-checking the map by hand.
constexpr uint32_t InvalidCornerReference = 0xFFFFFFFFu;

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ One display triangle's provenance. SourceFace is the cluster face ordinal it tessellates part of; SourceCorner[0..2] are
//    the three flat corner cursors (in the triangle's winding order) its vertices were expanded from. A barycentric hit on the
//    triangle therefore interpolates straight back to these corners, and SourceFace resolves the picked face directly.
struct TriangleOrigin
{
    uint32_t SourceFace      = InvalidCornerReference;                                              // [-] - cluster face ordinal
    uint32_t SourceCorner[3] = { InvalidCornerReference, InvalidCornerReference, InvalidCornerReference };   // [-] - flat corner cursors, winding order
};

// ðŸ“ The display mirror's whole output: the interleaved GPU stream (the renderer's single presentation contract, reused from
//    PolygonCluster.h â€” never redefined here) plus the parallel provenance map. TriangleOrigins.size() always equals
//    Indices.size() / 3; one origin per emitted triangle. Both are regenerated together by ConstructDisplayPolygons; neither is
//    ever read back into the authoring cluster (anti-fragility Â§3.3).
struct DisplayPolygons
{
    RenderVertexStream          Stream          = {};   // [-] - interleaved float vertices + triangle-list indices
    std::vector<TriangleOrigin> TriangleOrigins = {};   // [-] - provenance, parallel to Stream.Indices in groups of three
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Retrieve the provenance of display triangle TriangleOrdinal (0-based, NOT an index offset â€” triangle t owns indices
// 3t .. 3t+2). Returns a default TriangleOrigin (all InvalidCornerReference) when the ordinal is out of range, so a caller can
// branch on the sentinel rather than bounds-checking by hand.
TriangleOrigin QueryTriangleOrigin(const DisplayPolygons& Display, uint32_t TriangleOrdinal);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_GEOMETRY_MODELING_DISPLAY_TRIANGLEORIGIN_H
