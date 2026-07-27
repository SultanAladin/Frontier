/*============================================================================================================================================
                                                               POLYGONLOOPCUT.H
============================================================================================================================================*/
// ðŸ§© The Blender-standard loop-cut kernel for the authoring PolygonCluster: threading the quad ring the seed edge belongs to,
//    it mints CutCount evenly-spaced loop vertices on every ring edge and rewrites each crossed quad into CutCount+1 strips,
//    splicing every cut point into any face a terminal ring edge touches so the result stays watertight (no T-junctions). The
//    ring walk crosses each adjacent quad to its opposite edge and hops on until the ring closes, reaches a boundary, or meets
//    a non-quad â€” where it terminates cleanly. The new loop vertices are created at their EVEN rest position (Slide 0) so the
//    topology exists before any motion; the caller then slides the whole bundle along the ring rails through
//    LoopCutOffsetAdvance, reusing the viewport's modal-drag path exactly as Extrude does. A non-minting preview
//    (ResolveLoopCutPreview) returns the same cut lines as world-space segments so the Ctrl+R ghost tracks the eventual cut
//    while the wheel adjusts the count. Live-native mirror of the retired Polygon/Operations/LoopCutOperation kernel.

#pragma once
#ifndef FRONTIER_AUTHORING_POLYGONAUTHORING_OPERATIONS_LOOPCUT_POLYGONLOOPCUT_H
#define FRONTIER_AUTHORING_POLYGONAUTHORING_OPERATIONS_LOOPCUT_POLYGONLOOPCUT_H

#include "LinearAlgebra_Float64.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Frontier
{

struct PolygonCluster;   // ðŸ“ forward-declared â€” the kernel reads its face stream + positions and rewrites both in place.

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ One appended loop vertex and the ring-edge axis it slides along while the caller drives the interactive offset. Direction
//    is the ring edge's spanning vector divided by (CutCount+1) oriented coherently around the ring; OriginPosition is the even
//    rest position (Slide 0), so LoopCutOffsetAdvance re-derives the live position idempotently as
//    OriginPosition + Direction * Slide â€” the same origin-snapshot rule Extrude / the drag gizmo use.
struct LoopCutOffsetSample
{
    uint32_t NewVertex      = 0;                     // [-]  - appended loop vertex index into the cluster's VertexField
    Vector3d Direction      = { 0.0, 0.0, 0.0 };     // [cm] - one-division slide axis ((B - A) / (CutCount + 1))
    Vector3d OriginPosition = { 0.0, 0.0, 0.0 };     // [cm] - even rest position (the Slide-0 cut point)
};

// ðŸ“ One segment of the loop-cut PREVIEW line: the two ring-edge cut points of a single crossed quad at one cut level (object
//    space). The whole preview is the union of these short segments across every quad the ring crosses at every cut level â€”
//    drawn before any geometry is minted, so the user sees where the loops will land. Each pair is exactly the points
//    ConstructEdgeLoopCut would create for that quad's two ring edges, so the preview tracks the eventual cut precisely.
struct LoopCutSegment
{
    Vector3d MidpointA = { 0.0, 0.0, 0.0 };   // [cm] - cut point on one of the crossed quad's two ring edges
    Vector3d MidpointB = { 0.0, 0.0, 0.0 };   // [cm] - matching cut point on the opposite ring edge of the same quad
};

// ðŸ“ The whole outcome of a loop-cut insertion: the appended loop vertices + how to drive the live slide, plus the inserted
//    loop as the follow-on selection (LoopVertices â†’ the new vertex selection, LoopEdges â†’ the new edge selection, so the
//    slide + overlay track exactly the loop just cut). Completed is false (Target untouched) when the seed pair is not an edge
//    of the cluster or it borders no quad.
struct LoopCutOutcome
{
    bool                             Completed = false;   // [-] - false + no mutation on a no-op request
    std::vector<LoopCutOffsetSample> OffsetSamples;      // [-] - one per appended loop vertex (drives the slide)
    std::unordered_set<uint32_t>     LoopVertices;        // [-] - appended loop vertices -> new vertex selection
    std::unordered_set<uint64_t>     LoopEdges;           // [-] - inserted loop edge keys -> new edge selection
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Insert CutCount evenly-spaced edge loops through the ring containing the seed edge (SeedVertexA / SeedVertexB are cluster
// vertex indices â€” the picked edge key's two endpoints). CutCount is clamped to >= 1; the cuts sit at parameters i/(CutCount+1)
// along each ring edge, splitting each crossed quad into CutCount+1 strips and splicing every cut point into the faces a
// terminal ring edge touches (no T-junctions). On success Target's face stream + positions (+ corner UVs, linearly
// interpolated along each ring edge when the cluster carries them) are rewritten in place, Outcome is filled with the slide
// samples + the inserted loop selection, and true is returned. On a no-op (seed pair not an edge, or it borders no quad) Target
// is left untouched, Outcome.Completed is false, and false is returned. The caller must re-run ResolveAdjacencyIndex on the
// mutated Target afterwards.
bool ConstructEdgeLoopCut(PolygonCluster& Target,
                          uint32_t        SeedVertexA,
                          uint32_t        SeedVertexB,
                          uint32_t        CutCount,
                          LoopCutOutcome& Outcome);

// Resolve the loop-cut preview lines for the ring containing the seed edge WITHOUT minting any geometry: walks the same quad
// ring ConstructEdgeLoop walks and returns CutCount LoopCutSegments per crossed quad (one per cut level, each spanning the two
// opposite ring edges at parameter i/(CutCount+1)). SeedVertexA / SeedVertexB are cluster vertex indices; CutCount is clamped
// to >= 1. Returns true (OutSegments populated) when the seed is a real edge that touches at least one quad; false (OutSegments
// cleared) on the same no-op condition ConstructEdgeLoopCut reports. The runtime calls this each cycle while the Ctrl+R preview
// tracks the hovered edge and the wheel adjusts the count.
bool ResolveLoopCutPreview(const PolygonCluster&        Target,
                           uint32_t                     SeedVertexA,
                           uint32_t                     SeedVertexB,
                           uint32_t                     CutCount,
                           std::vector<LoopCutSegment>& OutSegments);

// Drive the interactive slide for the inserted loop: re-derives each sampled loop vertex as OriginPosition + Direction * Slide
// (signed), so it is idempotent from the even-rest snapshot and Slide 0 restores the even position with no special case. Slide
// is expected in [-1, 1] (one division either way); the caller clamps. Returns false (no writes) if any sample's NewVertex is
// out of range for Target.
bool LoopCutOffsetAdvance(PolygonCluster&                          Target,
                          const std::vector<LoopCutOffsetSample>&  Samples,
                          double                                   Slide);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_POLYGONAUTHORING_OPERATIONS_LOOPCUT_POLYGONLOOPCUT_H
