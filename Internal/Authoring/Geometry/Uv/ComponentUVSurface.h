/*============================================================================================================================================
                                                            COMPONENTUVSURFACE.H
============================================================================================================================================*/
// ðŸ§© The CPU UV-layout projector â€” the 2D counterpart of the 3D ComponentOverlaySurface. Where the 3D overlay reprojects the cluster
//    through a camera and depth-clips against an occlusion buffer, the UV editor has it far simpler: the per-corner UVs ARE 2D
//    positions already, so this module needs only a pan + zoom affine map (no camera, no depth, no occlusion). It walks the
//    cluster's original polygons (triangles / quads / N-gons preserved) and constructs one closed screen polyline per face â€” no
//    fan diagonal, so a quad reads as four edges and an N-gon as N. Selection faces / edges / vertices are highlighted IN PLACE
//    over the full dim layout (the confirmed UX): the whole UV atlas is always drawn, the selection lights up on top.
// ðŸ“ Pure math + data: NO ImGui, NO Vulkan. It consumes a PolygonCluster (its FaceCornerTexture + face slices) + an
//    AdjacencyIndex (for the edge-key encoding shared with the selection sets) and PRODUCES flat screen-space polylines / points
//    the runtime hands to ImGui's draw list. Kept in Authoring (not Rendering) because it needs no GPU state at all â€” unlike the
//    3D overlay, which lives under Rendering only because it borrows the GPU view / projection + stamps a depth field.

#pragma once
#ifndef FRONTIER_AUTHORING_GEOMETRY_UV_COMPONENTUVSURFACE_H
#define FRONTIER_AUTHORING_GEOMETRY_UV_COMPONENTUVSURFACE_H

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Frontier
{

struct PolygonCluster;   // ðŸ“ forward-declared; the .cpp reads its FaceCornerTexture + face slices
struct AdjacencyIndex;   // ðŸ“ forward-declared; the .cpp uses its edge-key encoding to test selected edges

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// ðŸ“ The pan + zoom affine map from UV space (u, v in [0, 1]) to the panel's pixel space, relative to the canvas top-left. The
//    projection is x = CanvasCenterX + (u - PanOffsetU) * ZoomScale; y = CanvasCenterY - (v - PanOffsetV) * ZoomScale â€” the
//    MINUS on v gives the V-up (Blender) convention: v = 0 at the bottom, v = 1 at the top. PanOffset defaults centre the unit
//    square; ZoomScale is pixels per one UV unit. The runtime supplies the live values from the panel's interaction state.
struct UVTransform
{
    float CanvasCenterX = 0.0f;   // [px] - pixel x the UV point PanOffsetU maps to (the pan anchor's screen x)
    float CanvasCenterY = 0.0f;   // [px] - pixel y the UV point PanOffsetV maps to (the pan anchor's screen y)
    float PanOffsetU    = 0.5f;   // [-]  - UV u pinned to CanvasCenterX (0.5 centres the unit square)
    float PanOffsetV    = 0.5f;   // [-]  - UV v pinned to CanvasCenterY (0.5 centres the unit square)
    float ZoomScale     = 300.0f; // [px] - pixels per one UV unit
};

// ðŸ“ One projected UV point in the panel's pixel space (already mapped through UVTransform). The runtime offsets these by the
//    canvas origin before handing them to ImGui.
struct UVScreenPoint
{
    float ScreenX = 0.0f;   // [px] - pixel x within the canvas
    float ScreenY = 0.0f;   // [px] - pixel y within the canvas
};

// ðŸ“ A run of connected pixel points. A face outline is one Closed run over its corner UVs (the loop wraps back to the first
//    point); a highlighted single edge is a two-point open run. No fan diagonals ever enter here â€” the outline is the true
//    polygon loop, so quads / tris / N-gons all read as their original shape.
struct UVPolyline
{
    std::vector<UVScreenPoint> Points;             // [px] - the run's pixel points in order
    bool                       Closed = false;     // [-]  - true = the last point connects back to the first (a face loop)
};

// ðŸ“ One face's filled interior as a convex polygon of pixel points (the selected-face highlight). Constructed for selection
//    faces only; the runtime fills it translucent before stroking the outlines on top. A convex fill is exact for the specimen's
//    quads / tris; a concave N-gon would need triangulation, deferred with the rest of the concave path (the specimen has none).
struct UVFillPolygon
{
    std::vector<UVScreenPoint> Points;   // [px] - the face's corner points in winding order
};

// ðŸ“ One frame's resolved UV output the runtime strokes. The base pass (FaceLoops + VertexDots) is the FULL dim layout, always
//    drawn. The selection passes (SelectionFills / SelectionLoops / SelectionDots) light up on top â€” highlight-in-place, never
//    isolate. Rebuilt each frame from the current transform; a single O(corners) pass over the specimen's ~2000 corners â€” cheap.
struct UVSurfaceOutput
{
    std::vector<UVPolyline>     FaceLoops;         // [-] - one closed outline per face (the base dim layout)
    std::vector<UVScreenPoint>  VertexDots;        // [-] - every corner point (the base vertex overlay, when enabled)
    std::vector<UVFillPolygon>  BaseFills;         // [-] - one convex fill per face (the neutral shell fill, when enabled)

    std::vector<UVFillPolygon>  SelectionFills;    // [-] - filled interiors of the selected faces
    std::vector<UVPolyline>     SelectionLoops;    // [-] - bright outlines of the selected faces + the selected single edges
    std::vector<UVScreenPoint>  SelectionDots;     // [-] - bright dots of the selected vertices (a seam vertex lights up twice)
};

// ðŸ“ What the UV surface constructs this frame. The three overlay gates mirror the 3D overlay's Edge / Vertex passes; the
//    borrowed selection-set pointers are the SAME shapes ComponentSelection produces (face ordinals, packed edge keys, vertex
//    indices). Pointers (not owned) so an empty / detached selection costs nothing â€” nullptr skips that highlight pass. Read-only.
struct UVSurfaceControl
{
    bool  FaceOverlayEnabled   = true;    // [-] - stroke every face's base outline (quads / N-gons show true edges)
    bool  EdgeOverlayEnabled   = true;    // [-] - stroke selected single edges as bright two-point runs
    bool  VertexOverlayEnabled = false;   // [-] - dot every corner point in the base pass
    bool  ShellFillEnabled     = false;   // [-] - construct a neutral convex fill per face (BaseFills) under the outlines

    const std::unordered_set<uint32_t>*  SelectionFaces    = nullptr;   // [-] - selected face ordinals (filled + outlined bright)
    const std::unordered_set<uint64_t>*  SelectionEdges    = nullptr;   // [-] - selected packed edge keys (bright single edges)
    const std::unordered_set<uint32_t>*  SelectionVertices = nullptr;   // [-] - selected vertex indices (bright corner dots)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Project one frame of the UV layout. Walks the cluster's original polygons via the running offset over FaceVertexCounts (the
// same slice idiom as ConstructRenderVertexStream), maps each corner's UV through Transform, and constructs the base face
// outlines + (per Control) the selection fills / outlines / dots. Screen coordinates are relative to the canvas top-left; the
// runtime offsets them by the canvas origin before drawing. Returns false (leaving Result empty) when the inputs are
// inconsistent â€” no per-corner UVs (FaceCornerTexture size != FaceVertexIndices size), or an empty face stream.
bool ResolveUVSurface(const PolygonCluster&     Cluster,
                      const AdjacencyIndex&     Adjacency,
                      const UVTransform&        Transform,
                      const UVSurfaceControl&   Control,
                      UVSurfaceOutput&          Result);

} // namespace Frontier

#endif   // FRONTIER_AUTHORING_GEOMETRY_UV_COMPONENTUVSURFACE_H
