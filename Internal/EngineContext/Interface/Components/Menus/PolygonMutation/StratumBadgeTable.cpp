/*==============================================================================================================================================
                                                           STRATUMBADGETABLE.CPP
==============================================================================================================================================*/
// 🧩 The seven stratum pictograms, transcribed from the prototype's BADGE table. Authored in the same 0-24 space the source SVGs use rather than
//    normalised to 0-1: the paths were drawn against that grid, and rescaling them by hand is how a transcription silently drifts. One Project()
//    call maps 0-24 to the pixel box, so the numbers below can be compared against the mockup line for line.

#include <cmath>

#include "StratumBadgeTable.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The badge palette, from the prototype's `C` constant. Deliberately NOT theme palette entries: a badge is fixed artwork
    //    drawn on its own black tile, so it stays legible under any panel palette instead of shifting with it.
    constexpr ImU32 BadgeTile   = IM_COL32(  0,   0,   0, 255);   // [-] - The rounded tile the artwork sits on
    constexpr ImU32 BadgeInk    = IM_COL32(232, 232, 240, 255);   // [-] - C.ink  — topmost detail
    constexpr ImU32 BadgeDim    = IM_COL32(138, 138, 153, 255);   // [-] - C.dim  — supporting form
    constexpr ImU32 BadgeBlue   = IM_COL32( 91, 140, 255, 255);   // [-] - C.blue — the form the stratum names
    constexpr ImU32 BadgeSky    = IM_COL32(126, 200, 255, 255);   // [-] - C.sky  — highlight on that form
    constexpr ImU32 BadgeFill   = IM_COL32( 91, 140, 255, 170);   // [-] - C.blue at aa — filled face body
    constexpr ImU32 BadgeWash   = IM_COL32( 91, 140, 255,  51);   // [-] - C.blue at 33 — vertex halo

    constexpr float BadgeSpan     = 24.0f;    // [-]  - The authoring grid the paths below are drawn against
    constexpr float TileRounding  = 5.0f;     // [-]  - Tile corner radius in authoring units (the SVG's rx)
    constexpr int   ArcSegments   = 20;       // [-]  - Segments per traced arc; smooth at badge sizes

    // 📝 One badge under construction. Carrying the box in a small state object keeps every shape helper free of four repeated
    //    parameters, which is what makes the transcriptions below readable next to the source.
    struct BadgeCanvas
    {
        ImDrawList* DrawList;       // [-]  - Target list
        ImVec2      Origin;         // [px] - Top-left of the badge box
        float       Scale;          // [-]  - Pixels per authoring unit

        [[nodiscard]] ImVec2 Project(float X, float Y) const
        {
            return ImVec2(Origin.x + X * Scale, Origin.y + Y * Scale);
        }

        [[nodiscard]] float Stroke(float Weight) const
        {
            // A hairline still has to land on a pixel, or a 1.1-weight detail line vanishes at small badge sizes.
            const float Scaled = Weight * Scale;
            return Scaled < 1.0f ? 1.0f : Scaled;
        }
    };

    void AddSegment(const BadgeCanvas& Canvas, float FromX, float FromY, float ToX, float ToY, ImU32 Tint, float Weight)
    {
        Canvas.DrawList->AddLine(Canvas.Project(FromX, FromY), Canvas.Project(ToX, ToY), Tint, Canvas.Stroke(Weight));
    }

    void AddDisc(const BadgeCanvas& Canvas, float CentreX, float CentreY, float Radius, ImU32 Tint)
    {
        Canvas.DrawList->AddCircleFilled(Canvas.Project(CentreX, CentreY), Radius * Canvas.Scale, Tint);
    }

    void AddRing(const BadgeCanvas& Canvas, float CentreX, float CentreY, float Radius, ImU32 Tint, float Weight)
    {
        Canvas.DrawList->AddCircle(Canvas.Project(CentreX, CentreY), Radius * Canvas.Scale, Tint, 0, Canvas.Stroke(Weight));
    }

    // 📝 Stroke an open polyline through authored points.
    void AddPath(const BadgeCanvas& Canvas, const ImVec2* Points, int PointCount, ImU32 Tint, float Weight)
    {
        for (int Index = 0; Index < PointCount; ++Index)
        {
            Canvas.DrawList->PathLineTo(Canvas.Project(Points[Index].x, Points[Index].y));
        }
        Canvas.DrawList->PathStroke(Tint, ImDrawFlags_None, Canvas.Stroke(Weight));
    }

    // 📝 Fill a closed polygon through authored points.
    void AddPolygon(const BadgeCanvas& Canvas, const ImVec2* Points, int PointCount, ImU32 Tint)
    {
        for (int Index = 0; Index < PointCount; ++Index)
        {
            Canvas.DrawList->PathLineTo(Canvas.Project(Points[Index].x, Points[Index].y));
        }
        Canvas.DrawList->PathFillConvex(Tint);
    }

    // 📝 Outline a closed polygon through authored points.
    void AddOutline(const BadgeCanvas& Canvas, const ImVec2* Points, int PointCount, ImU32 Tint, float Weight)
    {
        for (int Index = 0; Index < PointCount; ++Index)
        {
            Canvas.DrawList->PathLineTo(Canvas.Project(Points[Index].x, Points[Index].y));
        }
        Canvas.DrawList->PathStroke(Tint, ImDrawFlags_Closed, Canvas.Stroke(Weight));
    }

    // 📝 Trace an elliptical arc, which is what the source paths' `a` commands amount to. Given as centre + radii + angle sweep
    //    rather than as SVG endpoint-arc parameters: the seven arcs here are all half-turns of a known centre, so solving the
    //    endpoint form would add a page of arithmetic to reproduce shapes that are already describable directly.
    void AddArc(const BadgeCanvas& Canvas, float CentreX, float CentreY, float RadiusX, float RadiusY,
                float FromTurn, float ToTurn, ImU32 Tint, float Weight)
    {
        constexpr float Tau = 6.28318530718f;
        for (int Step = 0; Step <= ArcSegments; ++Step)
        {
            const float Fraction = static_cast<float>(Step) / static_cast<float>(ArcSegments);
            const float Angle    = (FromTurn + (ToTurn - FromTurn) * Fraction) * Tau;
            Canvas.DrawList->PathLineTo(Canvas.Project(CentreX + std::cos(Angle) * RadiusX,
                                                       CentreY + std::sin(Angle) * RadiusY));
        }
        Canvas.DrawList->PathStroke(Tint, ImDrawFlags_None, Canvas.Stroke(Weight));
    }

    //-------------------------------------------------------- THE SEVEN BADGES --------------------------------------------------------

    // 📝 Vertex — a corner of a wireframe with one vertex lit and haloed. The halo is drawn first so the lit dot sits over it.
    void InscribeVertexBadge(const BadgeCanvas& Canvas)
    {
        AddSegment(Canvas, 4.0f, 20.0f, 20.0f, 20.0f, BadgeDim, 1.4f);
        AddSegment(Canvas, 4.0f, 20.0f, 18.0f,  6.0f, BadgeDim, 1.4f);
        AddDisc(Canvas, 12.0f, 12.0f, 4.6f, BadgeWash);
        AddRing(Canvas, 12.0f, 12.0f, 3.4f, BadgeBlue, 1.6f);
    }

    // 📝 Edge — one lit segment between two endpoint dots.
    void InscribeEdgeBadge(const BadgeCanvas& Canvas)
    {
        AddSegment(Canvas, 4.0f, 20.0f, 20.0f, 4.0f, BadgeBlue, 2.4f);
        AddDisc(Canvas,  4.0f, 20.0f, 2.4f, BadgeSky);
        AddDisc(Canvas, 20.0f,  4.0f, 2.4f, BadgeSky);
    }

    // 📝 Face — a filled quad in perspective with its four corners marked.
    void InscribeFaceBadge(const BadgeCanvas& Canvas)
    {
        const ImVec2 Quad[4] = { { 4.0f, 8.0f }, { 12.0f, 4.0f }, { 20.0f, 8.0f }, { 12.0f, 12.0f } };
        AddPolygon(Canvas, Quad, 4, IM_COL32(91, 140, 255, 204));
        AddOutline(Canvas, Quad, 4, BadgeSky, 1.4f);
        AddDisc(Canvas,  4.0f,  8.0f, 1.6f, BadgeInk);
        AddDisc(Canvas, 20.0f,  8.0f, 1.6f, BadgeInk);
        AddDisc(Canvas, 12.0f, 12.0f, 1.6f, BadgeInk);
        AddDisc(Canvas, 12.0f,  4.0f, 1.6f, BadgeInk);
    }

    // 📝 Edge Loop — a full circuit around a tube, its near half lit and its far half dim, so the loop reads as closed.
    void InscribeEdgeLoopBadge(const BadgeCanvas& Canvas)
    {
        AddArc(Canvas, 12.0f, 12.0f, 8.0f, 8.0f, 0.5f, 1.0f, BadgeBlue, 2.2f);
        AddArc(Canvas, 12.0f, 12.0f, 8.0f, 8.0f, 0.5f, 0.0f, BadgeSky,  1.6f);
        AddDisc(Canvas,  4.0f, 12.0f, 2.0f, BadgeInk);
        AddDisc(Canvas, 20.0f, 12.0f, 2.0f, BadgeInk);
    }

    // 📝 Edge Ring — the lit ellipse crossing a tube, with the tube's dim silhouette behind it.
    void InscribeEdgeRingBadge(const BadgeCanvas& Canvas)
    {
        AddArc(Canvas, 12.0f, 8.0f, 8.0f, 4.0f, 0.0f, 1.0f, BadgeBlue, 2.2f);

        const ImVec2 Body[4] = { { 4.0f, 8.0f }, { 4.0f, 16.0f }, { 20.0f, 16.0f }, { 20.0f, 8.0f } };
        AddPath(Canvas, Body, 4, BadgeDim, 1.4f);
    }

    // 📝 Border — an OPEN boundary: the lit arc stops at two capped ends rather than closing, which is the whole distinction
    //    from Edge Loop and the reason Border is a first-class stratum.
    void InscribeBorderBadge(const BadgeCanvas& Canvas)
    {
        AddArc(Canvas, 12.0f, 10.6f, 8.0f, 3.4f, 0.5f, 1.0f, BadgeBlue, 2.4f);

        const ImVec2 Body[4] = { { 4.0f, 8.0f }, { 4.0f, 16.0f }, { 20.0f, 16.0f }, { 20.0f, 8.0f } };
        AddPath(Canvas, Body, 4, BadgeDim, 1.3f);

        AddDisc(Canvas,  4.0f, 8.0f, 2.2f, BadgeSky);
        AddDisc(Canvas, 20.0f, 8.0f, 2.2f, BadgeSky);
    }

    // 📝 Object — a whole solid: filled cube body, lit silhouette, and the three interior edges meeting at the near corner.
    void InscribeObjectBadge(const BadgeCanvas& Canvas)
    {
        const ImVec2 Body[6] =
        {
            { 12.0f, 3.0f }, { 20.0f, 8.0f }, { 20.0f, 16.0f }, { 12.0f, 21.0f }, { 4.0f, 16.0f }, { 4.0f, 8.0f }
        };
        AddPolygon(Canvas, Body, 6, BadgeFill);
        AddOutline(Canvas, Body, 6, BadgeSky, 1.5f);

        const ImVec2 Spokes[3] = { { 4.0f, 8.0f }, { 12.0f, 13.0f }, { 20.0f, 8.0f } };
        AddPath(Canvas, Spokes, 3, BadgeInk, 1.1f);
        AddSegment(Canvas, 12.0f, 13.0f, 12.0f, 21.0f, BadgeInk, 1.1f);
    }

    // 📝 Nothing — an empty selection. The dashed square is drawn DIM and open, with a lit plus at its centre: the dashes say
    //    "nothing is here", and the plus says the only thing on offer is putting something here. No blue form, because every other
    //    badge uses blue for the thing the stratum names and this stratum names the absence of one.
    void InscribeNothingBadge(const BadgeCanvas& Canvas)
    {
        // Eight dashes rather than a traced rectangle: a dashed outline is the standard "empty slot" idiom, and AddRect cannot
        // break its own stroke.
        constexpr float Near = 5.0f;
        constexpr float Far  = 19.0f;
        constexpr float Stop = 9.0f;
        constexpr float Ramp = 15.0f;

        AddSegment(Canvas, Near, Near, Stop, Near, BadgeDim, 1.3f);
        AddSegment(Canvas, Ramp, Near, Far,  Near, BadgeDim, 1.3f);
        AddSegment(Canvas, Near, Far,  Stop, Far,  BadgeDim, 1.3f);
        AddSegment(Canvas, Ramp, Far,  Far,  Far,  BadgeDim, 1.3f);
        AddSegment(Canvas, Near, Near, Near, Stop, BadgeDim, 1.3f);
        AddSegment(Canvas, Near, Ramp, Near, Far,  BadgeDim, 1.3f);
        AddSegment(Canvas, Far,  Near, Far,  Stop, BadgeDim, 1.3f);
        AddSegment(Canvas, Far,  Ramp, Far,  Far,  BadgeDim, 1.3f);

        AddSegment(Canvas, 12.0f, 8.5f, 12.0f, 15.5f, BadgeSky, 2.0f);
        AddSegment(Canvas,  8.5f, 12.0f, 15.5f, 12.0f, BadgeSky, 2.0f);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InscribeStratumBadge(ImDrawList* DrawList, TopologyStratum Stratum, ImVec2 BoxOrigin, float BoxSide)
{
    if (DrawList == nullptr || BoxSide <= 0.0f)
    {
        return;
    }

    const BadgeCanvas Canvas{ DrawList, BoxOrigin, BoxSide / BadgeSpan };

    // The tile first: the artwork is authored against black, and the two-tone contrast collapses without it.
    DrawList->AddRectFilled(BoxOrigin, ImVec2(BoxOrigin.x + BoxSide, BoxOrigin.y + BoxSide),
                            BadgeTile, TileRounding * Canvas.Scale);

    switch (Stratum)
    {
    case TopologyStratum::Vertex:   InscribeVertexBadge(Canvas);   break;
    case TopologyStratum::Edge:     InscribeEdgeBadge(Canvas);     break;
    case TopologyStratum::Face:     InscribeFaceBadge(Canvas);     break;
    case TopologyStratum::EdgeLoop: InscribeEdgeLoopBadge(Canvas); break;
    case TopologyStratum::EdgeRing: InscribeEdgeRingBadge(Canvas); break;
    case TopologyStratum::Border:   InscribeBorderBadge(Canvas);   break;
    case TopologyStratum::Object:   InscribeObjectBadge(Canvas);   break;
    case TopologyStratum::Nothing:  InscribeNothingBadge(Canvas);  break;
    default:                                                       break;
    }
}

}   // namespace Frontier
