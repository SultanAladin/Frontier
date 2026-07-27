/*============================================================================================================================================
                                                              POLYGONBEVEL.H
============================================================================================================================================*/
// ðŸ§© The vertex-centric bevel / chamfer kernel for the authoring PolygonCluster, after Blender's bmesh_bevel but self-healing where
//    Blender is not: every offset is CLAMPED to a per-component safe limit so the bevel can never punch through the opposite geometry,
//    and offset points that fall within a weld threshold are MERGED (auto-merge, default on) so no slivers survive. The kernel is
//    organised around the affected VERTEX: around each vertex the incident edges split the face-fan into arcs at the beveled edges,
//    each arc contributes one boundary vertex where the two bounding offset lines meet, so a corner is closed by construction (no
//    holes, no centroid fan). One flat chamfer at Segments == 1; higher Segments sweep a rounded superellipse profile (Roundness:
//    0 = flat chamfer, 0.5 = circular arc, 1 = square shoulder). Edge / Vertex / Face modes; a face bevel is the edge bevel of the
//    selected region's silhouette loop. Unlike Extrude / Inset, width AND segments both reshape connectivity, so this kernel rebuilds
//    Target from scratch each call; the interactive modal re-runs it every frame from a saved base snapshot (never an idempotent slide).

#pragma once
#ifndef FRONTIER_AUTHORING_POLYGONAUTHORING_OPERATIONS_BEVEL_POLYGONBEVEL_H
#define FRONTIER_AUTHORING_POLYGONAUTHORING_OPERATIONS_BEVEL_POLYGONBEVEL_H

#include "LinearAlgebra_Float64.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Frontier
{

struct PolygonCluster;   // ðŸ“ forward-declared â€” the kernel rebuilds its face stream + positions; the .cpp pulls the full header.
struct AdjacencyIndex;   // ðŸ“ forward-declared â€” the clamp evaluator reads loops / normals / centres over the BASE topology only.

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Which component the bevel acts on, taken from the active selection mode. `.Category` suffix per the naming skill (replaces `Kind`).
enum class BevelCategory
{
    EdgeCategory,     // [-] - bevel each selected edge into a chamfer / rounded strip between its two adjacent faces
    VertexCategory,   // [-] - bevel each selected vertex into a dome cap over its incident edges
    FaceCategory      // [-] - bevel the selected region's silhouette loop (edge bevel of the boundary edges)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ The profile the rounded bevel sweeps between the two edge-offset points. Roundness drives a superellipse exponent:
//    0 = flat chamfer (straight cut), 0.5 = circular arc (Blender's round), 1 = square shoulder. CustomPoints is reserved for the
//    fast-follow draggable editor; when non-empty it overrides Roundness (the kernel samples the polyline instead of the superellipse).
struct BevelProfile
{
    double                Roundness    = 0.5;   // [-] - superellipse control (0 chamfer .. 0.5 round .. 1 square)
    std::vector<Vector2d> CustomPoints = {};    // [-] - fast-follow: draggable profile control points (empty = use Roundness)
};

// ðŸ“ The whole outcome of a bevel: what topology the beveled region now owns (the follow-on selection) and the safe max width the
//    modal clamps its live drag to. On a degenerate / no-op request Completed stays false and Target is left untouched.
struct BevelOutcome
{
    bool                         Completed = false;    // [-] - false + no mutation on a degenerate request
    std::unordered_set<uint32_t> ResultFaces;          // [-] - faces of the beveled region -> follow-on face selection
    std::unordered_set<uint32_t> ResultVertices;       // [-] - vertices the bevel introduced -> follow-on vertex selection
    double                       ClampLimit = 0.0;     // [-] - the safe max width; the modal clamps drag to this (never self-cross)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Bevel Target's active-mode selection by Width over Segments loops on Profile. The kernel clamps Width to the per-component safe limit
// (so the offset can never cross opposite geometry) and, when AutoMerge is set, welds offset points that fall within WeldThreshold
// (so near-coincident corners collapse instead of leaving slivers). On success Target's PolygonCluster is rebuilt in place from a
// fresh flat extract, Outcome is filled (ResultFaces / ResultVertices / ClampLimit), and true is returned. On a no-op (empty
// selection, zero clamp limit, near-zero width, degenerate topology) Target is left untouched, Outcome.Completed is false, and false
// is returned. SelectedEdges are packed undirected edge keys (min<<32 | max, matching EncodeEdgeKey). The caller must re-run
// ResolveAdjacencyIndex on the mutated Target afterwards â€” the passed-in BaseAdjacency is now stale.
bool BevelPolygonSelection(PolygonCluster&                     Target,
                           const AdjacencyIndex&               BaseAdjacency,
                           const std::unordered_set<uint32_t>& SelectedFaces,
                           const std::unordered_set<uint64_t>& SelectedEdges,
                           const std::unordered_set<uint32_t>& SelectedVertices,
                           BevelCategory                       Category,
                           double                              Width,
                           uint32_t                            Segments,
                           const BevelProfile&                 Profile,
                           bool                                AutoMerge,
                           double                              WeldThreshold,
                           BevelOutcome&                       Outcome);

// The safe-max width for the active selection, per mode: edge = perpendicular distance from each beveled edge's line to its adjacent
// face centroid; vertex = half the shortest incident edge; face = the region's silhouette-loop edge limit (via the edge rule on the
// boundary). The modal clamps its live width to this so the bevel never self-crosses. Returns 0.0 when the selection cannot be
// beveled (empty, non-manifold, degenerate). Reads Source + BaseAdjacency without mutating either.
double EvaluateBevelClampLimit(const PolygonCluster&               Source,
                               const AdjacencyIndex&               BaseAdjacency,
                               const std::unordered_set<uint32_t>& SelectedFaces,
                               const std::unordered_set<uint64_t>& SelectedEdges,
                               const std::unordered_set<uint32_t>& SelectedVertices,
                               BevelCategory                       Category);

// Sample the profile height fraction at parameter T in [0, 1] along the bevel arc. T = 0 / 1 are the two edge-offset points (height
// 0); the return is how far the swept point bulges toward the corner apex. Superellipse from Profile.Roundness when CustomPoints is
// empty (0 chamfer -> linear, 0.5 -> circular quarter-arc, 1 -> square shoulder); otherwise linear-interpolates the CustomPoints
// polyline. Free helper so the toolbar preview swatch can draw the same curve the kernel sweeps.
double EvaluateBevelProfile(double T, const BevelProfile& Profile);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_POLYGONAUTHORING_OPERATIONS_BEVEL_POLYGONBEVEL_H
