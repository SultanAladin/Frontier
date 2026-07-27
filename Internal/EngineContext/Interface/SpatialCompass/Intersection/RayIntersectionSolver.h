/*==============================================================================================================================================
                                                          RAYINTERSECTIONSOLVER.H
==============================================================================================================================================*/
// 🧩 Resolves which face the cursor is over. The projected faces are convex screen quads; the solver point-tests the cursor against each front-
//    facing quad and, among hits, keeps the NEAREST (largest DepthKey) — so the front face wins when two overlap through the glass. This is the
//    C++ equivalent of the mockup relying on the browser to route a click to the topmost `.face`. Header-only; the .cpp calls it once per cycle
//    with the cursor to drive the hover highlight and, on click, the committed view.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_RAYINTERSECTIONSOLVER_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_RAYINTERSECTIONSOLVER_H

#include "IntersectionResult.h"
#include "../Topology/CompassPartition.h"

#include "imgui.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Signed area sign of triangle (A, B, P) — the side of edge A→B the point P lies on. Consistent signs across all four
//    edges mean the point is inside the convex quad.
inline float ResolveEdgeSide(const ImVec2& A, const ImVec2& B, const ImVec2& P)
{
    return (B.x - A.x) * (P.y - A.y) - (B.y - A.y) * (P.x - A.x);
}

// 📝 Point-in-convex-quad test for the projected screen corners [0..3] wound consistently. True when the cursor is inside.
inline bool ResolvePointInsidePatch(const SurfacePatchDefinition& Patch, const ImVec2& Cursor)
{
    float Sign0 = ResolveEdgeSide(Patch.ScreenCorner[0], Patch.ScreenCorner[1], Cursor);
    float Sign1 = ResolveEdgeSide(Patch.ScreenCorner[1], Patch.ScreenCorner[2], Cursor);
    float Sign2 = ResolveEdgeSide(Patch.ScreenCorner[2], Patch.ScreenCorner[3], Cursor);
    float Sign3 = ResolveEdgeSide(Patch.ScreenCorner[3], Patch.ScreenCorner[0], Cursor);

    bool AnyNegative = (Sign0 < 0.0f) || (Sign1 < 0.0f) || (Sign2 < 0.0f) || (Sign3 < 0.0f);
    bool AnyPositive = (Sign0 > 0.0f) || (Sign1 > 0.0f) || (Sign2 > 0.0f) || (Sign3 > 0.0f);
    return !(AnyNegative && AnyPositive);   // all same side (allowing zeros on an edge) => inside
}


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Resolve the front-facing face nearest the viewer under the cursor. Only front-facing quads (FacingViewer) are clickable —
//    a back face read through the glass is never the pick target. Returns the resolved zone + preset; None when the cursor
//    misses every face. Assumes ProjectSurfacePatch has already filled the screen corners + FacingViewer + DepthKey this cycle.
inline void SolveCompassHit(const CompassPartition& Partition,
                            const ImVec2& Cursor,
                            CompassZone& OutZone,
                            AlignmentPreset& OutPreset)
{
    OutZone   = CompassZone::None;
    OutPreset = AlignmentPreset::Count;

    float BestDepth = -1e30f;
    for (int Index = 0; Index < CompassPartition::FaceCount; ++Index)
    {
        const SurfacePatchDefinition& Patch = Partition.Face[Index];
        if (!Patch.FacingViewer)
        {
            continue;
        }
        if (!ResolvePointInsidePatch(Patch, Cursor))
        {
            continue;
        }
        if (Patch.DepthKey > BestDepth)
        {
            BestDepth = Patch.DepthKey;
            OutZone   = CompassZone::Face;
            OutPreset = Patch.Preset;
        }
    }
}

}   // namespace Frontier

#endif
