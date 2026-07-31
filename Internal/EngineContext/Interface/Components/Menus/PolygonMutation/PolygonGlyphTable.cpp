/*==============================================================================================================================================
                                                          POLYGONGLYPHTABLE.CPP
==============================================================================================================================================*/
// 🧩 The stroke geometry for every PolygonGlyph, authored in a 0-1 unit square. Each glyph is built on demand by a small function rather than held
//    in one giant constant array: a GlyphStrokeSet is ~600 bytes of fixed-capacity storage, so 78 of them resident would cost ~47 KB of static data
//    to serve the handful of glyphs one open menu actually draws. Building costs a few dozen float writes — far below the cost of the draw itself.

#include <cmath>

#include "PolygonGlyphTable.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Glyphs are authored inside this inset rather than the full 0-1 square, so a stroke of any weight stays clear of the box
    //    edge and adjacent rows never appear to touch. Matches the 24×24 source glyphs' ~3 px padding.
    constexpr float GlyphLow  = 0.16f;      // [0-1] - Near edge of the drawable area
    constexpr float GlyphHigh = 0.84f;      // [0-1] - Far edge
    constexpr float GlyphMid  = 0.50f;      // [0-1] - Centre

    using Point = GlyphPoint;

    // 📝 Shorthand builders. Each returns the set by value; the compiler elides the copy, and a glyph definition below reads as
    //    a list of shapes rather than a list of array writes.
    void AddRun(GlyphStrokeSet& Strokes, const Point* Points, int PointCount, bool Closed = false, bool Filled = false)
    {
        AppendGlyphRun(Strokes, Points, PointCount, Closed, Filled);
    }

    void AddLine(GlyphStrokeSet& Strokes, float FromX, float FromY, float ToX, float ToY)
    {
        const Point Segment[2] = { { FromX, FromY }, { ToX, ToY } };
        AppendGlyphRun(Strokes, Segment, 2, false, false);
    }

    void AddBox(GlyphStrokeSet& Strokes, float LeftX, float TopY, float RightX, float BottomY)
    {
        const Point Corners[4] = { { LeftX, TopY }, { RightX, TopY }, { RightX, BottomY }, { LeftX, BottomY } };
        AppendGlyphRun(Strokes, Corners, 4, true, false);
    }

    void AddDot(GlyphStrokeSet& Strokes, float CentreX, float CentreY, float Radius = 0.075f)
    {
        AppendGlyphCircle(Strokes, Point{ CentreX, CentreY }, Radius, true);
    }

    void AddRing(GlyphStrokeSet& Strokes, float CentreX, float CentreY, float Radius)
    {
        AppendGlyphCircle(Strokes, Point{ CentreX, CentreY }, Radius, false);
    }

    // 📝 A solid triangular arrowhead at (TipX, TipY) pointing along the given unit direction. Filled, so it reads as a head
    //    rather than a chevron — the source glyphs use solid heads for every directional cue.
    void AddArrowHead(GlyphStrokeSet& Strokes, float TipX, float TipY, float DirectionX, float DirectionY, float Size = 0.11f)
    {
        // Perpendicular gives the two base corners without a trig call.
        const float BackX = TipX - DirectionX * Size;
        const float BackY = TipY - DirectionY * Size;
        const float SideX = -DirectionY * Size * 0.55f;
        const float SideY =  DirectionX * Size * 0.55f;

        const Point Head[3] =
        {
            { TipX, TipY },
            { BackX + SideX, BackY + SideY },
            { BackX - SideX, BackY - SideY }
        };
        AppendGlyphRun(Strokes, Head, 3, true, true);
    }

    // 📝 An arc approximated by a fixed run of chords. Angles in turns (0-1) rather than radians so the callers below read as
    //    fractions of a circle, and so no <cmath> constant is needed beyond the sin/cos themselves.
    void AddArc(GlyphStrokeSet& Strokes, float CentreX, float CentreY, float Radius, float StartTurn, float EndTurn, int Steps = 10)
    {
        if (Steps < 2 || Steps > 16) { Steps = 10; }

        Point Chord[17] = {};
        for (int Index = 0; Index < Steps; ++Index)
        {
            const float Fraction = static_cast<float>(Index) / static_cast<float>(Steps - 1);
            const float Turn     = StartTurn + (EndTurn - StartTurn) * Fraction;
            const float Radians  = Turn * 6.28318530718f;

            // std::cos/sin rather than ImGui's ImCos/ImSin — those live in imgui_internal.h, and pulling the internal header
            // into a component for two calls would couple this unit to ImGui's private surface for no gain.
            Chord[Index].X = CentreX + std::cos(Radians) * Radius;
            Chord[Index].Y = CentreY + std::sin(Radians) * Radius;
        }
        AppendGlyphRun(Strokes, Chord, Steps, false, false);
    }

    // 📝 An ELLIPTICAL arc, which the circular AddArc above cannot express. The creation primitives need it throughout: a cylinder
    //    cap, a sphere's latitude line and a torus rim are all circles seen in perspective, and drawing them as true circles would
    //    flatten the depth cue that makes the solid readable at 20 px.
    void AddEllipticArc(GlyphStrokeSet& Strokes, float CentreX, float CentreY, float RadiusX, float RadiusY,
                        float StartTurn, float EndTurn, bool Closed = false, int Steps = 12)
    {
        if (Steps < 2 || Steps > 16) { Steps = 12; }

        Point Chord[17] = {};
        for (int Index = 0; Index < Steps; ++Index)
        {
            const float Fraction = static_cast<float>(Index) / static_cast<float>(Steps - 1);
            const float Turn     = StartTurn + (EndTurn - StartTurn) * Fraction;
            const float Radians  = Turn * 6.28318530718f;

            Chord[Index].X = CentreX + std::cos(Radians) * RadiusX;
            Chord[Index].Y = CentreY + std::sin(Radians) * RadiusY;
        }
        AppendGlyphRun(Strokes, Chord, Steps, Closed, false);
    }

    // 📝 A short zigzag between two points, used by the "irregular becomes regular" glyphs (Straighten, Relax, Remesh).
    void AddZigzag(GlyphStrokeSet& Strokes, float FromX, float FromY, float ToX, float ToY, float Amplitude, int Steps = 5)
    {
        if (Steps < 3 || Steps > 9) { Steps = 5; }

        Point Wave[9] = {};
        for (int Index = 0; Index < Steps; ++Index)
        {
            const float Fraction = static_cast<float>(Index) / static_cast<float>(Steps - 1);
            const float Sway     = ((Index % 2) == 0) ? -Amplitude : Amplitude;
            Wave[Index].X = FromX + (ToX - FromX) * Fraction;
            Wave[Index].Y = FromY + (ToY - FromY) * Fraction + ((Index == 0 || Index == Steps - 1) ? 0.0f : Sway);
        }
        AppendGlyphRun(Strokes, Wave, Steps, false, false);
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

GlyphStrokeSet ResolvePolygonGlyph(PolygonGlyph Glyph)
{
    GlyphStrokeSet Strokes;

    switch (Glyph)
    {
    //---------------------------------------------------------- TRANSFORM ----------------------------------------------------------
    case PolygonGlyph::TranslateArrows:
        AddLine(Strokes, GlyphLow, GlyphMid, GlyphHigh, GlyphMid);
        AddLine(Strokes, GlyphMid, GlyphLow, GlyphMid, GlyphHigh);
        AddArrowHead(Strokes, GlyphHigh, GlyphMid,  1.0f,  0.0f);
        AddArrowHead(Strokes, GlyphLow,  GlyphMid, -1.0f,  0.0f);
        AddArrowHead(Strokes, GlyphMid,  GlyphLow,  0.0f, -1.0f);
        AddArrowHead(Strokes, GlyphMid,  GlyphHigh, 0.0f,  1.0f);
        break;

    case PolygonGlyph::RotateArc:
        AddArc(Strokes, GlyphMid, GlyphMid, 0.30f, 0.60f, 0.15f);
        AddArrowHead(Strokes, GlyphMid + 0.30f, GlyphMid, 0.0f, 1.0f);
        AddDot(Strokes, GlyphMid, GlyphMid, 0.045f);
        break;

    case PolygonGlyph::ScaleCorner:
        AddBox(Strokes, GlyphLow, GlyphLow, 0.62f, 0.62f);
        AddLine(Strokes, 0.62f, 0.62f, GlyphHigh, GlyphHigh);
        AddArrowHead(Strokes, GlyphHigh, GlyphHigh, 0.707f, 0.707f);
        AddDot(Strokes, GlyphLow, GlyphLow, 0.05f);
        break;

    case PolygonGlyph::SlideTrack:
        AddLine(Strokes, GlyphLow, 0.62f, GlyphHigh, 0.62f);
        AddDot(Strokes, 0.42f, 0.62f);
        AddLine(Strokes, 0.42f, 0.36f, 0.68f, 0.36f);
        AddArrowHead(Strokes, 0.68f, 0.36f, 1.0f, 0.0f, 0.09f);
        break;

    case PolygonGlyph::FlattenPlane:
        AddLine(Strokes, GlyphLow, 0.70f, GlyphHigh, 0.70f);
        AddDot(Strokes, 0.30f, 0.28f, 0.055f);
        AddDot(Strokes, GlyphMid, 0.22f, 0.055f);
        AddDot(Strokes, 0.70f, 0.34f, 0.055f);
        AddArrowHead(Strokes, GlyphMid, 0.60f, 0.0f, 1.0f, 0.10f);
        break;

    case PolygonGlyph::AxisMarker:
        AddLine(Strokes, GlyphLow, GlyphHigh, GlyphHigh, GlyphHigh);
        AddLine(Strokes, GlyphLow, GlyphHigh, GlyphLow, GlyphLow);
        AddLine(Strokes, GlyphLow, GlyphHigh, 0.62f, 0.42f);
        AddDot(Strokes, GlyphLow, GlyphHigh, 0.05f);
        break;

    case PolygonGlyph::CircleFit:
        AddBox(Strokes, 0.22f, 0.22f, 0.78f, 0.78f);
        AddRing(Strokes, GlyphMid, GlyphMid, 0.28f);
        break;

    case PolygonGlyph::SpanEven:
        AddLine(Strokes, GlyphLow, GlyphMid, GlyphHigh, GlyphMid);
        AddDot(Strokes, GlyphLow, GlyphMid, 0.055f);
        AddDot(Strokes, 0.39f, GlyphMid, 0.055f);
        AddDot(Strokes, 0.61f, GlyphMid, 0.055f);
        AddDot(Strokes, GlyphHigh, GlyphMid, 0.055f);
        break;

    case PolygonGlyph::StraightenLine:
        AddZigzag(Strokes, GlyphLow, 0.32f, GlyphHigh, 0.32f, 0.12f);
        AddLine(Strokes, GlyphLow, 0.72f, GlyphHigh, 0.72f);
        AddArrowHead(Strokes, GlyphMid, 0.60f, 0.0f, 1.0f, 0.09f);
        break;

    //------------------------------------------------------- EXTRUDE / BUILD -------------------------------------------------------
    case PolygonGlyph::ExtrudeOut:
        AddBox(Strokes, 0.22f, 0.60f, 0.78f, GlyphHigh);
        AddBox(Strokes, 0.22f, 0.26f, 0.78f, 0.50f);
        AddLine(Strokes, GlyphMid, 0.58f, GlyphMid, 0.40f);
        AddArrowHead(Strokes, GlyphMid, 0.36f, 0.0f, -1.0f, 0.10f);
        break;

    case PolygonGlyph::ExtrudeSpike:
        AddLine(Strokes, 0.24f, GlyphHigh, GlyphMid, 0.30f);
        AddLine(Strokes, 0.76f, GlyphHigh, GlyphMid, 0.30f);
        AddLine(Strokes, 0.24f, GlyphHigh, 0.76f, GlyphHigh);
        AddDot(Strokes, GlyphMid, 0.30f, 0.06f);
        break;

    case PolygonGlyph::InsetRing:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddBox(Strokes, 0.33f, 0.33f, 0.67f, 0.67f);
        break;

    case PolygonGlyph::OutlineRing:
        AddBox(Strokes, 0.32f, 0.32f, 0.68f, 0.68f);
        AddBox(Strokes, GlyphLow - 0.04f, GlyphLow - 0.04f, GlyphHigh + 0.04f, GlyphHigh + 0.04f);
        AddArrowHead(Strokes, 0.90f, 0.10f, 0.707f, -0.707f, 0.09f);
        break;

    case PolygonGlyph::BevelChamfer:
        {
            const Point Chamfer[5] =
            {
                { GlyphLow, GlyphHigh }, { GlyphLow, 0.40f }, { 0.40f, GlyphLow },
                { GlyphHigh, GlyphLow }, { GlyphHigh, GlyphHigh }
            };
            AddRun(Strokes, Chamfer, 5, true);
            AddLine(Strokes, GlyphLow, 0.40f, 0.40f, GlyphLow);
            AddDot(Strokes, GlyphLow, 0.40f, 0.05f);
            AddDot(Strokes, 0.40f, GlyphLow, 0.05f);
        }
        break;

    case PolygonGlyph::BridgeSpan:
        AddRing(Strokes, 0.28f, GlyphMid, 0.15f);
        AddRing(Strokes, 0.72f, GlyphMid, 0.15f);
        AddLine(Strokes, 0.28f, GlyphMid - 0.15f, 0.72f, GlyphMid - 0.15f);
        AddLine(Strokes, 0.28f, GlyphMid + 0.15f, 0.72f, GlyphMid + 0.15f);
        break;

    case PolygonGlyph::FillPatch:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddLine(Strokes, GlyphLow, 0.55f, 0.55f, GlyphLow);
        AddLine(Strokes, GlyphLow, 0.75f, 0.75f, GlyphLow);
        AddLine(Strokes, 0.35f, GlyphHigh, GlyphHigh, 0.35f);
        break;

    case PolygonGlyph::GridPatch:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddLine(Strokes, 0.39f, GlyphLow, 0.39f, GlyphHigh);
        AddLine(Strokes, 0.61f, GlyphLow, 0.61f, GlyphHigh);
        AddLine(Strokes, GlyphLow, 0.39f, GlyphHigh, 0.39f);
        AddLine(Strokes, GlyphLow, 0.61f, GlyphHigh, 0.61f);
        break;

    case PolygonGlyph::ShellThickness:
        AddArc(Strokes, GlyphMid, 0.80f, 0.34f, 0.50f, 1.00f);
        AddArc(Strokes, GlyphMid, 0.80f, 0.22f, 0.50f, 1.00f);
        AddLine(Strokes, GlyphMid - 0.34f, 0.80f, GlyphMid - 0.22f, 0.80f);
        AddLine(Strokes, GlyphMid + 0.22f, 0.80f, GlyphMid + 0.34f, 0.80f);
        break;

    case PolygonGlyph::SweepPath:
        AddArc(Strokes, 0.30f, 0.30f, 0.46f, 0.00f, 0.25f);
        AddBox(Strokes, GlyphLow - 0.04f, 0.22f, 0.34f, 0.40f);
        AddDot(Strokes, 0.76f, 0.30f, 0.05f);
        break;

    //--------------------------------------------------------- CUT / SPLIT ---------------------------------------------------------
    case PolygonGlyph::LoopInsert:
        AddBox(Strokes, GlyphLow, 0.28f, GlyphHigh, 0.72f);
        AddLine(Strokes, 0.39f, 0.28f, 0.39f, 0.72f);
        AddLine(Strokes, 0.61f, 0.28f, 0.61f, 0.72f);
        AddLine(Strokes, GlyphMid, GlyphLow, GlyphMid, GlyphHigh);
        break;

    case PolygonGlyph::LoopOffset:
        AddLine(Strokes, 0.38f, GlyphLow, 0.38f, GlyphHigh);
        AddLine(Strokes, 0.62f, GlyphLow, 0.62f, GlyphHigh);
        AddLine(Strokes, 0.38f, GlyphMid, 0.62f, GlyphMid);
        AddArrowHead(Strokes, 0.62f, GlyphMid, 1.0f, 0.0f, 0.08f);
        break;

    case PolygonGlyph::SubdivideQuads:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddLine(Strokes, GlyphMid, GlyphLow, GlyphMid, GlyphHigh);
        AddLine(Strokes, GlyphLow, GlyphMid, GlyphHigh, GlyphMid);
        AddDot(Strokes, GlyphMid, GlyphMid, 0.05f);
        break;

    case PolygonGlyph::UnSubdivideMerge:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddLine(Strokes, GlyphMid - 0.10f, GlyphMid, GlyphMid + 0.10f, GlyphMid);
        AddArrowHead(Strokes, GlyphMid - 0.12f, GlyphMid, -1.0f, 0.0f, 0.08f);
        AddArrowHead(Strokes, GlyphMid + 0.12f, GlyphMid,  1.0f, 0.0f, 0.08f);
        break;

    case PolygonGlyph::ConnectPath:
        AddLine(Strokes, 0.26f, 0.72f, 0.74f, 0.28f);
        AddDot(Strokes, 0.26f, 0.72f, 0.07f);
        AddDot(Strokes, 0.74f, 0.28f, 0.07f);
        break;

    case PolygonGlyph::SeamSplit:
        AddLine(Strokes, 0.30f, GlyphLow, 0.30f, GlyphHigh);
        AddLine(Strokes, 0.70f, GlyphLow, 0.70f, GlyphHigh);
        AddArrowHead(Strokes, 0.20f, GlyphMid, -1.0f, 0.0f, 0.09f);
        AddArrowHead(Strokes, 0.80f, GlyphMid,  1.0f, 0.0f, 0.09f);
        break;

    case PolygonGlyph::RipApart:
        AddLine(Strokes, GlyphMid - 0.05f, GlyphLow, 0.28f, GlyphHigh);
        AddLine(Strokes, GlyphMid + 0.05f, GlyphLow, 0.72f, GlyphHigh);
        AddDot(Strokes, GlyphMid - 0.05f, GlyphLow, 0.06f);
        AddDot(Strokes, GlyphMid + 0.05f, GlyphLow, 0.06f);
        break;

    case PolygonGlyph::PokeCentre:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddLine(Strokes, GlyphLow, GlyphLow, GlyphMid, GlyphMid);
        AddLine(Strokes, GlyphHigh, GlyphLow, GlyphMid, GlyphMid);
        AddLine(Strokes, GlyphLow, GlyphHigh, GlyphMid, GlyphMid);
        AddLine(Strokes, GlyphHigh, GlyphHigh, GlyphMid, GlyphMid);
        AddDot(Strokes, GlyphMid, GlyphMid, 0.055f);
        break;

    case PolygonGlyph::BisectPlane:
        AddBox(Strokes, 0.26f, 0.26f, 0.74f, 0.74f);
        AddLine(Strokes, GlyphLow - 0.06f, GlyphHigh, GlyphHigh + 0.06f, GlyphLow);
        break;

    case PolygonGlyph::PartitionSolid:
        AddBox(Strokes, GlyphLow, 0.30f, 0.62f, GlyphHigh);
        AddBox(Strokes, 0.38f, GlyphLow, GlyphHigh, 0.70f);
        break;

    //-------------------------------------------------------- MERGE / WELD --------------------------------------------------------
    case PolygonGlyph::MergeInward:
        AddDot(Strokes, GlyphMid, GlyphMid, 0.055f);
        AddArrowHead(Strokes, GlyphMid - 0.13f, GlyphMid,  1.0f,  0.0f, 0.10f);
        AddArrowHead(Strokes, GlyphMid + 0.13f, GlyphMid, -1.0f,  0.0f, 0.10f);
        AddArrowHead(Strokes, GlyphMid, GlyphMid - 0.13f,  0.0f,  1.0f, 0.10f);
        AddArrowHead(Strokes, GlyphMid, GlyphMid + 0.13f,  0.0f, -1.0f, 0.10f);
        break;

    case PolygonGlyph::WeldTarget:
        AddDot(Strokes, 0.26f, 0.68f, 0.065f);
        AddRing(Strokes, 0.74f, 0.32f, 0.10f);
        AddLine(Strokes, 0.34f, 0.62f, 0.64f, 0.40f);
        AddArrowHead(Strokes, 0.66f, 0.39f, 0.80f, -0.60f, 0.09f);
        break;

    case PolygonGlyph::CollapseDown:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddDot(Strokes, GlyphMid, GlyphMid, 0.07f);
        AddLine(Strokes, GlyphLow + 0.06f, GlyphLow + 0.06f, GlyphMid - 0.08f, GlyphMid - 0.08f);
        AddLine(Strokes, GlyphHigh - 0.06f, GlyphHigh - 0.06f, GlyphMid + 0.08f, GlyphMid + 0.08f);
        break;

    case PolygonGlyph::CoplanarUnify:
        AddBox(Strokes, GlyphLow, 0.30f, GlyphHigh, 0.70f);
        AddZigzag(Strokes, GlyphMid, 0.30f, GlyphMid, 0.70f, 0.05f, 5);
        break;

    //--------------------------------------------------------- SUBDIVISION --------------------------------------------------------
    case PolygonGlyph::SmoothSurface:
        {
            const Point Faceted[5] =
            {
                { GlyphLow, 0.70f }, { 0.32f, 0.36f }, { GlyphMid, 0.30f }, { 0.68f, 0.36f }, { GlyphHigh, 0.70f }
            };
            AddRun(Strokes, Faceted, 5);
            AddArc(Strokes, GlyphMid, 0.78f, 0.34f, 0.55f, 0.95f);
        }
        break;

    case PolygonGlyph::RelaxNeighbour:
        AddDot(Strokes, GlyphMid, GlyphMid, 0.06f);
        AddRing(Strokes, GlyphMid, GlyphMid, 0.28f);
        AddLine(Strokes, GlyphMid - 0.28f, GlyphMid, GlyphMid - 0.10f, GlyphMid);
        AddLine(Strokes, GlyphMid + 0.28f, GlyphMid, GlyphMid + 0.10f, GlyphMid);
        break;

    case PolygonGlyph::SharpenInverse:
        AddArc(Strokes, GlyphMid, 0.80f, 0.32f, 0.55f, 0.95f);
        {
            const Point Crease[3] = { { 0.24f, 0.66f }, { GlyphMid, 0.26f }, { 0.76f, 0.66f } };
            AddRun(Strokes, Crease, 3);
        }
        break;

    case PolygonGlyph::DecimateSparse:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddLine(Strokes, GlyphLow, GlyphMid, GlyphHigh, GlyphMid);
        AddLine(Strokes, 0.38f, GlyphLow, 0.38f, GlyphMid);
        AddLine(Strokes, 0.62f, GlyphLow, 0.62f, GlyphMid);
        break;

    case PolygonGlyph::RemeshUniform:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddLine(Strokes, GlyphMid, GlyphLow, GlyphMid, GlyphHigh);
        AddLine(Strokes, GlyphLow, GlyphMid, GlyphHigh, GlyphMid);
        AddLine(Strokes, GlyphLow, GlyphLow, GlyphMid, GlyphMid);
        break;

    //------------------------------------------------------ NORMALS / SHADING -----------------------------------------------------
    case PolygonGlyph::NormalArrow:
        AddLine(Strokes, GlyphLow, 0.70f, GlyphHigh, 0.70f);
        AddLine(Strokes, GlyphMid, 0.70f, GlyphMid, 0.26f);
        AddArrowHead(Strokes, GlyphMid, 0.22f, 0.0f, -1.0f);
        break;

    case PolygonGlyph::WindingReverse:
        AddLine(Strokes, GlyphLow, GlyphMid, GlyphHigh, GlyphMid);
        AddLine(Strokes, 0.36f, GlyphMid, 0.36f, GlyphLow);
        AddArrowHead(Strokes, 0.36f, GlyphLow, 0.0f, -1.0f, 0.10f);
        AddLine(Strokes, 0.64f, GlyphMid, 0.64f, GlyphHigh);
        AddArrowHead(Strokes, 0.64f, GlyphHigh, 0.0f, 1.0f, 0.10f);
        break;

    case PolygonGlyph::ShadeSmoothCurve:
        AddArc(Strokes, GlyphMid, 0.78f, 0.32f, 0.55f, 0.95f);
        AddDot(Strokes, GlyphMid, 0.62f, 0.045f);
        break;

    case PolygonGlyph::ShadeFacet:
        {
            const Point Facets[5] =
            {
                { GlyphLow, 0.68f }, { 0.33f, 0.40f }, { GlyphMid, 0.34f }, { 0.67f, 0.40f }, { GlyphHigh, 0.68f }
            };
            AddRun(Strokes, Facets, 5);
            AddDot(Strokes, 0.33f, 0.40f, 0.04f);
            AddDot(Strokes, 0.67f, 0.40f, 0.04f);
        }
        break;

    case PolygonGlyph::SharpEdgeMark:
        AddLine(Strokes, GlyphLow, 0.70f, GlyphMid, 0.32f);
        AddLine(Strokes, GlyphMid, 0.32f, GlyphHigh, 0.70f);
        AddDot(Strokes, GlyphMid, 0.32f, 0.065f);
        break;

    //---------------------------------------------------------- ATTRIBUTES --------------------------------------------------------
    case PolygonGlyph::CreaseWeight:
        AddLine(Strokes, GlyphLow, GlyphMid, GlyphHigh, GlyphMid);
        AddLine(Strokes, 0.30f, GlyphMid, 0.30f, 0.32f);
        AddLine(Strokes, 0.44f, GlyphMid, 0.44f, 0.26f);
        AddLine(Strokes, 0.58f, GlyphMid, 0.58f, 0.32f);
        AddLine(Strokes, 0.72f, GlyphMid, 0.72f, 0.38f);
        break;

    case PolygonGlyph::UvSeamMark:
        AddLine(Strokes, GlyphLow, 0.30f, 0.32f, 0.30f);
        AddLine(Strokes, 0.44f, 0.30f, 0.56f, 0.30f);
        AddLine(Strokes, 0.68f, 0.30f, GlyphHigh, 0.30f);
        AddBox(Strokes, 0.30f, 0.46f, 0.70f, GlyphHigh);
        break;

    case PolygonGlyph::MaterialSwatch:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        {
            const Point Fill[4] = { { 0.28f, 0.28f }, { 0.72f, 0.28f }, { 0.72f, 0.72f }, { 0.28f, 0.72f } };
            AddRun(Strokes, Fill, 4, true, true);
        }
        break;

    //------------------------------------------------------- TOPOLOGY REPAIR ------------------------------------------------------
    case PolygonGlyph::TriangulateSplit:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddLine(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        break;

    case PolygonGlyph::QuadrangulatePair:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddZigzag(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh, 0.05f, 5);
        break;

    case PolygonGlyph::PlanarCorrect:
        {
            const Point Warped[4] = { { GlyphLow, 0.34f }, { 0.70f, GlyphLow }, { GlyphHigh, 0.66f }, { 0.30f, GlyphHigh } };
            AddRun(Strokes, Warped, 4, true);
            AddLine(Strokes, GlyphLow, GlyphMid, GlyphHigh, GlyphMid);
        }
        break;

    case PolygonGlyph::SpinDiagonal:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddLine(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddArc(Strokes, GlyphMid, GlyphMid, 0.17f, 0.10f, 0.60f, 8);
        break;

    case PolygonGlyph::HoleSeal:
        AddArc(Strokes, GlyphMid, GlyphMid, 0.30f, 0.10f, 0.90f, 12);
        AddLine(Strokes, GlyphMid + 0.24f, GlyphMid - 0.18f, GlyphMid + 0.24f, GlyphMid + 0.18f);
        break;

    case PolygonGlyph::LooseReclaim:
        AddBox(Strokes, GlyphLow, 0.52f, GlyphHigh, GlyphHigh);
        AddDot(Strokes, 0.30f, 0.28f, 0.05f);
        AddDot(Strokes, GlyphMid, 0.36f, 0.05f);
        AddDot(Strokes, 0.70f, 0.24f, 0.05f);
        break;

    case PolygonGlyph::NonManifoldRepair:
        AddLine(Strokes, GlyphMid, GlyphLow, GlyphMid, GlyphHigh);
        AddLine(Strokes, GlyphMid, GlyphMid, GlyphLow, 0.30f);
        AddLine(Strokes, GlyphMid, GlyphMid, GlyphHigh, 0.30f);
        AddLine(Strokes, GlyphMid, GlyphMid, GlyphHigh, 0.70f);
        AddDot(Strokes, GlyphMid, GlyphMid, 0.055f);
        break;

    case PolygonGlyph::HullWrap:
        {
            const Point Hull[5] =
            {
                { GlyphLow, GlyphMid }, { 0.36f, GlyphLow }, { GlyphHigh, 0.36f },
                { 0.72f, GlyphHigh }, { 0.28f, 0.78f }
            };
            AddRun(Strokes, Hull, 5, true);
            AddDot(Strokes, GlyphMid, GlyphMid, 0.045f);
            AddDot(Strokes, 0.60f, 0.58f, 0.045f);
        }
        break;

    //----------------------------------------------------- DUPLICATE / SYMMETRY ---------------------------------------------------
    case PolygonGlyph::DuplicateOffset:
        AddBox(Strokes, GlyphLow, 0.34f, 0.64f, GlyphHigh);
        AddBox(Strokes, 0.36f, GlyphLow, GlyphHigh, 0.66f);
        break;

    case PolygonGlyph::ShellSeparate:
        AddBox(Strokes, GlyphLow, 0.30f, 0.44f, 0.70f);
        AddBox(Strokes, 0.56f, 0.30f, GlyphHigh, 0.70f);
        AddArrowHead(Strokes, 0.10f, GlyphMid, -1.0f, 0.0f, 0.08f);
        AddArrowHead(Strokes, 0.90f, GlyphMid,  1.0f, 0.0f, 0.08f);
        break;

    case PolygonGlyph::ExtractSurface:
        AddBox(Strokes, GlyphLow, 0.56f, GlyphHigh, GlyphHigh);
        AddBox(Strokes, 0.32f, GlyphLow, 0.78f, 0.40f);
        AddArrowHead(Strokes, GlyphMid + 0.05f, 0.46f, 0.30f, -0.95f, 0.09f);
        break;

    case PolygonGlyph::DetachComponent:
        AddBox(Strokes, GlyphLow, GlyphLow, 0.58f, GlyphHigh);
        AddDot(Strokes, 0.78f, 0.34f, 0.065f);
        AddLine(Strokes, 0.64f, 0.42f, 0.74f, 0.37f);
        break;

    case PolygonGlyph::MirrorAxis:
        AddLine(Strokes, GlyphMid, GlyphLow - 0.04f, GlyphMid, GlyphHigh + 0.04f);
        {
            const Point Left[3]  = { { 0.38f, 0.30f }, { GlyphLow, GlyphMid }, { 0.38f, 0.70f } };
            const Point Right[3] = { { 0.62f, 0.30f }, { GlyphHigh, GlyphMid }, { 0.62f, 0.70f } };
            AddRun(Strokes, Left, 3, true);
            AddRun(Strokes, Right, 3, true);
        }
        break;

    case PolygonGlyph::SymmetryBalance:
        AddLine(Strokes, GlyphMid, GlyphLow - 0.04f, GlyphMid, GlyphHigh + 0.04f);
        AddArc(Strokes, GlyphMid, GlyphMid, 0.28f, 0.25f, 0.75f);
        AddArc(Strokes, GlyphMid, GlyphMid, 0.28f, 0.75f, 1.25f);
        break;

    //---------------------------------------------------- SELECTION CONVERSION ----------------------------------------------------
    case PolygonGlyph::SelectionGrow:
        AddBox(Strokes, 0.36f, 0.36f, 0.64f, 0.64f);
        AddArrowHead(Strokes, GlyphLow, GlyphLow, -0.707f, -0.707f, 0.10f);
        AddArrowHead(Strokes, GlyphHigh, GlyphLow, 0.707f, -0.707f, 0.10f);
        AddArrowHead(Strokes, GlyphLow, GlyphHigh, -0.707f, 0.707f, 0.10f);
        AddArrowHead(Strokes, GlyphHigh, GlyphHigh, 0.707f, 0.707f, 0.10f);
        break;

    case PolygonGlyph::SelectionShrink:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddArrowHead(Strokes, 0.42f, 0.42f, 0.707f, 0.707f, 0.10f);
        AddArrowHead(Strokes, 0.58f, 0.42f, -0.707f, 0.707f, 0.10f);
        AddArrowHead(Strokes, 0.42f, 0.58f, 0.707f, -0.707f, 0.10f);
        AddArrowHead(Strokes, 0.58f, 0.58f, -0.707f, -0.707f, 0.10f);
        break;

    case PolygonGlyph::LoopHighlight:
        AddBox(Strokes, GlyphLow, 0.28f, GlyphHigh, 0.72f);
        AddLine(Strokes, 0.39f, 0.28f, 0.39f, 0.72f);
        AddLine(Strokes, 0.61f, 0.28f, 0.61f, 0.72f);
        AddLine(Strokes, GlyphLow, GlyphMid, GlyphHigh, GlyphMid);
        break;

    case PolygonGlyph::RingHighlight:
        AddBox(Strokes, GlyphLow, 0.28f, GlyphHigh, 0.72f);
        AddLine(Strokes, 0.39f, 0.28f, 0.39f, 0.72f);
        AddLine(Strokes, 0.61f, 0.28f, 0.61f, 0.72f);
        AddLine(Strokes, 0.28f, 0.28f, 0.28f, 0.72f);
        AddLine(Strokes, 0.72f, 0.28f, 0.72f, 0.72f);
        break;

    case PolygonGlyph::StratumPromote:
        AddDot(Strokes, GlyphLow + 0.02f, 0.70f, 0.065f);
        AddLine(Strokes, 0.34f, 0.62f, 0.56f, 0.40f);
        AddBox(Strokes, 0.58f, GlyphLow, GlyphHigh, 0.42f);
        break;

    case PolygonGlyph::LinkedShell:
        AddBox(Strokes, GlyphLow, 0.34f, 0.52f, GlyphHigh);
        AddLine(Strokes, 0.52f, GlyphMid, 0.68f, GlyphMid);
        AddBox(Strokes, 0.68f, 0.34f, GlyphHigh, 0.66f);
        break;

    case PolygonGlyph::SimilarTrait:
        AddBox(Strokes, GlyphLow, GlyphLow, 0.44f, 0.44f);
        AddBox(Strokes, 0.56f, 0.56f, GlyphHigh, GlyphHigh);
        AddLine(Strokes, 0.44f, 0.44f, 0.56f, 0.56f);
        break;

    case PolygonGlyph::BoundaryTrace:
        AddArc(Strokes, GlyphMid, GlyphMid, 0.30f, 0.08f, 0.92f, 12);
        AddDot(Strokes, GlyphMid + 0.28f, GlyphMid - 0.15f, 0.05f);
        AddDot(Strokes, GlyphMid + 0.28f, GlyphMid + 0.15f, 0.05f);
        break;

    case PolygonGlyph::CheckerSkip:
        AddBox(Strokes, GlyphLow, GlyphLow, GlyphHigh, GlyphHigh);
        AddLine(Strokes, GlyphMid, GlyphLow, GlyphMid, GlyphHigh);
        AddLine(Strokes, GlyphLow, GlyphMid, GlyphHigh, GlyphMid);
        {
            const Point UpperLeft[4]  = { { GlyphLow, GlyphLow }, { GlyphMid, GlyphLow }, { GlyphMid, GlyphMid }, { GlyphLow, GlyphMid } };
            const Point LowerRight[4] = { { GlyphMid, GlyphMid }, { GlyphHigh, GlyphMid }, { GlyphHigh, GlyphHigh }, { GlyphMid, GlyphHigh } };
            AddRun(Strokes, UpperLeft, 4, true, true);
            AddRun(Strokes, LowerRight, 4, true, true);
        }
        break;

    case PolygonGlyph::ShortestPath:
        AddDot(Strokes, GlyphLow, 0.70f, 0.065f);
        AddDot(Strokes, GlyphHigh, 0.30f, 0.065f);
        {
            const Point Path[4] = { { GlyphLow, 0.70f }, { 0.40f, 0.66f }, { 0.60f, 0.38f }, { GlyphHigh, 0.30f } };
            AddRun(Strokes, Path, 4);
        }
        break;

    case PolygonGlyph::TraitFilter:
        {
            const Point Funnel[5] =
            {
                { GlyphLow, GlyphLow }, { GlyphHigh, GlyphLow }, { 0.58f, GlyphMid },
                { 0.58f, GlyphHigh }, { 0.42f, GlyphHigh }
            };
            AddRun(Strokes, Funnel, 5);
            AddLine(Strokes, 0.42f, GlyphHigh, 0.42f, GlyphMid);
            AddLine(Strokes, 0.42f, GlyphMid, GlyphLow, GlyphLow);
        }
        break;

    //----------------------------------------------------------- REMOVAL ----------------------------------------------------------
    case PolygonGlyph::RemoveCross:
        AddLine(Strokes, 0.26f, 0.26f, 0.74f, 0.74f);
        AddLine(Strokes, 0.74f, 0.26f, 0.26f, 0.74f);
        break;

    case PolygonGlyph::DissolveFade:
        AddBox(Strokes, GlyphLow, 0.30f, GlyphHigh, 0.70f);
        AddLine(Strokes, GlyphMid, 0.30f, GlyphMid, 0.40f);
        AddLine(Strokes, GlyphMid, 0.46f, GlyphMid, 0.54f);
        AddLine(Strokes, GlyphMid, 0.60f, GlyphMid, 0.70f);
        break;

    case PolygonGlyph::DissolveAngle:
        AddLine(Strokes, GlyphLow, 0.66f, GlyphMid, 0.40f);
        AddLine(Strokes, GlyphMid, 0.40f, GlyphHigh, 0.62f);
        AddArc(Strokes, GlyphMid, 0.44f, 0.16f, 0.10f, 0.40f, 8);
        break;

    //------------------------------------------------- CREATION PRIMITIVES -------------------------------------------------
    // 📝 These picture the RESULT rather than an operation, so they are drawn as solids in the same three-quarter view the stratum
    //    badges use: a visible top face plus two side faces. The shared vanishing geometry is what makes Box, Cylinder and Cone read
    //    as members of one family at 20 px rather than as three unrelated outlines.

    case PolygonGlyph::PrimitiveBox:
    {
        // Front face, then the top and right faces sharing its edges — a cube needs all three or it reads as a flat square.
        const Point Front[4] = { { 0.22f, 0.40f }, { 0.62f, 0.40f }, { 0.62f, 0.80f }, { 0.22f, 0.80f } };
        AddRun(Strokes, Front, 4, true);
        const Point Top[4] = { { 0.22f, 0.40f }, { 0.40f, 0.22f }, { 0.80f, 0.22f }, { 0.62f, 0.40f } };
        AddRun(Strokes, Top, 4, true);
        const Point Side[4] = { { 0.62f, 0.40f }, { 0.80f, 0.22f }, { 0.80f, 0.62f }, { 0.62f, 0.80f } };
        AddRun(Strokes, Side, 4, true);
        break;
    }

    case PolygonGlyph::PrimitivePlane:
    {
        // A parallelogram, not a rectangle: a flat quad lying in the ground plane is only distinguishable from a face-on square by
        // its perspective.
        const Point Quad[4] = { { 0.18f, 0.62f }, { 0.42f, 0.34f }, { 0.86f, 0.34f }, { 0.62f, 0.62f } };
        AddRun(Strokes, Quad, 4, true);
        break;
    }

    case PolygonGlyph::PrimitiveSphere:
        AddRing(Strokes, GlyphMid, GlyphMid, 0.30f);
        // One latitude ellipse. A bare circle is ambiguous with PrimitiveCircle, and this single line resolves it.
        AddEllipticArc(Strokes, GlyphMid, GlyphMid, 0.30f, 0.11f, 0.0f, 1.0f, true);
        break;

    case PolygonGlyph::PrimitiveCylinder:
        // Top cap closed, bottom cap only its front half — the back half would be hidden by the body.
        AddEllipticArc(Strokes, GlyphMid, 0.28f, 0.26f, 0.10f, 0.0f, 1.0f, true);
        AddEllipticArc(Strokes, GlyphMid, 0.72f, 0.26f, 0.10f, 0.0f, 0.5f);
        AddLine(Strokes, GlyphMid - 0.26f, 0.28f, GlyphMid - 0.26f, 0.72f);
        AddLine(Strokes, GlyphMid + 0.26f, 0.28f, GlyphMid + 0.26f, 0.72f);
        break;

    case PolygonGlyph::PrimitiveCone:
        AddEllipticArc(Strokes, GlyphMid, 0.74f, 0.26f, 0.10f, 0.0f, 0.5f);
        AddLine(Strokes, GlyphMid - 0.26f, 0.74f, GlyphMid, 0.20f);
        AddLine(Strokes, GlyphMid + 0.26f, 0.74f, GlyphMid, 0.20f);
        // The back half of the base, so the cone sits ON the ellipse rather than floating above an arc.
        AddEllipticArc(Strokes, GlyphMid, 0.74f, 0.26f, 0.10f, 0.5f, 1.0f);
        break;

    case PolygonGlyph::PrimitiveTorus:
        AddEllipticArc(Strokes, GlyphMid, GlyphMid, 0.32f, 0.16f, 0.0f, 1.0f, true);
        AddEllipticArc(Strokes, GlyphMid, GlyphMid, 0.14f, 0.06f, 0.0f, 1.0f, true);
        break;

    case PolygonGlyph::PrimitiveCircle:
        AddEllipticArc(Strokes, GlyphMid, GlyphMid, 0.30f, 0.30f, 0.0f, 1.0f, true);
        AddDot(Strokes, GlyphMid, GlyphMid, 0.045f);
        break;

    case PolygonGlyph::PrimitiveVertex:
        // A single point needs a size cue, or it reads as a stray mark. Crosshair stubs supply one without implying an axis.
        AddDot(Strokes, GlyphMid, GlyphMid, 0.10f);
        AddLine(Strokes, GlyphLow, GlyphMid, GlyphLow + 0.10f, GlyphMid);
        AddLine(Strokes, GlyphHigh - 0.10f, GlyphMid, GlyphHigh, GlyphMid);
        AddLine(Strokes, GlyphMid, GlyphLow, GlyphMid, GlyphLow + 0.10f);
        AddLine(Strokes, GlyphMid, GlyphHigh - 0.10f, GlyphMid, GlyphHigh);
        break;

    case PolygonGlyph::PrimitiveGrid:
    {
        const Point Quad[4] = { { 0.18f, 0.66f }, { 0.42f, 0.30f }, { 0.86f, 0.30f }, { 0.62f, 0.66f } };
        AddRun(Strokes, Quad, 4, true);
        // Two interior lines each way. Any more and the cell walls merge into grey at tile size.
        AddLine(Strokes, 0.26f, 0.54f, 0.70f, 0.54f);
        AddLine(Strokes, 0.34f, 0.42f, 0.78f, 0.42f);
        AddLine(Strokes, 0.34f, 0.66f, 0.58f, 0.30f);
        AddLine(Strokes, 0.46f, 0.66f, 0.70f, 0.30f);
        break;
    }

    case PolygonGlyph::PrimitiveText:
        // A capital "A" on a baseline: the letterform says text where an abstract mark could not.
        AddLine(Strokes, 0.30f, 0.68f, 0.46f, 0.26f);
        AddLine(Strokes, 0.46f, 0.26f, 0.62f, 0.68f);
        AddLine(Strokes, 0.36f, 0.52f, 0.56f, 0.52f);
        AddLine(Strokes, GlyphLow, 0.78f, GlyphHigh, 0.78f);
        break;

    case PolygonGlyph::CurveLine:
        // Open polyline with its control points shown — the handles are what separate a CURVE from a drawn edge.
        AddLine(Strokes, 0.22f, 0.70f, 0.44f, 0.34f);
        AddLine(Strokes, 0.44f, 0.34f, 0.78f, 0.58f);
        AddDot(Strokes, 0.22f, 0.70f, 0.065f);
        AddDot(Strokes, 0.44f, 0.34f, 0.065f);
        AddDot(Strokes, 0.78f, 0.58f, 0.065f);
        break;

    case PolygonGlyph::CurveBezier:
    {
        // A cubic drawn as chords, with the two handles struck out to their control points.
        Point Curve[9] = {};
        for (int Index = 0; Index < 9; ++Index)
        {
            const float T = static_cast<float>(Index) / 8.0f;
            const float U = 1.0f - T;
            // Control points: (0.20,0.72) (0.30,0.26) (0.70,0.74) (0.80,0.28)
            Curve[Index].X = U*U*U*0.20f + 3.0f*U*U*T*0.30f + 3.0f*U*T*T*0.70f + T*T*T*0.80f;
            Curve[Index].Y = U*U*U*0.72f + 3.0f*U*U*T*0.26f + 3.0f*U*T*T*0.74f + T*T*T*0.28f;
        }
        AddRun(Strokes, Curve, 9);
        AddLine(Strokes, 0.20f, 0.72f, 0.30f, 0.26f);
        AddLine(Strokes, 0.80f, 0.28f, 0.70f, 0.74f);
        AddDot(Strokes, 0.30f, 0.26f, 0.055f);
        AddDot(Strokes, 0.70f, 0.74f, 0.055f);
        break;
    }

    case PolygonGlyph::CurveCircle:
        AddRing(Strokes, GlyphMid, GlyphMid, 0.28f);
        AddDot(Strokes, GlyphMid, GlyphMid - 0.28f, 0.06f);
        AddDot(Strokes, GlyphMid + 0.28f, GlyphMid, 0.06f);
        break;

    case PolygonGlyph::CurveSpiral:
    {
        // Radius shrinking as the angle advances — one and a half turns, which is enough to read as a coil.
        Point Coil[14] = {};
        for (int Index = 0; Index < 14; ++Index)
        {
            const float Fraction = static_cast<float>(Index) / 13.0f;
            const float Radians  = Fraction * 1.5f * 6.28318530718f;
            const float Radius   = 0.30f * (1.0f - Fraction * 0.72f);
            Coil[Index].X = GlyphMid + std::cos(Radians) * Radius;
            Coil[Index].Y = GlyphMid + std::sin(Radians) * Radius;
        }
        AddRun(Strokes, Coil, 14);
        break;
    }

    case PolygonGlyph::LightPoint:
        AddRing(Strokes, GlyphMid, GlyphMid, 0.15f);
        // Four radiating stubs on the diagonals, gapped from the bulb so they read as emitted rather than attached.
        // ⚠️ FOUR, not eight: eight stubs plus the ring lands exactly on GlyphRunCapacity (8), and AppendGlyphRun drops
        //    an over-budget run SILENTLY — a glyph sitting on the cap loses any stroke added to it later with no
        //    diagnostic. The diagonals alone carry the "emitting" read at 20 px, where eight stubs merge into a grey halo.
        AddLine(Strokes, 0.30f, 0.30f, 0.38f, 0.38f);
        AddLine(Strokes, 0.70f, 0.70f, 0.62f, 0.62f);
        AddLine(Strokes, 0.70f, 0.30f, 0.62f, 0.38f);
        AddLine(Strokes, 0.30f, 0.70f, 0.38f, 0.62f);
        break;

    case PolygonGlyph::LightDirectional:
        // Parallel rays with heads: the parallelism IS the distinction from a point light.
        AddLine(Strokes, 0.24f, 0.24f, 0.52f, 0.52f);
        AddArrowHead(Strokes, 0.58f, 0.58f, 0.7071f, 0.7071f, 0.10f);
        AddLine(Strokes, 0.46f, 0.20f, 0.70f, 0.44f);
        AddArrowHead(Strokes, 0.76f, 0.50f, 0.7071f, 0.7071f, 0.10f);
        AddLine(Strokes, 0.20f, 0.46f, 0.44f, 0.70f);
        AddArrowHead(Strokes, 0.50f, 0.76f, 0.7071f, 0.7071f, 0.10f);
        break;

    case PolygonGlyph::LightSpot:
    {
        // Head plus a widening cone onto an elliptical pool — the pool is what makes it a spot rather than a cone primitive.
        const Point Head[4] = { { 0.38f, 0.18f }, { 0.62f, 0.18f }, { 0.66f, 0.30f }, { 0.34f, 0.30f } };
        AddRun(Strokes, Head, 4, true);
        AddLine(Strokes, 0.34f, 0.30f, 0.22f, 0.72f);
        AddLine(Strokes, 0.66f, 0.30f, 0.78f, 0.72f);
        AddEllipticArc(Strokes, GlyphMid, 0.74f, 0.28f, 0.08f, 0.0f, 1.0f, true);
        break;
    }

    case PolygonGlyph::LightArea:
    {
        const Point Panel[4] = { { 0.20f, 0.24f }, { 0.80f, 0.24f }, { 0.72f, 0.44f }, { 0.28f, 0.44f } };
        AddRun(Strokes, Panel, 4, true);
        AddLine(Strokes, 0.34f, 0.52f, 0.30f, 0.76f);
        AddLine(Strokes, GlyphMid, 0.52f, GlyphMid, 0.78f);
        AddLine(Strokes, 0.66f, 0.52f, 0.70f, 0.76f);
        break;
    }

    case PolygonGlyph::CameraBody:
    {
        const Point Body[4] = { { 0.18f, 0.34f }, { 0.62f, 0.34f }, { 0.62f, 0.68f }, { 0.18f, 0.68f } };
        AddRun(Strokes, Body, 4, true);
        // The lens barrel as a wedge off the body, which is the shape that reads as "camera" at this size.
        const Point Barrel[3] = { { 0.62f, 0.44f }, { 0.84f, 0.34f }, { 0.84f, 0.68f } };
        AddRun(Strokes, Barrel, 3, true);
        AddDot(Strokes, 0.28f, 0.28f, 0.05f);
        break;
    }

    case PolygonGlyph::ReferenceImage:
    {
        const Point Frame[4] = { { 0.20f, 0.24f }, { 0.80f, 0.24f }, { 0.80f, 0.76f }, { 0.20f, 0.76f } };
        AddRun(Strokes, Frame, 4, true);
        // A horizon and a sun: the picture-within-a-frame is what separates this from a plain plane.
        AddLine(Strokes, 0.20f, 0.60f, 0.38f, 0.44f);
        AddLine(Strokes, 0.38f, 0.44f, 0.56f, 0.60f);
        AddLine(Strokes, 0.56f, 0.60f, 0.68f, 0.50f);
        AddLine(Strokes, 0.68f, 0.50f, 0.80f, 0.60f);
        AddDot(Strokes, 0.64f, 0.34f, 0.055f);
        break;
    }

    case PolygonGlyph::EmptyPivot:
        // Three axis stubs from an origin and nothing else — an empty carries a transform but no geometry, and the glyph says so by
        // showing only the transform.
        AddLine(Strokes, GlyphMid, GlyphMid, GlyphMid, 0.22f);
        AddLine(Strokes, GlyphMid, GlyphMid, 0.78f, 0.62f);
        AddLine(Strokes, GlyphMid, GlyphMid, 0.22f, 0.62f);
        AddDot(Strokes, GlyphMid, GlyphMid, 0.06f);
        break;

    // 📝 Blank and any identity added to the enum without a path here fall through to an empty set, which InscribeGlyph draws as
    //    nothing. Deliberately silent: a new catalogue row is more useful with a missing icon than with a failed assertion.
    case PolygonGlyph::Blank:
    default:
        break;
    }

    return Strokes;
}

void InscribePolygonGlyph(ImDrawList* DrawList, PolygonGlyph Glyph, ImVec2 BoxOrigin, float BoxSide, ImU32 Tint, float StrokeWidth)
{
    const GlyphStrokeSet Strokes = ResolvePolygonGlyph(Glyph);
    InscribeGlyph(DrawList, Strokes, BoxOrigin, BoxSide, Tint, StrokeWidth);
}

}   // namespace Frontier
