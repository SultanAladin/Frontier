/*==============================================================================================================================================
                                                          LABELALIGNMENT.H
==============================================================================================================================================*/
// 🧩 In-plane face captions ("FRONT", "TOP", ...). Rather than draw flat screen-space text, each caption is laid out inside its FACE PLANE in cube
//    space and projected through the SAME rig transform as the face corners — so the letters foreshorten, skew, and tilt exactly as if silk-screened
//    on the cube. Each glyph becomes a textured quad (ImGui font atlas) whose four cube-space corners are rotated + perspective-divided, then emitted
//    as an AddImageQuad. Locked-to-face orientation: the text is glued flat to the plane (reads mirrored only when a face turns away, like a real
//    cube), so the .cpp only paints captions on front-facing faces. Header-only; the .cpp calls ConstructInPlaneFaceLabel once per visible face.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_LABELALIGNMENT_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_LABELALIGNMENT_H

#include "../Topology/SurfacePatchDefinition.h"
#include "CoordinateProjection.h"

#include "imgui.h"

#include <math.h>
#include <string.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The projected centroid of a face's four screen corners — kept for callers that anchor to a face centre.
inline ImVec2 ResolvePatchCentroid(const SurfacePatchDefinition& Patch)
{
    return ImVec2((Patch.ScreenCorner[0].x + Patch.ScreenCorner[1].x + Patch.ScreenCorner[2].x + Patch.ScreenCorner[3].x) * 0.25f,
                  (Patch.ScreenCorner[0].y + Patch.ScreenCorner[1].y + Patch.ScreenCorner[2].y + Patch.ScreenCorner[3].y) * 0.25f);
}

// 📝 Top-left position that centres a measured text block on an anchor point (screen-space fallback).
inline ImVec2 ResolveCentredLabelOrigin(const ImVec2& Anchor, const ImVec2& TextSize)
{
    return ImVec2(Anchor.x - TextSize.x * 0.5f, Anchor.y - TextSize.y * 0.5f);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The face centre in cube space (mean of the four corners).
inline CubeSpacePoint ResolveFaceCentre(const SurfacePatchDefinition& Patch)
{
    return CubeSpacePoint{
        (Patch.Corner[0].X + Patch.Corner[1].X + Patch.Corner[2].X + Patch.Corner[3].X) * 0.25f,
        (Patch.Corner[0].Y + Patch.Corner[1].Y + Patch.Corner[2].Y + Patch.Corner[3].Y) * 0.25f,
        (Patch.Corner[0].Z + Patch.Corner[1].Z + Patch.Corner[2].Z + Patch.Corner[3].Z) * 0.25f };
}

// 📝 Project one cube-space point through the live rig (rotate + perspective divide), matching the face-corner projection.
inline ImVec2 ProjectLabelPoint(const CubeSpacePoint& Point, const ProjectionContext& Context)
{
    const CubeSpacePoint Rotated = RotateCubePoint(Point, Context.ElevationDeg, Context.AzimuthDeg);
    float Depth = 0.0f;
    return ProjectCubePoint(Rotated, Context, Depth);
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      IN-PLANE FACE LABEL
//------------------------------------------------------------------------------------------------------------------------

// 📝 Paint a caption INSIDE the face plane so it foreshortens with the cube. The face's two in-plane axes are taken straight
//    from its corner winding: U (rightward) = Corner1 - Corner0, V (upward) = Corner3 - Corner0. The string is laid out along U,
//    centred, with each glyph a quad in cube space sized in the same units as the cube half-extent. Every glyph quad's four
//    corners are projected through the rig and emitted as a textured AddImageQuad from the font atlas — perspective-correct,
//    locked flat to the face. Caller gates this to front-facing faces (a back caption would read mirrored through the glass).
inline void ConstructInPlaneFaceLabel(ImDrawList*                   DrawList,
                                      const SurfacePatchDefinition& Patch,
                                      const ProjectionContext&      Context,
                                      const char*                   Text,
                                      float                         CubeHalfExtent,
                                      ImU32                         Colour)
{
    if (DrawList == nullptr || Text == nullptr || Text[0] == '\0')
    {
        return;
    }

    // ImGui 1.92+: glyphs + metrics live on the size-baked font (ImFontBaked); the atlas binds via a ImTextureRef.
    ImFontBaked* Font = ImGui::GetFontBaked();
    if (Font == nullptr)
    {
        return;
    }
    const ImTextureRef Atlas = ImGui::GetIO().Fonts->TexRef;

    // Face frame in cube space: origin at Corner0, U across (Corner0->Corner1), V up (Corner0->Corner3). These already carry
    // the face's real orientation + winding, so text laid out in (U, V) sits flat on the plane the same way for every face.
    const CubeSpacePoint& C0 = Patch.Corner[0];
    const CubeSpacePoint  U  { Patch.Corner[1].X - C0.X, Patch.Corner[1].Y - C0.Y, Patch.Corner[1].Z - C0.Z };
    const CubeSpacePoint  V  { Patch.Corner[3].X - C0.X, Patch.Corner[3].Y - C0.Y, Patch.Corner[3].Z - C0.Z };

    // Normalise U/V to unit cube-space vectors (each edge spans 2*HalfExtent).
    const float EdgeLength = 2.0f * CubeHalfExtent;
    const float InvEdge    = (EdgeLength > 1e-4f) ? (1.0f / EdgeLength) : 0.0f;
    const CubeSpacePoint UHat{ U.X * InvEdge, U.Y * InvEdge, U.Z * InvEdge };
    const CubeSpacePoint VHat{ V.X * InvEdge, V.Y * InvEdge, V.Z * InvEdge };
    const CubeSpacePoint Centre = ResolveFaceCentre(Patch);

    // Cap height in cube-space units — a fraction of the face so the longest label ("BOTTOM") fits across the face width.
    const float CapHeight  = CubeHalfExtent * 0.42f;          // [cube-units] - glyph cell height on the face
    const float FontScale  = CapHeight / Font->Size;           // atlas px -> cube-space units

    // Measure the string extents (in cube-space units): total advance width for horizontal centring, and the tightest
    // vertical glyph box (min Y0 / max Y1 across all glyphs) so the block centres on its true visual middle, not the baseline.
    float TotalWidth = 0.0f;
    float MinY0      =  1e9f;    // [atlas px] - highest glyph top (smallest Y0, font Y grows down)
    float MaxY1      = -1e9f;    // [atlas px] - lowest glyph bottom (largest Y1)
    for (const char* Scan = Text; *Scan != '\0'; ++Scan)
    {
        const ImFontGlyph* Glyph = Font->FindGlyph((ImWchar)(unsigned char)*Scan);
        if (Glyph == nullptr) { continue; }
        TotalWidth += Glyph->AdvanceX * FontScale;
        if (Glyph->Y0 < MinY0) { MinY0 = Glyph->Y0; }
        if (Glyph->Y1 > MaxY1) { MaxY1 = Glyph->Y1; }
    }
    if (MaxY1 < MinY0) { MinY0 = 0.0f; MaxY1 = 0.0f; }         // empty / all-space string guard

    // Pen starts half a width left of centre. The baseline is placed so the MIDPOINT of the glyph box (MinY0..MaxY1) lands
    // exactly on the face centre: baseline + (MinY0 + MaxY1)/2 * scale == 0  ->  baseline = -(MinY0 + MaxY1)/2 * scale.
    float PenU = -TotalWidth * 0.5f;
    const float BaselineV = ((MinY0 + MaxY1) * 0.5f) * FontScale;   // in face-V units (V grows up, font Y grows down -> +)

    for (const char* Scan = Text; *Scan != '\0'; ++Scan)
    {
        const ImFontGlyph* Glyph = Font->FindGlyph((ImWchar)(unsigned char)*Scan);
        if (Glyph == nullptr) { continue; }

        // Glyph box on the face, in cube-space (U,V) offsets from the face centre. Font Y grows DOWN, face V grows UP, so
        // the vertical offsets are negated. X0/Y0/X1/Y1 are atlas-space pixels relative to the pen baseline.
        const float GX0 = PenU + Glyph->X0 * FontScale;
        const float GX1 = PenU + Glyph->X1 * FontScale;
        const float GV0 = BaselineV - Glyph->Y0 * FontScale;   // top edge (Y0 < Y1 in font space)
        const float GV1 = BaselineV - Glyph->Y1 * FontScale;   // bottom edge

        // Four glyph-quad corners in cube space: Centre + u*UHat + v*VHat.
        auto PlaceOnFace = [&](float u, float v) -> CubeSpacePoint
        {
            return CubeSpacePoint{ Centre.X + UHat.X * u + VHat.X * v,
                                   Centre.Y + UHat.Y * u + VHat.Y * v,
                                   Centre.Z + UHat.Z * u + VHat.Z * v };
        };

        const ImVec2 P0 = ProjectLabelPoint(PlaceOnFace(GX0, GV0), Context);   // top-left
        const ImVec2 P1 = ProjectLabelPoint(PlaceOnFace(GX1, GV0), Context);   // top-right
        const ImVec2 P2 = ProjectLabelPoint(PlaceOnFace(GX1, GV1), Context);   // bottom-right
        const ImVec2 P3 = ProjectLabelPoint(PlaceOnFace(GX0, GV1), Context);   // bottom-left

        // AddImageQuad winds p0->p1->p2->p3 with matching UVs; the glyph atlas UVs follow the same corner order.
        DrawList->AddImageQuad(Atlas, P0, P1, P2, P3,
                               ImVec2(Glyph->U0, Glyph->V0), ImVec2(Glyph->U1, Glyph->V0),
                               ImVec2(Glyph->U1, Glyph->V1), ImVec2(Glyph->U0, Glyph->V1),
                               Colour);

        PenU += Glyph->AdvanceX * FontScale;
    }
}

}   // namespace Frontier

#endif
