/*============================================================================================================================================
                                                            POLYGONTOPOLOGYEDIT.H
============================================================================================================================================*/
// ðŸ§© The Blender-standard topology-removal + hole kernels for the authoring PolygonCluster: Delete (leave a hole), Dissolve (merge
//    adjacent faces, stay watertight), Fill (cap one boundary edge-loop with an N-gon), and Bridge (wall a quad strip between two equal
//    boundary loops). All four are SINGLE-SHOT â€” they mutate the cluster's face stream + vertex field once and report the follow-on
//    selection through a shared TopologyEditOutcome; there is no interactive slide (unlike Extrude / Inset / Loop Cut). Each mirrors
//    the live Extrude idiom: read the base AdjacencyIndex, rewrite the flat face stream, keep the parallel per-corner UV array
//    consistent, and hand back the result component sets so the viewport re-selects the survivors and re-streams the display.

#pragma once
#ifndef FRONTIER_AUTHORING_POLYGONAUTHORING_OPERATIONS_TOPOLOGY_POLYGONTOPOLOGYEDIT_H
#define FRONTIER_AUTHORING_POLYGONAUTHORING_OPERATIONS_TOPOLOGY_POLYGONTOPOLOGYEDIT_H

#include <cstdint>
#include <unordered_set>

namespace Frontier
{

struct PolygonCluster;   // ðŸ“ forward-declared â€” the kernels mutate its face stream + positions; the .cpp pulls the full header.
struct AdjacencyIndex;   // ðŸ“ forward-declared â€” read the base loops / normals / edge incidence before any mutation.
struct SelectionState;   // ðŸ“ forward-declared â€” Delete / Dissolve read the active-mode set; the .cpp pulls ComponentSelection.h.

//------------------------------------------------------------------------------------------------------------------------
//                                                          ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ Which component set of the SelectionState a Delete / Dissolve reads (mirrors the active SelectionMode). `.Category` suffix per
//    the naming skill (replaces the banned `Kind`).
enum class DeleteCategory
{
    VertexCategory,   // [-] - operate on Selection.Vertices
    EdgeCategory,     // [-] - operate on Selection.Edges (packed keys)
    FaceCategory      // [-] - operate on Selection.Faces (ordinals)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ The whole outcome of one topology edit: what to re-select afterwards (shaped like ExtrudeOutcome's cap sets). Only the sets an
//    op produces are filled â€” Delete fills the surviving hole rim (ResultVertices), Fill / Bridge fill ResultFaces with the new
//    face(s), Dissolve fills ResultFaces with the merged N-gon(s). Completed is false with NO mutation on a degenerate request
//    (empty / mismatched selection, a non-boundary loop, mismatched bridge loop lengths).
struct TopologyEditOutcome
{
    bool                          Completed = false;   // [-] - false + Target untouched on a degenerate request
    std::unordered_set<uint32_t>  ResultFaces;         // [-] - follow-on face ordinals (new / merged faces)
    std::unordered_set<uint32_t>  ResultVertices;      // [-] - follow-on vertex indices (hole rim / merged verts)
    std::unordered_set<uint64_t>  ResultEdges;         // [-] - follow-on packed edge keys (bridge rungs / rails)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Delete the active-mode selection, leaving a hole (Blender's Delete). Face: drop the selected face ordinals, keep every vertex a
// surviving face still uses, compact the rest â€” ResultVertices is the removed region's rim. Edge: drop both faces incident to each
// selected edge, then drop any vertex left with no surviving incidence â€” a true hole. Vertex: drop every face touching a selected
// vertex, then the vertices themselves. Adjacency must describe Target's CURRENT topology. Returns false (Target untouched) on an
// empty selection for Category; the caller must re-run ResolveAdjacencyIndex afterwards (the passed-in Adjacency is now stale).
bool ResolveDeleteSelection(PolygonCluster&        Target,
                            const AdjacencyIndex&  Adjacency,
                            const SelectionState&  Selection,
                            DeleteCategory         Category,
                            TopologyEditOutcome&   Outcome);

// Dissolve the active-mode selection, keeping the surface watertight (Blender's Dissolve). Edge: union the two faces across each
// selected interior edge into one N-gon (the shared edge vanishes). Face: union a contiguous selected-face patch into one N-gon = its
// ordered outer boundary loop. Vertex: merge the incident face ring around each selected interior vertex into one face (the reverse
// of a poke); boundary / valence-under-3 vertices are skipped. Returns false (Target untouched) when nothing dissolvable is selected;
// re-run ResolveAdjacencyIndex afterwards.
bool ResolveDissolveSelection(PolygonCluster&        Target,
                              const AdjacencyIndex&  Adjacency,
                              const SelectionState&  Selection,
                              DeleteCategory         Category,
                              TopologyEditOutcome&   Outcome);

// Cap one selected boundary edge-loop (a hole) with a single N-gon face (Blender's Fill). SelectedEdges must chain into exactly ONE
// closed ring of boundary edges (each edge incident to a single face). The new face's winding is chosen so its normal opposes the
// average adjacent-face normal (faces outward); per-corner UVs copy the ring verts' existing corner UVs where present. ResultFaces =
// the new face. Returns false (Target untouched) when the selection is not a single closed boundary ring. Re-run adjacency after.
bool ConstructBoundaryFill(PolygonCluster&                      Target,
                           const AdjacencyIndex&                Adjacency,
                           const std::unordered_set<uint64_t>&  SelectedEdges,
                           TopologyEditOutcome&                 Outcome);

// Wall a quad strip between TWO selected boundary edge-loops of EQUAL edge count, closing both holes into a tube (Blender's Bridge
// Edge Loops). SelectedEdges must chain into exactly two closed boundary rings of the same length N; loop B is rotated + direction-
// chosen to minimise the total corresponding-vertex distance to loop A. Appends N quads walling the rings. ResultFaces = the N new
// quads. Returns false (Target untouched) when the selection is not two equal-length boundary rings. Re-run adjacency after.
bool BridgeBoundaryLoops(PolygonCluster&                      Target,
                         const AdjacencyIndex&                Adjacency,
                         const std::unordered_set<uint64_t>&  SelectedEdges,
                         TopologyEditOutcome&                 Outcome);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_POLYGONAUTHORING_OPERATIONS_TOPOLOGY_POLYGONTOPOLOGYEDIT_H
