/*==============================================================================================================================================
                                                            GLYPHINSCRIPTION.CPP
==============================================================================================================================================*/
// 🧩 Unit-square glyph paths rasterised into a pixel box through ImDrawList. Stroke width is passed through unscaled by design: a glyph drawn in a
//    22 px row and one drawn in a 48 px palette tile want the SAME hairline weight, not a proportionally fatter one, so weight stays the caller's
//    decision while geometry follows the box.

#include "GlyphInscription.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Glyph space to screen space. One multiply-add per axis; inlined into the run loops below.
    inline ImVec2 ProjectGlyphPoint(GlyphPoint Point, ImVec2 BoxOrigin, float BoxSide)
    {
        return ImVec2(BoxOrigin.x + Point.X * BoxSide, BoxOrigin.y + Point.Y * BoxSide);
    }

    // 📝 Circle segment count from pixel radius, so a small dot does not pay for 32 segments and a large ring does not read as a
    //    polygon. Clamped to a range ImDrawList handles well; 0 would ask ImGui to auto-tessellate, which it does per-radius anyway,
    //    but being explicit keeps the vertex cost predictable across a menu of ~60 glyphs.
    inline int ResolveCircleSegments(float RadiusPixels)
    {
        const int Segments = static_cast<int>(RadiusPixels * 2.0f);
        if (Segments < 8)  { return 8; }
        if (Segments > 24) { return 24; }
        return Segments;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void InscribeGlyph(ImDrawList*           DrawList,
                   const GlyphStrokeSet& Strokes,
                   ImVec2                BoxOrigin,
                   float                 BoxSide,
                   ImU32                 Tint,
                   float                 StrokeWidth)
{
    if (DrawList == nullptr || BoxSide <= 0.0f)
    {
        return;
    }

    for (int RunIndex = 0; RunIndex < Strokes.RunCount; ++RunIndex)
    {
        const GlyphRun& Run = Strokes.Runs[RunIndex];
        if (Run.PointCount < 2 || Run.FirstPoint < 0 || Run.FirstPoint + Run.PointCount > Strokes.PointCount)
        {
            continue;
        }

        // Points are pushed into the draw list's own path buffer rather than a local array: ImDrawList already owns a scratch
        // buffer for exactly this, so a glyph adds no stack footprint regardless of how many points it declares.
        DrawList->PathClear();
        for (int PointIndex = 0; PointIndex < Run.PointCount; ++PointIndex)
        {
            DrawList->PathLineTo(ProjectGlyphPoint(Strokes.Points[Run.FirstPoint + PointIndex], BoxOrigin, BoxSide));
        }

        if (Run.Filled)
        {
            DrawList->PathFillConvex(Tint);
        }
        else
        {
            DrawList->PathStroke(Tint, Run.Closed ? ImDrawFlags_Closed : ImDrawFlags_None, StrokeWidth);
        }
    }

    for (int CircleIndex = 0; CircleIndex < Strokes.CircleCount; ++CircleIndex)
    {
        const GlyphCircle& Circle = Strokes.Circles[CircleIndex];
        const ImVec2 Centre = ProjectGlyphPoint(Circle.Centre, BoxOrigin, BoxSide);
        const float  Radius = Circle.Radius * BoxSide;
        if (Radius <= 0.0f)
        {
            continue;
        }

        const int Segments = ResolveCircleSegments(Radius);
        if (Circle.Filled)
        {
            DrawList->AddCircleFilled(Centre, Radius, Tint, Segments);
        }
        else
        {
            DrawList->AddCircle(Centre, Radius, Tint, Segments, StrokeWidth);
        }
    }
}

bool AppendGlyphRun(GlyphStrokeSet& Strokes, const GlyphPoint* RunPoints, int RunPointCount, bool Closed, bool Filled)
{
    if (RunPoints == nullptr || RunPointCount < 2)
    {
        return false;
    }
    if (Strokes.RunCount >= GlyphRunCapacity || Strokes.PointCount + RunPointCount > GlyphPointCapacity)
    {
        return false;
    }

    GlyphRun& Run = Strokes.Runs[Strokes.RunCount++];
    Run.FirstPoint = Strokes.PointCount;
    Run.PointCount = RunPointCount;
    Run.Closed     = Closed;
    Run.Filled     = Filled;

    for (int PointIndex = 0; PointIndex < RunPointCount; ++PointIndex)
    {
        Strokes.Points[Strokes.PointCount++] = RunPoints[PointIndex];
    }
    return true;
}

bool AppendGlyphCircle(GlyphStrokeSet& Strokes, GlyphPoint Centre, float Radius, bool Filled)
{
    if (Strokes.CircleCount >= GlyphCircleCapacity || Radius <= 0.0f)
    {
        return false;
    }

    GlyphCircle& Circle = Strokes.Circles[Strokes.CircleCount++];
    Circle.Centre = Centre;
    Circle.Radius = Radius;
    Circle.Filled = Filled;
    return true;
}

}   // namespace Frontier
