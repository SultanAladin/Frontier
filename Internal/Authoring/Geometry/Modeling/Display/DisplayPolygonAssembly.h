/*============================================================================================================================================
                                                          DISPLAYPOLYGONASSEMBLY.H
============================================================================================================================================*/
// ðŸ§© The display mirror's entry point: ConstructDisplayPolygons derives the throwaway GPU triangle view (plus its provenance
//    map) from the authoring PolygonCluster â€” the single source of truth on the non-edit path (Â§2.1). The cluster is
//    fan-triangulated verbatim (triangles / quads / N-gons preserved as source faces), each corner is expanded into an
//    interleaved float RenderVertex, and one TriangleOrigin is recorded per emitted triangle so a later pick resolves back to
//    the source face + corners. The derivation is one-way and re-runnable: it never mutates the cluster and is never read back
//    to recover it (Â§3.3). Double->float conversion happens only here, at this presentation boundary.
// ðŸ“ A separate half-edge (TopologyStructure) overload is a later edit-mode concern; this phase renders + picks straight off
//    the cluster, so only the cluster overload exists here.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_MODELING_DISPLAY_DISPLAYPOLYGONASSEMBLY_H
#define FRONTIER_AUTHORING_GEOMETRY_MODELING_DISPLAY_DISPLAYPOLYGONASSEMBLY_H

#include "PolygonCluster.h"
#include "TriangleOrigin.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Construct the display polygon for the WHOLE cluster in one aggregate pass (no per-face public entry, mirroring
// ConstructRenderVertexStream Â§3.1). Each face is fan-triangulated (v0, vi, vi+1); its corners are expanded into interleaved
// float RenderVertices (no dedup â€” one render vertex per corner reference, so seams stay split), and one TriangleOrigin is
// recorded per triangle carrying the source face ordinal and the three flat corner cursors in winding order. UV per corner is
// resolved from FaceCornerTexture, then the per-vertex VertexField UV, then zero â€” identical to ConstructRenderVertexStream.
//
// Two-pass, no-partial-writes contract: Result is fully reset, then written only if the whole cluster validates. Returns false
// WITHOUT partial writes (Result left empty) when a face has fewer than three corners, an index out of range, a truncated
// index stream, or (when present) a corner-UV array that does not cover every corner. An empty cluster returns true with an
// empty DisplayPolygons.
bool ConstructDisplayPolygons(const PolygonCluster& Source, DisplayPolygons& Result);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_GEOMETRY_MODELING_DISPLAY_DISPLAYPOLYGONASSEMBLY_H
