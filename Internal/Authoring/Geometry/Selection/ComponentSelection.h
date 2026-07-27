/*============================================================================================================================================
                                                            COMPONENTSELECTION.H
============================================================================================================================================*/
// ðŸ§© The component-selection state + operations for the non-edit (cluster) path â€” the engine-named port of the reference
//    prototype's selection.js. Owns the three active sets (faces / vertices / edges keyed by packed edge key) plus a
//    single-anchor LastPick, and the mode-aware operations the hotkeys drive: additive click toggle, whole edge-loop from a
//    seed (the vertex-centric valence-4 quad rule with a straightness fallback), select-linked (connected component by shared
//    vertices, seams ignored), and select-all. A loop preview (the ghost set a modifier-held click would grab) is computed
//    without mutating the state, so the viewport can show the loop direction before the user commits.
// ðŸ“ This reads only an AdjacencyIndex (the halfedge.js TOPO shape) â€” no half-edge, no cluster mutation â€” exactly as the
//    prototype's selection module read TOPO. Faces / vertices are ordinal indices into the cluster; edges are packed keys
//    (EncodeEdgeKey), so an edge in a set is directly the AdjacencyIndex.EdgeVertices / EdgeFaces key.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_SELECTION_COMPONENTSELECTION_H
#define FRONTIER_AUTHORING_GEOMETRY_SELECTION_COMPONENTSELECTION_H

#include "SelectionMode.h"

#include <cstdint>
#include <unordered_set>

namespace Frontier
{

struct AdjacencyIndex;   // ðŸ“ forward-declared; every operation reads its loops / adjacency / edge incidence
struct PolygonCluster;   // ðŸ“ forward-declared; the loop walk's straightness fallback reads its vertex positions

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ The single anchor for a two-pick path/loop gesture (the prototype's SEL.lastPick). Enabled is false until the first
//    committed pick; Mode records which set the anchor lives in so a later modifier-click can decide loop-vs-path.
struct SelectionAnchor
{
    bool          Enabled      = false;             // [-] - true once a pick has committed an anchor
    SelectionMode Mode         = FaceMode;          // [-] - the set the anchor id indexes
    uint32_t      FaceOrVertex = 0;                 // [-] - anchor face ordinal (Face) or vertex index (Vertex)
    uint64_t      EdgeKey      = 0;                 // [-] - anchor edge key (Edge mode)
};

// ðŸ“ The whole selection state â€” three parallel active sets (only the active mode's set is authoritative for a gesture, the
//    others are read when an operation crosses modes) + the pick anchor. Plain sets of ordinals / keys, mirroring SEL.
struct SelectionState
{
    std::unordered_set<uint32_t>  Faces;            // [-] - selected face ordinals
    std::unordered_set<uint32_t>  Vertices;         // [-] - selected vertex indices
    std::unordered_set<uint64_t>  Edges;            // [-] - selected packed edge keys
    SelectionAnchor               LastPick;         // [-] - anchor for two-pick loop / path gestures
};

// ðŸ“ The ghost set a modifier-held click would grab, computed on hover without mutating the live selection (the prototype's
//    VP3.preview). Only one of the two sets is populated per hover: Edges for a hovered edge loop, Faces for a hovered face
//    ring. Empty (both clear) when there is nothing to preview. The Seed* fields name the hovered component the preview grew
//    from so the viewport can draw a direction hint from it.
struct SelectionPreview
{
    std::unordered_set<uint64_t>  Edges;            // [-] - edge-loop keys a Ctrl/Alt click would select
    std::unordered_set<uint32_t>  Faces;            // [-] - face-ring ordinals a Ctrl/Alt click would select
    bool                          Enabled  = false; // [-] - true when either set is populated
    uint64_t                      SeedEdge = 0;     // [-] - the hovered seed edge (Edge preview)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Drop every set + the anchor (the prototype's selClear). Leaves the state empty.
void ResetSelectionState(SelectionState& State);

// Commit a single-component click in the given mode. When Additive is false the active-mode set is cleared first (a plain
// click replaces); when true the component toggles (Blender Ctrl-click add/remove). Updates LastPick to this component. Face /
// Vertex pass their ordinal in Component; Edge passes its packed key in EdgeComponent (Component ignored).
void ResolveClickSelection(SelectionState& State,
                           SelectionMode   Mode,
                           uint32_t        Component,
                           uint64_t        EdgeComponent,
                           bool            Additive);

// Merge a region-select hit (Box / Lasso / Circle / Paint) into the active-mode set with click-select set semantics: plain
// (neither modifier) clears the active set first then adds every hit (replace); Additive (Shift) adds without clearing; and
// Subtractive (Ctrl) erases every hit from the set. Only the active mode's set is touched (Vertex -> Vertices, Edge -> Edges,
// Face / Island -> Faces); the hit sets come straight from ResolveRegionSelection. LastPick is untouched â€” a region has no
// single anchor. Object mode is a no-op (region select is a component-mode gesture).
void ResolveRegionSelectionMerge(SelectionState&                      State,
                                 SelectionMode                        Mode,
                                 const std::unordered_set<uint32_t>&  HitVertices,
                                 const std::unordered_set<uint64_t>&  HitEdges,
                                 const std::unordered_set<uint32_t>&  HitFaces,
                                 bool                                 Additive,
                                 bool                                 Subtractive);

// Select the whole edge loop grown from SeedEdge (the port of selLoopFromEdge â†’ computeLoopEdges): the vertex-centric
// valence-4 quad rule walked from both endpoints, with a straightness fallback where the topology is irregular. When Additive
// (Shift held) the loop MERGES into the edge set so a second loop-click keeps the first; otherwise it replaces. Faces /
// Vertices always clear (a loop is an edge-mode gesture). Updates LastPick. The cluster supplies positions for the fallback.
void ResolveEdgeLoopSelection(SelectionState&       State,
                              const AdjacencyIndex&  Index,
                              const PolygonCluster&  Cluster,
                              uint64_t               SeedEdge,
                              bool                   Additive);

// Select the face loop / ring grown from SeedFace (the port of selRingFromFace â†’ computeFaceBand): from a quad seed, the
// longer of the two opposite-edge-pair bands walked with the topological quad rule; from a non-quad seed, the geometric
// anti-parallel fallback out every loop edge. When Additive (Shift held) the band MERGES into the face set so a second
// loop-click keeps the first; otherwise it replaces. Edges / Vertices always clear (a ring is a face-mode gesture). Updates
// LastPick. The cluster supplies positions for the geometric fallback.
void ResolveFaceLoopSelection(SelectionState&       State,
                              const AdjacencyIndex&  Index,
                              const PolygonCluster&  Cluster,
                              uint32_t               SeedFace,
                              bool                   Additive);

// The whole UV island (chart) of faces reachable from SeedFace across interior edges that are NOT UV seams: a flood over
// FaceEdgeKeys that stops at a boundary edge (only one incident face) and at any seam edge (an edge whose two incident faces
// disagree on either endpoint's corner UV, epsilon 1e-6, read from FaceCornerTexture). When the cluster carries no per-corner
// UVs the seam test cannot run, so it degrades to a pure connected-component flood (topological island). Returns the face
// ordinals of the island (SeedFace always included). Consumed by both the 3D and UV picks in IslandMode.
std::vector<uint32_t> ResolveIslandFaces(const AdjacencyIndex&  Index,
                                         const PolygonCluster&  Cluster,
                                         uint32_t               SeedFace);

// Select the whole UV island grown from SeedFace (ResolveIslandFaces). When Additive (Shift held) the island MERGES into the
// face set so a second Shift+click keeps the first; otherwise it replaces. Edges / Vertices always clear (an island is a
// face-pool gesture). Updates LastPick. Mirrors ResolveFaceLoopSelection â€” the highlight is automatic in both surfaces.
void ResolveIslandSelection(SelectionState&        State,
                            const AdjacencyIndex&   Index,
                            const PolygonCluster&   Cluster,
                            uint32_t                SeedFace,
                            bool                    Additive);

// Grow the active-mode selection to the connected component(s) reachable from it by shared vertices (the port of selLinked):
// seeds are the vertices of the current selection, the component floods through VertexAdjacency ignoring seams, then every
// vertex / edge / face fully inside the component is added. No-op (returns 0) when nothing is selected. Returns the component
// vertex count.
uint32_t ResolveLinkedSelection(SelectionState& State, const AdjacencyIndex& Index, SelectionMode Mode);

// Select every component of the active mode (the port of selAll): all vertices, all edge keys, or all faces.
void ResolveSelectAll(SelectionState& State, const AdjacencyIndex& Index, SelectionMode Mode);

// Grow the active-mode selection by one ring â€” add every component adjacent to the current selection (the port of selGrow, Blender
// Ctrl+NumpadPlus). Vertex adds one-ring adjacency; Edge adds every loop edge of every face touching a selected edge's endpoints;
// Face / Island / Object add edge-adjacent faces. Snapshots the additions before merging so one call grows exactly one ring.
void ResolveGrowSelection(SelectionState& State, const AdjacencyIndex& Index, SelectionMode Mode);

// Shrink the active-mode selection by one ring â€” remove every boundary component that touches an unselected adjacent (the port of
// selShrink, Blender Ctrl+NumpadMinus). Uses the same adjacency as ResolveGrowSelection so grow-then-shrink round-trips a solid
// interior. Snapshots the boundary before erasing so one call peels exactly one ring, never cascading through the whole set.
void ResolveShrinkSelection(SelectionState& State, const AdjacencyIndex& Index, SelectionMode Mode);

// Compute (without mutating State) the loop / ring a modifier-held click on the hovered component would grab, for a direction
// ghost (the port of buildLoopPreview â†’ loopEdgeSet / computeFaceBand). In Edge mode HoverEdge seeds an edge loop into
// Preview.Edges; in Face mode HoverFace seeds a face band into Preview.Faces; other modes leave Preview empty. Clears Preview
// first; HoverValid false means no hover â†’ empty preview. The unused hover argument is ignored per mode.
void ResolveLoopPreview(SelectionPreview&      Preview,
                        const AdjacencyIndex&   Index,
                        const PolygonCluster&   Cluster,
                        SelectionMode           Mode,
                        bool                    HoverValid,
                        uint64_t                HoverEdge,
                        uint32_t                HoverFace);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_GEOMETRY_SELECTION_COMPONENTSELECTION_H
